#include "validate/constraints.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <optional>

#include "ast/casting.hpp"
#include "parse/constraint.hpp"
#include "sema/constant.hpp"
#include "sema/expression.hpp"
#include "sema/types.hpp"
#include "validate/values.hpp"

namespace nitwit::validate {

namespace {

std::optional<value::Comparison> comparisonOf(ast::BinaryOperator op) {
    switch (op) {
    case ast::BinaryOperator::Equal:
        return value::Comparison::Equal;
    case ast::BinaryOperator::NotEqual:
        return value::Comparison::NotEqual;
    case ast::BinaryOperator::Less:
        return value::Comparison::Less;
    case ast::BinaryOperator::LessEqual:
        return value::Comparison::LessEqual;
    case ast::BinaryOperator::Greater:
        return value::Comparison::Greater;
    case ast::BinaryOperator::GreaterEqual:
        return value::Comparison::GreaterEqual;
    default:
        return std::nullopt;
    }
}

bool isAssignment(ast::BinaryOperator op) {
    return op >= ast::BinaryOperator::Assign && op <= ast::BinaryOperator::OrAssign;
}

// Whether the expression's value is the same wherever it is evaluated: literals and enumeration constants combined by
// operators and casts. Constraints contain no side effects (parse::parseConstraint rejects them).
bool isConstant(const ast::Expr* expr) {
    switch (expr->kind()) {
    case ast::ExprKind::IntegerLiteral:
    case ast::ExprKind::FloatingLiteral:
    case ast::ExprKind::CharacterLiteral:
    case ast::ExprKind::SizeOfAlignOf:
    case ast::ExprKind::TypesCompatible:
        return true;
    case ast::ExprKind::DeclRef:
        return ast::isa<ast::EnumConstantDecl>(ast::cast<ast::DeclRefExpr>(expr)->decl());
    case ast::ExprKind::Paren:
        return isConstant(ast::cast<ast::ParenExpr>(expr)->inner());
    case ast::ExprKind::Unary: {
        const auto* unary = ast::cast<ast::UnaryExpr>(expr);
        const ast::UnaryOperator op = unary->op();
        return (op == ast::UnaryOperator::Plus || op == ast::UnaryOperator::Minus
                || op == ast::UnaryOperator::BitwiseNot || op == ast::UnaryOperator::LogicalNot)
               && isConstant(unary->operand());
    }
    case ast::ExprKind::Binary: {
        const auto* binary = ast::cast<ast::BinaryExpr>(expr);
        return !isAssignment(binary->op()) && binary->op() != ast::BinaryOperator::Comma && isConstant(binary->lhs())
               && isConstant(binary->rhs());
    }
    case ast::ExprKind::Cast:
        return isConstant(ast::cast<ast::CastExpr>(expr)->operand());
    case ast::ExprKind::ImplicitCast:
        return isConstant(ast::cast<ast::ImplicitCastExpr>(expr)->operand());
    default:
        return false;
    }
}

// The operand of a comparison without the conversions sema inserted to bring it to the comparison's type: integer
// promotions and the usual arithmetic conversions (C17 6.3.1.1, 6.3.1.8). Integer promotions and conversions between
// floating types widen; a conversion between integer types may change the signedness, and one from an integer to a
// floating type (`i < 0.5`) may round. Only the last conversion of the chain can change the value, so converting the
// operand's value directly to the comparison's type gives the same value, which values.hpp relies on.
const ast::Expr* withoutArithmeticConversions(const ast::Expr* expr) {
    while (true) {
        expr = ast::ignoreParens(expr);
        const auto* cast = ast::dyn_cast<ast::ImplicitCastExpr>(expr);
        if (cast == nullptr
            || (cast->castKind() != ast::CastKind::IntegralCast && cast->castKind() != ast::CastKind::IntegralToFloating
                && cast->castKind() != ast::CastKind::FloatingCast)) {
            return expr;
        }
        expr = cast->operand();
    }
}

Conjunct conjunctOf(const ast::Expr* expr) {
    Conjunct conjunct;
    conjunct.expr = expr;
    const auto* binary = ast::dyn_cast<ast::BinaryExpr>(ast::ignoreParens(expr));
    const std::optional<value::Comparison> op = binary != nullptr ? comparisonOf(binary->op()) : std::nullopt;
    if (!op) {
        return conjunct;
    }
    const bool isLhsConstant = isConstant(binary->lhs());
    const bool isRhsConstant = isConstant(binary->rhs());
    if (isRhsConstant == isLhsConstant) {
        conjunct.isResultNan = *op == value::Comparison::NotEqual
                               && ast::isa<ast::ResultExpr>(withoutArithmeticConversions(binary->lhs()))
                               && ast::isa<ast::ResultExpr>(withoutArithmeticConversions(binary->rhs()));
        return conjunct;
    }
    const ast::Expr* subject = isRhsConstant ? binary->lhs() : binary->rhs();
    conjunct.subject = withoutArithmeticConversions(subject);
    conjunct.constant = isRhsConstant ? binary->rhs() : binary->lhs();
    conjunct.op = isRhsConstant ? *op : mirrored(*op);
    conjunct.type = subject->type();
    return conjunct;
}

// The operands of `&&` are scalar rvalues after sema, so each conjunct can be evaluated on its own. Nesting is bounded
// by the constraint parser's limit.
void split(const ast::Expr* expr, std::vector<Conjunct>& conjuncts) {
    const auto* binary = ast::dyn_cast<ast::BinaryExpr>(ast::ignoreParens(expr));
    if (binary != nullptr && binary->op() == ast::BinaryOperator::LogicalAnd) {
        split(binary->lhs(), conjuncts);
        split(binary->rhs(), conjuncts);
        return;
    }
    conjuncts.push_back(conjunctOf(expr));
}

std::string normalized(std::string_view text) {
    const auto isSpace = [](char c) {
        return std::isspace(static_cast<unsigned char>(c)) != 0;
    };
    while (!text.empty() && isSpace(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && isSpace(text.back())) {
        text.remove_suffix(1);
    }
    std::string lower(text);
    std::ranges::transform(lower, lower.begin(), [](char c) { return static_cast<char>(std::tolower(c)); });
    return lower;
}

} // namespace

ConstraintCompiler::ConstraintCompiler(const ProgramInput& program, DiagnosticEngine& diagnostics)
    : program_(program), diagnostics_(diagnostics) {}

std::expected<const CompiledConstraint*, std::string>
ConstraintCompiler::compile(WaypointId waypoint, std::string_view text, const ast::FunctionDecl* function,
                            SourceLocation location, ast::QualType resultType) {
    const Key key{waypoint, location.offset};
    if (const auto found = constraints_.find(key); found != constraints_.end()) {
        return &found->second;
    }
    const std::size_t first = diagnostics_.diagnostics().size();
    const ast::Expr* analyzed = analyze(text, function, location, resultType);
    if (analyzed == nullptr) {
        return std::unexpected(errorSince(first));
    }
    CompiledConstraint& compiled = constraints_[key];
    split(analyzed, compiled.conjuncts);
    return &compiled;
}

std::expected<BranchExpectation, std::string> ConstraintCompiler::branch(WaypointId waypoint, std::string_view text,
                                                                         const ast::FunctionDecl* function,
                                                                         SourceLocation location) {
    const std::string value = normalized(text);
    if (value == "true") {
        return BranchExpectation{.kind = BranchExpectation::Kind::True, .caseValue = 0};
    }
    if (value == "false") {
        return BranchExpectation{.kind = BranchExpectation::Kind::False, .caseValue = 0};
    }
    if (value == "default") {
        return BranchExpectation{.kind = BranchExpectation::Kind::Default, .caseValue = 0};
    }
    const Key key{waypoint, location.offset};
    if (const auto found = branches_.find(key); found != branches_.end()) {
        return found->second;
    }
    const std::size_t first = diagnostics_.diagnostics().size();
    const ast::Expr* analyzed = analyze(text, function, location, ast::QualType());
    if (analyzed == nullptr) {
        return std::unexpected(errorSince(first));
    }
    const std::optional<Int128> caseValue = sema::isInteger(program_.context, analyzed->type())
                                                ? sema::evaluateIntegerConstant(program_.context, analyzed)
                                                : std::nullopt;
    if (!caseValue) {
        return std::unexpected(std::format("the branching constraint '{}' is neither true, false, default nor an "
                                           "integer constant",
                                           text));
    }
    return branches_[key] = BranchExpectation{.kind = BranchExpectation::Kind::Case, .caseValue = *caseValue};
}

ast::Expr* ConstraintCompiler::analyze(std::string_view text, const ast::FunctionDecl* function,
                                       SourceLocation location, ast::QualType resultType) {
    const parse::ConstraintScope scope{.unit = &program_.unit, .function = function, .location = location};
    ast::Expr* parsed = parse::parseConstraint(text, scope, program_.context, program_.sources, diagnostics_);
    if (parsed == nullptr) {
        return nullptr;
    }
    return sema::analyzeConstraint(parsed, resultType, program_.context, diagnostics_);
}

std::string ConstraintCompiler::errorSince(std::size_t first) const {
    for (const Diagnostic& diagnostic : diagnostics_.diagnostics().subspan(first)) {
        if (diagnostic.severity == Severity::Error) {
            return diagnostics_.format(diagnostic);
        }
    }
    return "the constraint cannot be compiled";
}

} // namespace nitwit::validate
