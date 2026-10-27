// Non-local jumps (C11 7.13): setjmp saves an environment and writes a handle to it into the jmp_buf; longjmp unwinds
// the host stack to the instruction loop of the environment's frame, which evaluates the instruction of the setjmp call
// again, the call returning longjmp's value (interp/README.md, "Non-local jumps").

#include <algorithm>
#include <array>
#include <format>

#include "ast/casting.hpp"
#include "interp/execution.hpp"
#include "sema/constant.hpp"
#include "support/assert.hpp"

namespace nitwit::interp {

namespace {

/// A jmp_buf holds the id of its environment in its first 8 bytes, little-endian; 0 is no environment. glibc's jmp_buf
/// is larger in both data models (156 and 200 bytes).
constexpr std::size_t handleSize = 8;

bool isConstant(const ast::Expr* expr, ast::AstContext& context) {
    return sema::evaluateIntegerConstant(context, expr).has_value();
}

/// Whether the lvalue designates the same object whenever it is evaluated, without side effects: a variable, a member
/// of one, or an element of an array variable with a constant index.
bool isFixedObject(const ast::Expr* expr, ast::AstContext& context) {
    while (true) {
        switch (expr->kind()) {
        case ast::ExprKind::Paren:
            expr = ast::cast<ast::ParenExpr>(expr)->inner();
            break;
        case ast::ExprKind::DeclRef:
            return ast::isa<ast::VarDecl>(ast::cast<ast::DeclRefExpr>(expr)->decl());
        case ast::ExprKind::Member: {
            const auto* member = ast::cast<ast::MemberExpr>(expr);
            if (member->isArrow()) {
                return false;
            }
            expr = member->base();
            break;
        }
        case ast::ExprKind::ArraySubscript: {
            const auto* subscript = ast::cast<ast::ArraySubscriptExpr>(expr);
            const auto* decay = ast::dyn_cast<ast::ImplicitCastExpr>(subscript->base());
            if (decay == nullptr || decay->castKind() != ast::CastKind::ArrayToPointerDecay
                || !isConstant(subscript->index(), context)) {
                return false;
            }
            expr = decay->operand();
            break;
        }
        default:
            return false;
        }
    }
}

/// The operand of `expr` that contains the setjmp call, if `expr` is one of the expressions around the call that
/// evaluate only constants and fixed objects besides it; null otherwise.
const ast::Expr* operandAroundCall(const ast::Expr* expr, ast::AstContext& context) {
    switch (expr->kind()) {
    case ast::ExprKind::Paren:
        return ast::cast<ast::ParenExpr>(expr)->inner();
    case ast::ExprKind::Cast:
        return ast::cast<ast::CastExpr>(expr)->operand();
    case ast::ExprKind::ImplicitCast:
        return ast::cast<ast::ImplicitCastExpr>(expr)->operand();
    case ast::ExprKind::Unary: {
        const auto* unary = ast::cast<ast::UnaryExpr>(expr);
        return unary->op() == ast::UnaryOperator::LogicalNot ? unary->operand() : nullptr;
    }
    case ast::ExprKind::Binary: {
        const auto* binary = ast::cast<ast::BinaryExpr>(expr);
        switch (binary->op()) {
        case ast::BinaryOperator::Less:
        case ast::BinaryOperator::Greater:
        case ast::BinaryOperator::LessEqual:
        case ast::BinaryOperator::GreaterEqual:
        case ast::BinaryOperator::Equal:
        case ast::BinaryOperator::NotEqual:
            if (isConstant(binary->rhs(), context)) {
                return binary->lhs();
            }
            return isConstant(binary->lhs(), context) ? binary->rhs() : nullptr;
        case ast::BinaryOperator::Assign:
            return isFixedObject(binary->lhs(), context) ? binary->rhs() : nullptr;
        default:
            return nullptr;
        }
    }
    default:
        return nullptr;
    }
}

/// Whether evaluating `root` again does exactly what the return of `call` in it again does natively: on the way from
/// `root` to the call, everything else that is evaluated is a constant or a fixed object, so that evaluating it again
/// gives what the first evaluation gave. That covers the uses of C11 7.13.1.1p4 (the whole controlling expression or
/// expression statement, under `!`, compared with an integer constant expression, cast to void) and the value
/// assigned to a variable or initializing one, which GCC and Clang support as well.
bool isResumableIn(const ast::Expr* root, const ast::CallExpr* call, ast::AstContext& context) {
    for (const ast::Expr* expr = root; expr != nullptr; expr = operandAroundCall(expr, context)) {
        if (expr == call) {
            return true;
        }
    }
    return false;
}

/// The expression of the instruction that a setjmp call in it is evaluated in: an expression statement, a controlling
/// expression or an initializer. Null for the other instructions; after `return setjmp(env);`, for example, no
/// longjmp can come back, since the function has returned.
const ast::Expr* rootOf(const Instruction& instruction) {
    switch (instruction.op) {
    case OpCode::Evaluate:
    case OpCode::Branch:
    case OpCode::Switch:
        return instruction.expr;
    case OpCode::Declare:
        if (const auto* var = ast::dyn_cast<ast::VarDecl>(instruction.decl)) {
            return var->initializer();
        }
        return nullptr;
    default:
        return nullptr;
    }
}

} // namespace

// C11 7.13.1.1: setjmp saves the calling environment and returns 0. A second call at the same site in the same frame,
// as in a loop, saves the same environment again, under the same id, so that the environments do not grow with the
// iterations.
value::Value Execution::setJump(const ast::CallExpr* call, const Arguments& arguments) {
    NITWIT_ASSERT(frame_ != nullptr, "setjmp outside a function");
    if (arguments.values.empty() || types_.describe(arguments.types[0]).representation != Representation::Pointer) {
        stop(Termination::Unsupported, "setjmp without a pointer to a jmp_buf", call->location());
    }
    const Frame& frame = *frame_;
    JumpEnvironment environment{.id = 0,
                                .frameSerial = frame.serial,
                                .depth = frames_.size() - 1,
                                .pc = frame.pc,
                                .call = call,
                                .dynamicCount = frame.dynamic.size(),
                                .isInVariablyModifiedScope = frame.arrayCount > 0 || !frame.arraySizes.empty(),
                                .location = location_};
    // The environments of the current frame are the last ones (JumpEnvironment).
    auto saved = jumpEnvironments_.rbegin();
    while (saved != jumpEnvironments_.rend() && saved->frameSerial == frame.serial && saved->call != call) {
        ++saved;
    }
    if (saved != jumpEnvironments_.rend() && saved->frameSerial == frame.serial) {
        environment.id = saved->id;
        *saved = environment;
    } else {
        environment.id = nextJumpId_++;
        jumpEnvironments_.push_back(environment);
    }
    std::array<std::byte, handleSize> handle{};
    for (std::size_t i = 0; i < handleSize; ++i) {
        handle[i] = static_cast<std::byte>((environment.id >> (8 * i)) & 0xFF);
    }
    writeBytes(static_cast<Address>(arguments.values[0].bits()), handle, call);
    return value::Value::integer(0, types_.intFormat());
}

// C11 7.13.2.1: longjmp restores the environment, and the setjmp call returns `val`, or 1 if `val` is 0. Undefined if
// setjmp did not save the environment or its function has returned.
void Execution::longJump(const ast::CallExpr* call, const Arguments& arguments) {
    if (arguments.values.size() < 2 || types_.describe(arguments.types[0]).representation != Representation::Pointer
        || !types_.describe(arguments.types[1]).isIntegral()) {
        stop(Termination::Unsupported, "longjmp without a pointer to a jmp_buf and an integer", call->location());
    }
    std::array<std::byte, handleSize> handle{};
    const auto read = memory_.readScalar(static_cast<Address>(arguments.values[0].bits()), handle);
    if (!read) {
        memoryError(read.error(), false, call);
    }
    std::uint64_t id = 0;
    for (std::size_t i = 0; read->isInitialized && i < handleSize; ++i) {
        id |= std::to_integer<std::uint64_t>(handle[i]) << (8 * i);
    }
    const auto found = std::ranges::find(jumpEnvironments_, id, &JumpEnvironment::id);
    if (id == 0 || found == jumpEnvironments_.end()) {
        undefined(id != 0 && id < nextJumpId_ ? "longjmp to an environment whose function has returned (C11 7.13.2.1p2)"
                                              : "longjmp to an environment that setjmp did not save (C11 7.13.2.1p2)",
                  call);
    }
    const JumpEnvironment environment = *found;
    NITWIT_ASSERT(environment.depth < frames_.size() && frames_[environment.depth]->serial == environment.frameSerial,
                  "an environment of a frame that has returned");
    if (const std::optional<std::string> reason = whyNotResumable(environment)) {
        stop(Termination::Unsupported, std::format("longjmp to a setjmp call {}", *reason), call->location());
    }

    const value::IntegerFormat format = types_.intFormat();
    const value::Value requested =
        convertInteger(arguments.values[1], types_.describe(arguments.types[1]).integer, format, call);
    const bool isZero = requested.bits() == 0;
    value::Value result = isZero ? value::Value::integer(1, format) : requested;
    if (requested.term()) {
        // `val == 0 ? 1 : val`, with the operand the run selected.
        value::Value test = value::Value::integer(isZero ? 1 : 0, format);
        attach(test,
               terms_.integerCompare(value::Comparison::Equal, requested, value::Value::integer(0, format), format),
               call);
        attach(result, terms_.conditional(test, format, result, format), call);
    }
    throw LongJump{.environment = environment, .value = std::move(result)};
}

std::optional<std::string> Execution::whyNotResumable(const JumpEnvironment& environment) {
    if (environment.call->directCallee() == nullptr) {
        return "through a function pointer";
    }
    // C11 7.13.2.1p2: undefined once the scope has been left; the interpreter does not follow which scopes it left.
    if (environment.isInVariablyModifiedScope) {
        return "in a function that declared a variably modified type before it";
    }
    const FunctionCode& body = *frames_[environment.depth]->body;
    for (const auto& [statementExpr, range] : body.statementExpressions) {
        if (environment.pc >= range.begin && environment.pc < range.end) {
            return "in a statement expression";
        }
    }
    const ast::Expr* root = rootOf(body.code[environment.pc]);
    if (root == nullptr || !isResumableIn(root, environment.call, context_)) {
        return "in an expression that the interpreter cannot resume: setjmp must be the whole controlling expression "
               "or expression statement, perhaps under `!`, compared with an integer constant or assigned to a "
               "variable (C11 7.13.1.1p4)";
    }
    return std::nullopt;
}

// The stack objects that the frame allocated after the setjmp call end: longjmp restores the stack pointer. They are
// variable length arrays declared after the call and alloca blocks (C11 7.13.2.1p2 makes a jump back into the scope of
// a variable length array undefined; such environments do not resume, see whyNotResumable).
std::uint32_t Execution::resume(Frame& frame, LongJump& jump) {
    NITWIT_ASSERT(frame_ == &frame, "the frames above the setjmp call ended before it resumes");
    const JumpEnvironment& environment = jump.environment;
    while (frame.dynamic.size() > environment.dynamicCount) {
        const DynamicObject object = frame.dynamic.back();
        frame.dynamic.pop_back();
        memory_.popStack(object.address);
        if (object.isArray) {
            --frame.arrayCount;
        }
    }
    location_ = environment.location;
    resumption_ = Resumption{.call = environment.call, .value = std::move(jump.value)};
    return environment.pc;
}

void Execution::forgetEnvironments(const Frame& frame) {
    while (!jumpEnvironments_.empty() && jumpEnvironments_.back().frameSerial == frame.serial) {
        jumpEnvironments_.pop_back();
    }
}

} // namespace nitwit::interp
