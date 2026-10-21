// Function bodies and statements (C17 6.8, 6.9.1): conditions, switch and case values, return values, and the locals
// of each function.

#include <algorithm>
#include <format>
#include <vector>

#include "ast/casting.hpp"
#include "sema/sema_impl.hpp"
#include "sema/types.hpp"
#include "support/assert.hpp"
#include "value/value.hpp"

namespace nitwit::sema::detail {

// C17 6.9.1: the parameters are the first locals, followed by every automatic variable of the body in declaration
// order (varDecl() appends them).
void Sema::functionBody(ast::FunctionDecl* function) {
    const ast::FunctionType* type = functionOf(function->type());
    NITWIT_ASSERT(type != nullptr, "function definition without a function type");
    const ast::QualType returnType = type->returnType();
    if (classify(context_, returnType) == TypeClass::Record) {
        requireComplete(returnType, function->location(),
                        std::format("return type is an incomplete type '{}'", typeName(returnType)));
    }
    function_.emplace(FunctionContext{.function = function, .returnType = returnType, .locals = {}});
    for (ast::VarDecl* parameter : function->parameters()) {
        requireComplete(parameter->type(), parameter->location(),
                        std::format("parameter '{}' has incomplete type", parameter->name().text()));
        parameter->setLocalIndex(static_cast<std::uint32_t>(function_->locals.size()));
        function_->locals.push_back(parameter);
    }
    stmt(function->body());
    function->setLocals(context_.copy<ast::VarDecl*>(function_->locals));
    function_.reset();
}

void Sema::stmt(ast::Stmt* stmt) {
    // Chains of labels (`case 1: case 2: ...`) and of `else if` are followed in loops, since generated programs have
    // long ones.
    while (true) {
        switch (stmt->kind()) {
        case ast::StmtKind::Case:
            stmt = ast::cast<ast::CaseStmt>(stmt)->subStmt(); // the values are checked by switchStmt()
            continue;
        case ast::StmtKind::Default:
            stmt = ast::cast<ast::DefaultStmt>(stmt)->subStmt();
            continue;
        case ast::StmtKind::Label:
            stmt = ast::cast<ast::LabelStmt>(stmt)->subStmt();
            continue;
        case ast::StmtKind::If: {
            auto* ifStmt = ast::cast<ast::IfStmt>(stmt);
            ifStmt->setCondition(condition(ifStmt->condition(), "'if' condition"));
            this->stmt(ifStmt->thenStmt());
            if (ifStmt->elseStmt() == nullptr) {
                return;
            }
            stmt = ifStmt->elseStmt();
            continue;
        }
        case ast::StmtKind::Compound:
            for (ast::Stmt* child : ast::cast<ast::CompoundStmt>(stmt)->body()) {
                this->stmt(child);
            }
            return;
        case ast::StmtKind::Expression:
            return expr(ast::cast<ast::ExpressionStmt>(stmt)->expr());
        case ast::StmtKind::Declaration:
            for (ast::Decl* decl : ast::cast<ast::DeclarationStmt>(stmt)->decls()) {
                declaration(decl);
            }
            return;
        case ast::StmtKind::While: {
            auto* whileStmt = ast::cast<ast::WhileStmt>(stmt);
            whileStmt->setCondition(condition(whileStmt->condition(), "'while' condition"));
            stmt = whileStmt->body();
            continue;
        }
        case ast::StmtKind::DoWhile: {
            auto* doStmt = ast::cast<ast::DoWhileStmt>(stmt);
            this->stmt(doStmt->body());
            doStmt->setCondition(condition(doStmt->condition(), "'do' condition"));
            return;
        }
        case ast::StmtKind::For: {
            auto* forStmt = ast::cast<ast::ForStmt>(stmt);
            if (forStmt->init() != nullptr) {
                this->stmt(forStmt->init());
            }
            if (forStmt->condition() != nullptr) {
                forStmt->setCondition(condition(forStmt->condition(), "'for' condition"));
            }
            if (forStmt->increment() != nullptr) {
                expr(forStmt->increment());
            }
            stmt = forStmt->body();
            continue;
        }
        case ast::StmtKind::Switch:
            return switchStmt(ast::cast<ast::SwitchStmt>(stmt));
        case ast::StmtKind::Return:
            return returnStmt(ast::cast<ast::ReturnStmt>(stmt));
        case ast::StmtKind::Asm:
            return expr(ast::cast<ast::AsmStmt>(stmt)->asmTemplate());
        case ast::StmtKind::Break:
        case ast::StmtKind::Continue:
        case ast::StmtKind::Goto:
        case ast::StmtKind::Null:
            return;
        }
        NITWIT_UNREACHABLE("unknown statement kind");
    }
}

// C17 6.8.4.2: the controlling expression is promoted; each case value is converted to the promoted type, and no two
// case values (GNU: ranges) may overlap.
void Sema::switchStmt(ast::SwitchStmt* switchStmt) {
    ast::Expr* controlling = switchStmt->condition();
    expr(controlling);
    if (!isIntegerClass(classify(context_, valueType(controlling)))) {
        error(controlling->location(), "switch quantity not an integer");
    }
    ast::Expr* promoted = promote(controlling);
    switchStmt->setCondition(promoted);
    const value::IntegerFormat format = integerFormat(context_, promoted->type());
    const auto convert = [&](ast::Expr* value) {
        expr(value);
        if (!isIntegerClass(classify(context_, value->type()))) {
            error(value->location(), "case label does not reduce to an integer constant");
        }
        const Int128 constant = integerConstant(value, "case label");
        return value::Value::integer(static_cast<UInt128>(constant), format).signedBits();
    };
    struct Range {
        Int128 low;
        Int128 high;
        const ast::CaseStmt* label;
    };
    std::vector<Range> ranges;
    for (ast::CaseStmt* label : switchStmt->cases()) {
        const Int128 low = convert(label->value());
        const Int128 high = label->rangeEnd() != nullptr ? convert(label->rangeEnd()) : low;
        label->setValues(low, high);
        if (high < low) {
            warning(label->location(), "empty range specified");
            continue;
        }
        ranges.push_back({low, high, label});
    }
    std::ranges::sort(ranges, {}, &Range::low);
    for (std::size_t i = 1; i < ranges.size(); ++i) {
        if (ranges[i].low <= ranges[i - 1].high) {
            const auto later = std::max(ranges[i].label, ranges[i - 1].label, [](const auto* a, const auto* b) {
                return a->location().offset < b->location().offset;
            });
            error(later->location(), "duplicate case value");
        }
    }
    stmt(switchStmt->body());
}

// C17 6.8.6.4: the value is converted as if by assignment to the return type. GCC 14 rejects a value in a void
// function and a missing value in a non-void one unless -fpermissive; older sv-benchmarks rely on both.
void Sema::returnStmt(ast::ReturnStmt* returnStmt) {
    NITWIT_ASSERT(function_.has_value(), "return outside of a function");
    const ast::QualType returnType = function_->returnType;
    const bool returnsVoid = classify(context_, returnType) == TypeClass::Void;
    ast::Expr* value = returnStmt->value();
    if (value == nullptr) {
        if (!returnsVoid) {
            warning(returnStmt->location(), "'return' with no value, in function returning non-void");
        }
        return;
    }
    expr(value);
    if (returnsVoid) {
        if (classify(context_, value->type()) != TypeClass::Void) {
            warning(returnStmt->location(), "'return' with a value, in function returning void");
            returnStmt->setValue(implicitCast(value, context_.builtin(ast::BuiltinKind::Void), ast::CastKind::ToVoid));
        }
        return;
    }
    const AssignmentContext context{.kind = AssignmentContext::Kind::Return,
                                    .location = value->location(),
                                    .argumentIndex = 0,
                                    .function = nullptr};
    returnStmt->setValue(convertForAssignment(value, returnType, context));
}

} // namespace nitwit::sema::detail
