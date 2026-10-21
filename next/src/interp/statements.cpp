// The instruction loop: statements, branches, switches, declarations and the lifetime of variable length arrays.

#include <algorithm>
#include <format>

#include "ast/casting.hpp"
#include "interp/execution.hpp"
#include "sema/types.hpp"
#include "support/assert.hpp"

namespace nitwit::interp {

namespace {

/// A jump out of the instructions of a statement expression (GNU: break, continue or goto out of `({ ... })`). The
/// statement expression is abandoned; the loop that runs the target's instructions catches it.
struct LeaveRange {
    std::uint32_t target = 0;
};

/// `return` inside a statement expression: ends the function, abandoning the expression.
struct ReturnFromRange {
    Returned result;
};

bool contains(const InstructionRange* range, std::uint32_t pc) {
    return range == nullptr || (pc >= range->begin && pc < range->end);
}

} // namespace

void Execution::execute(Frame& frame, std::uint32_t begin, const InstructionRange* range) {
    const std::vector<Instruction>& code = frame.body->code;
    std::uint32_t pc = begin;
    const auto jump = [&](std::uint32_t target) {
        if (!contains(range, target)) {
            throw LeaveRange{target};
        }
        if (frame.arrayCount > code[target].vlaDepth) {
            releaseDynamic(frame, code[target].vlaDepth);
        }
        pc = target;
    };
    while (true) {
        try {
            while (true) {
                const Instruction& instruction = code[pc];
                if (range == nullptr && !frame.temporaries.empty()) {
                    releaseTemporaries(frame); // the full expression of the previous instruction has ended
                }
                switch (instruction.op) {
                case OpCode::Statement:
                    location_ = instruction.stmt->location();
                    countStep();
                    beforeStatement(instruction.stmt);
                    ++pc;
                    break;
                case OpCode::Evaluate:
                    effect(instruction.expr);
                    ++pc;
                    break;
                case OpCode::Declare:
                    declare(frame, instruction.decl);
                    ++pc;
                    break;
                case OpCode::Branch: {
                    countStep(); // also counts loops whose bodies have no statements
                    const bool outcome = instruction.expr == nullptr || test(instruction.expr);
                    afterCondition({.construct = instruction.construct,
                                    .stmt = instruction.stmt,
                                    .conditional = nullptr,
                                    .location = instruction.location,
                                    .outcome = outcome,
                                    .switchValue = {},
                                    .switchLabel = nullptr});
                    if (outcome) {
                        ++pc;
                    } else {
                        jump(instruction.target);
                    }
                    break;
                }
                case OpCode::Jump:
                    jump(instruction.target);
                    break;
                case OpCode::Switch:
                    countStep();
                    jump(switchTarget(frame, instruction));
                    break;
                case OpCode::Return:
                    if (instruction.expr != nullptr) {
                        frame.result = returnedValue(instruction.expr);
                    }
                    if (range != nullptr) {
                        throw ReturnFromRange{std::move(frame.result)};
                    }
                    return;
                case OpCode::FallOff:
                    return;
                case OpCode::EndScope:
                    releaseDynamic(frame, instruction.target);
                    ++pc;
                    break;
                case OpCode::StatementValue:
                    frame.statementValue = rvalue(instruction.expr);
                    ++pc;
                    break;
                case OpCode::EndStatementValue:
                    return;
                case OpCode::Unsupported:
                    stop(Termination::Unsupported, "inline assembly is not supported", instruction.stmt->location());
                }
            }
        } catch (const LeaveRange& leave) {
            // Thrown by a statement expression evaluated in this loop, or by this loop itself.
            if (!contains(range, leave.target)) {
                throw;
            }
            jump(leave.target);
        } catch (ReturnFromRange& returned) {
            if (range != nullptr) {
                throw;
            }
            frame.result = std::move(returned.result);
            return;
        }
    }
}

// C17 6.8.6.4p3: the value of the expression is returned. A struct or union is copied, with the initialization state
// and the origins of its bytes, into a temporary of the caller, since this frame's objects end now.
Returned Execution::returnedValue(const ast::Expr* expr) {
    const TypeInfo type = types_.describe(expr->type());
    if (type.representation == Representation::Record && frames_.size() >= 2) {
        const Address source = recordObject(expr);
        const Address object = temporary(type.size, frames_[frames_.size() - 2], expr);
        copyObject(object, source, type.size, expr);
        return {.value = value::Value::ofBytes(readBytes(object, type.size, expr)), .object = object};
    }
    value::Value result = rvalue(expr);
    if (result.kind() == value::Value::Kind::None) {
        return {};
    }
    return {.value = std::move(result), .object = 0};
}

// C17 6.8.4.2p5: the promoted condition is compared with the converted case values; without a match, control goes to
// the default label or past the switch.
std::uint32_t Execution::switchTarget(const Frame& frame, const Instruction& instruction) {
    const SwitchTable& table = frame.body->switches[instruction.index];
    const value::Value condition = rvalue(instruction.expr);
    const Int128 key = condition.signedBits();
    const auto next = std::ranges::upper_bound(table.cases, key, {}, &SwitchTable::Case::low);
    const SwitchTable::Case* match = nullptr;
    if (next != table.cases.begin() && key <= std::prev(next)->high) {
        match = &*std::prev(next);
    }
    const ast::Stmt* label = match != nullptr ? static_cast<const ast::Stmt*>(match->label) : table.defaultLabel;
    afterCondition({.construct = ConditionEvent::Construct::Switch,
                    .stmt = instruction.stmt,
                    .conditional = nullptr,
                    .location = instruction.location,
                    .outcome = label != nullptr,
                    .switchValue = condition,
                    .switchLabel = label});
    return match != nullptr ? match->target : table.defaultTarget;
}

void Execution::declare(Frame& frame, const ast::Decl* decl) {
    if (const auto* typedefDecl = ast::dyn_cast<ast::TypedefDecl>(decl)) {
        return evaluateArraySizes(frame, typedefDecl->underlying());
    }
    const auto* var = ast::cast<ast::VarDecl>(decl);
    evaluateArraySizes(frame, var->type());
    if (var->isParameter()) {
        return; // allocated at entry; only the sizes of its type were evaluated (C17 6.9.1p10)
    }
    const ObjectLayout& layout = frame.body->locals[var->localIndex()];
    if (layout.isVariableLength) {
        // C17 6.2.4p7: a variable length array lives from its declaration to the end of its scope.
        ast::QualType element = var->type();
        while (const auto* array = ast::dyn_cast<ast::ArrayType>(desugared(element))) {
            element = array->element();
        }
        const std::uint64_t alignment = std::max(sema::alignOf(context_, element), var->requestedAlignment());
        const Address address =
            allocate(Region::Stack, sizeOf(var->type(), nullptr), alignment, var->name().text(), nullptr);
        frame.dynamic.push_back({.address = address, .isArray = true});
        ++frame.arrayCount;
        frame.locals[var->localIndex()] = address;
    }
    if (var->initializer() != nullptr) {
        initialize(frame.locals[var->localIndex()], var->type(), var->initializer(), false);
    } else if (!layout.isVariableLength) {
        // C17 6.2.4p6: reaching the declaration again (in a loop, after a goto) makes the value indeterminate.
        if (const auto cleared = memory_.markUninitialized(frame.locals[var->localIndex()], layout.size); !cleared) {
            memoryError(cleared.error(), true, nullptr);
        }
    }
}

// C17 6.7.6.2p5: the size expressions of a variable length array type are evaluated where the declaration is
// reached. Types behind typedefs and typeof keep the sizes evaluated at their own declarations.
void Execution::evaluateArraySizes(Frame& frame, ast::QualType type) {
    const ast::Type* current = type.type();
    while (true) {
        if (const auto* array = ast::dyn_cast<ast::ArrayType>(current)) {
            if (array->sizeKind() == ast::ArrayType::SizeKind::Variable) {
                const ast::Expr* sizeExpression = array->sizeExpression();
                const value::Value count = rvalue(sizeExpression);
                if (types_.describe(sizeExpression->type()).integer.isSigned && count.signedBits() < 0) {
                    undefined("a variable length array with a negative size", sizeExpression);
                }
                const auto size = static_cast<std::uint64_t>(count.bits());
                const auto known =
                    std::ranges::find_if(frame.arraySizes, [&](const auto& entry) { return entry.first == array; });
                if (known != frame.arraySizes.end()) {
                    known->second = size;
                } else {
                    frame.arraySizes.emplace_back(array, size);
                }
            }
            current = array->element().type();
        } else if (const auto* pointer = ast::dyn_cast<ast::PointerType>(current)) {
            current = pointer->pointee().type();
        } else {
            return;
        }
    }
}

// Leaving the scope of a variable length array ends it; GCC also frees the alloca blocks allocated after it, since
// both live on the stack above it.
void Execution::releaseDynamic(Frame& frame, std::uint32_t arrayDepth) {
    while (frame.arrayCount > arrayDepth) {
        const DynamicObject object = frame.dynamic.back();
        frame.dynamic.pop_back();
        memory_.popStack(object.address);
        if (object.isArray) {
            --frame.arrayCount;
        }
    }
}

} // namespace nitwit::interp
