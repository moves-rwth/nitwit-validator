// Rvalues: literals, operators (C17 6.5), conditional and statement expressions. Every conversion is an explicit cast
// node of the typed AST (conversions.cpp); the operands of an operator already have the types it computes in.

#include <format>
#include <utility>

#include "ast/casting.hpp"
#include "interp/execution.hpp"
#include "sema/constant.hpp"
#include "sema/types.hpp"
#include "support/assert.hpp"

namespace nitwit::interp {

namespace {

using ast::BinaryOperator;
using ast::UnaryOperator;

value::Comparison comparisonOf(BinaryOperator op) {
    switch (op) {
    case BinaryOperator::Less:
        return value::Comparison::Less;
    case BinaryOperator::Greater:
        return value::Comparison::Greater;
    case BinaryOperator::LessEqual:
        return value::Comparison::LessEqual;
    case BinaryOperator::GreaterEqual:
        return value::Comparison::GreaterEqual;
    case BinaryOperator::Equal:
        return value::Comparison::Equal;
    case BinaryOperator::NotEqual:
        return value::Comparison::NotEqual;
    default:
        NITWIT_UNREACHABLE("not a comparison");
    }
}

/// The operator of a compound assignment, e.g. Add for `+=`.
BinaryOperator operatorOf(BinaryOperator compound) {
    switch (compound) {
    case BinaryOperator::MulAssign:
        return BinaryOperator::Mul;
    case BinaryOperator::DivAssign:
        return BinaryOperator::Div;
    case BinaryOperator::RemAssign:
        return BinaryOperator::Rem;
    case BinaryOperator::AddAssign:
        return BinaryOperator::Add;
    case BinaryOperator::SubAssign:
        return BinaryOperator::Sub;
    case BinaryOperator::ShlAssign:
        return BinaryOperator::Shl;
    case BinaryOperator::ShrAssign:
        return BinaryOperator::Shr;
    case BinaryOperator::AndAssign:
        return BinaryOperator::BitwiseAnd;
    case BinaryOperator::XorAssign:
        return BinaryOperator::BitwiseXor;
    case BinaryOperator::OrAssign:
        return BinaryOperator::BitwiseOr;
    default:
        NITWIT_UNREACHABLE("not a compound assignment");
    }
}

value::IntegerOperator integerOperatorOf(BinaryOperator op) {
    switch (op) {
    case BinaryOperator::Mul:
        return value::IntegerOperator::Mul;
    case BinaryOperator::Div:
        return value::IntegerOperator::Div;
    case BinaryOperator::Rem:
        return value::IntegerOperator::Rem;
    case BinaryOperator::Add:
        return value::IntegerOperator::Add;
    case BinaryOperator::Sub:
        return value::IntegerOperator::Sub;
    case BinaryOperator::BitwiseAnd:
        return value::IntegerOperator::And;
    case BinaryOperator::BitwiseXor:
        return value::IntegerOperator::Xor;
    case BinaryOperator::BitwiseOr:
        return value::IntegerOperator::Or;
    default:
        NITWIT_UNREACHABLE("not an integer operator");
    }
}

value::FloatOperator floatOperatorOf(BinaryOperator op) {
    switch (op) {
    case BinaryOperator::Mul:
        return value::FloatOperator::Mul;
    case BinaryOperator::Div:
        return value::FloatOperator::Div;
    case BinaryOperator::Add:
        return value::FloatOperator::Add;
    case BinaryOperator::Sub:
        return value::FloatOperator::Sub;
    default:
        NITWIT_UNREACHABLE("not a floating operator");
    }
}

bool isCompoundAssignment(BinaryOperator op) {
    return op >= BinaryOperator::MulAssign && op <= BinaryOperator::OrAssign;
}

value::Value one(const TypeInfo& type) {
    switch (type.floating) {
    case value::FloatFormat::Float:
        return value::Value::ofFloat(1.0F);
    case value::FloatFormat::Double:
        return value::Value::ofDouble(1.0);
    case value::FloatFormat::LongDouble:
        return value::Value::ofLongDouble(1.0L);
    }
    NITWIT_UNREACHABLE("unknown floating format");
}

} // namespace

value::Value Execution::rvalue(const ast::Expr* expr) {
    checkHostStack(expr); // deep expressions recurse here
    switch (expr->kind()) {
    case ast::ExprKind::IntegerLiteral:
    case ast::ExprKind::FloatingLiteral:
    case ast::ExprKind::CharacterLiteral:
        return literal(expr);
    case ast::ExprKind::DeclRef: {
        const auto* reference = ast::cast<ast::DeclRefExpr>(expr);
        if (const auto* constant = ast::dyn_cast<ast::EnumConstantDecl>(reference->decl())) {
            return value::Value::integer(static_cast<UInt128>(constant->value()),
                                         types_.describe(expr->type()).integer);
        }
        break;
    }
    case ast::ExprKind::Paren:
        return rvalue(ast::cast<ast::ParenExpr>(expr)->inner());
    case ast::ExprKind::Unary:
        return unary(ast::cast<ast::UnaryExpr>(expr));
    case ast::ExprKind::Binary:
        return binary(ast::cast<ast::BinaryExpr>(expr));
    case ast::ExprKind::Conditional:
        return conditional(ast::cast<ast::ConditionalExpr>(expr), true);
    case ast::ExprKind::Call:
        return call(ast::cast<ast::CallExpr>(expr), true);
    case ast::ExprKind::Member:
        // Also a member of a struct rvalue, such as `f().x` (C17 6.5.2.3p3): member() finds its object.
        return load(member(ast::cast<ast::MemberExpr>(expr)), expr->type(), expr);
    case ast::ExprKind::Cast: {
        const auto* castExpr = ast::cast<ast::CastExpr>(expr);
        return castValue(castExpr->castKind(), castExpr->operand(), expr->type(), expr);
    }
    case ast::ExprKind::ImplicitCast: {
        const auto* castExpr = ast::cast<ast::ImplicitCastExpr>(expr);
        return castValue(castExpr->castKind(), castExpr->operand(), expr->type(), expr);
    }
    case ast::ExprKind::ImplicitZero:
        return zeroOf(types_.describe(expr->type()));
    case ast::ExprKind::SizeOfAlignOf:
        return sizeOrAlignment(ast::cast<ast::SizeOfAlignOfExpr>(expr));
    case ast::ExprKind::OffsetOf:
        return offsetOf(ast::cast<ast::OffsetOfExpr>(expr));
    case ast::ExprKind::TypesCompatible: {
        const std::optional<value::Value> result = constant(expr);
        NITWIT_ASSERT(result.has_value(), "__builtin_types_compatible_p is a constant");
        return *result;
    }
    case ast::ExprKind::VaArg:
        return vaArg(ast::cast<ast::VaArgExpr>(expr));
    case ast::ExprKind::StatementExpr:
        return statementExpression(ast::cast<ast::StatementExpr>(expr));
    case ast::ExprKind::GenericSelection:
        return rvalue(ast::cast<ast::GenericSelectionExpr>(expr)->selected());
    case ast::ExprKind::ChooseExpr:
        return rvalue(ast::cast<ast::ChooseExpr>(expr)->selected());
    case ast::ExprKind::Result:
        if (returnValue_ == nullptr) {
            stop(Termination::Unsupported, "\\result outside the return of a call", expr->location());
        }
        return *returnValue_;
    case ast::ExprKind::StringLiteral:
    case ast::ExprKind::ArraySubscript:
    case ast::ExprKind::CompoundLiteral:
    case ast::ExprKind::InitList:
    case ast::ExprKind::DesignatedInit:
        break;
    }
    if (expr->isLValue()) {
        return load(lvalue(expr), expr->type(), expr);
    }
    stop(Termination::Unsupported, "an expression the interpreter cannot evaluate here", expr->location());
}

// C17 6.8.3p2: an expression statement is evaluated for its side effects. Lvalues are not read (sema leaves them
// unconverted), so `(void)x;` does not read an uninitialized x.
void Execution::effect(const ast::Expr* expr) {
    switch (expr->kind()) {
    case ast::ExprKind::Paren:
        return effect(ast::cast<ast::ParenExpr>(expr)->inner());
    case ast::ExprKind::Call:
        (void)call(ast::cast<ast::CallExpr>(expr), false);
        return;
    case ast::ExprKind::Binary: {
        const auto* binaryExpr = ast::cast<ast::BinaryExpr>(expr);
        if (binaryExpr->op() == BinaryOperator::Assign) {
            (void)assignment(binaryExpr, false);
            return;
        }
        if (binaryExpr->op() == BinaryOperator::Comma) {
            effect(binaryExpr->lhs());
            return effect(binaryExpr->rhs());
        }
        break;
    }
    case ast::ExprKind::Conditional:
        (void)conditional(ast::cast<ast::ConditionalExpr>(expr), false);
        return;
    case ast::ExprKind::Cast:
        if (const auto* castExpr = ast::cast<ast::CastExpr>(expr); castExpr->castKind() == ast::CastKind::ToVoid) {
            return effect(castExpr->operand());
        }
        break;
    case ast::ExprKind::ImplicitCast:
        if (const auto* castExpr = ast::cast<ast::ImplicitCastExpr>(expr);
            castExpr->castKind() == ast::CastKind::ToVoid) {
            return effect(castExpr->operand());
        }
        break;
    default:
        break;
    }
    if (expr->isLValue()) {
        (void)lvalue(expr);
    } else {
        (void)rvalue(expr);
    }
}

bool Execution::test(const ast::Expr* condition) {
    const value::Value value = rvalue(condition);
    switch (value.kind()) {
    case value::Value::Kind::Integer:
        return value.bits() != 0;
    case value::Value::Kind::Float:
    case value::Value::Kind::Double:
    case value::Value::Kind::LongDouble:
        return value::toBool(value).bits() != 0;
    case value::Value::Kind::None:
    case value::Value::Kind::Bytes:
        break;
    }
    stop(Termination::Unsupported, "a condition that is not a scalar", condition->location());
}

value::Value Execution::checked(value::Result result, const ast::Expr* where) {
    switch (result.undefinedBehavior) {
    case value::UndefinedBehavior::None:
        break;
    case value::UndefinedBehavior::DivisionByZero:
        undefined("division by zero", where);
    case value::UndefinedBehavior::SignedOverflow:
    case value::UndefinedBehavior::ShiftCountOutOfRange:
    case value::UndefinedBehavior::ShiftOfNegative:
    case value::UndefinedBehavior::ShiftOverflow:
    case value::UndefinedBehavior::FloatToIntegerOverflow:
        reportUndefined(result.undefinedBehavior, where);
        break;
    }
    return std::move(result.value);
}

value::Value Execution::literal(const ast::Expr* expr) {
    const TypeInfo type = types_.describe(expr->type());
    if (const auto* integer = ast::dyn_cast<ast::IntegerLiteralExpr>(expr)) {
        return value::Value::integer(integer->value().value, type.integer);
    }
    if (const auto* character = ast::dyn_cast<ast::CharacterLiteralExpr>(expr)) {
        return value::Value::integer(static_cast<UInt128>(static_cast<Int128>(character->value().value)), type.integer);
    }
    // The lexer rounded the constant once, to the precision its suffix selects (lex/literals.hpp).
    const lex::FloatingLiteral& floating = ast::cast<ast::FloatingLiteralExpr>(expr)->value();
    value::Value decoded;
    value::FloatFormat format = value::FloatFormat::Double;
    switch (floating.suffix) {
    case lex::FloatingSuffix::F:
    case lex::FloatingSuffix::F32:
        decoded = value::Value::ofFloat(floating.floatValue);
        format = value::FloatFormat::Float;
        break;
    case lex::FloatingSuffix::None:
    case lex::FloatingSuffix::F64:
    case lex::FloatingSuffix::F32x:
        decoded = value::Value::ofDouble(floating.doubleValue);
        break;
    case lex::FloatingSuffix::L:
    case lex::FloatingSuffix::F64x:
    case lex::FloatingSuffix::F128:
        decoded = value::Value::ofLongDouble(floating.longDoubleValue);
        format = value::FloatFormat::LongDouble;
        break;
    }
    return format == type.floating ? decoded : value::convertFloat(decoded, format, type.floating);
}

value::Value Execution::unary(const ast::UnaryExpr* expr) {
    const ast::Expr* operand = expr->operand();
    switch (expr->op()) {
    case UnaryOperator::PostIncrement:
    case UnaryOperator::PostDecrement:
    case UnaryOperator::PreIncrement:
    case UnaryOperator::PreDecrement:
        return increment(expr);
    case UnaryOperator::AddressOf:
        // C17 6.5.3.2p3: &*p and &a[i] compute the address without accessing the object.
        return value::Value::integer(lvalue(operand).address, types_.pointerFormat());
    case UnaryOperator::Dereference:
        return load(lvalue(expr), expr->type(), expr);
    case UnaryOperator::Plus:
    case UnaryOperator::Extension:
        return rvalue(operand);
    case UnaryOperator::Minus: {
        const value::Value value = rvalue(operand);
        const TypeInfo type = types_.describe(expr->type());
        if (type.representation == Representation::Floating) {
            return value::floatNegate(value, type.floating);
        }
        return checked(value::integerNegate(value, type.integer), expr);
    }
    case UnaryOperator::BitwiseNot:
        return value::integerComplement(rvalue(operand), types_.describe(expr->type()).integer);
    case UnaryOperator::LogicalNot:
        // C17 6.5.3.3p5: !E is 0 == E.
        return value::Value::integer(test(operand) ? 0 : 1, types_.intFormat());
    case UnaryOperator::Real:
    case UnaryOperator::Imag:
        break;
    }
    stop(Termination::Unsupported, "complex arithmetic is not supported", expr->location());
}

value::Value Execution::binary(const ast::BinaryExpr* expr) {
    const BinaryOperator op = expr->op();
    switch (op) {
    case BinaryOperator::Comma:
        // C17 6.5.17p2: the left operand is a void expression.
        effect(expr->lhs());
        return rvalue(expr->rhs());
    case BinaryOperator::LogicalAnd:
    case BinaryOperator::LogicalOr:
        return logical(expr);
    case BinaryOperator::Assign:
        return assignment(expr, true);
    case BinaryOperator::Less:
    case BinaryOperator::Greater:
    case BinaryOperator::LessEqual:
    case BinaryOperator::GreaterEqual:
    case BinaryOperator::Equal:
    case BinaryOperator::NotEqual:
        return comparison(op, expr);
    default:
        break;
    }
    if (isCompoundAssignment(op)) {
        return compoundAssignment(expr);
    }
    // GCC evaluates the operands of arithmetic operators from left to right.
    const value::Value lhs = rvalue(expr->lhs());
    const value::Value rhs = rvalue(expr->rhs());
    return arithmetic(op, lhs, rhs, expr->lhs()->type(), expr->rhs()->type(), expr->type(), expr);
}

value::Value Execution::arithmetic(BinaryOperator op, const value::Value& lhs, const value::Value& rhs,
                                   ast::QualType lhsType, ast::QualType rhsType, ast::QualType resultType,
                                   const ast::Expr* where) {
    const TypeInfo left = types_.describe(lhsType);
    if (op == BinaryOperator::Add || op == BinaryOperator::Sub) {
        const TypeInfo right = types_.describe(rhsType);
        if (left.representation == Representation::Pointer && right.representation == Representation::Pointer) {
            // C17 6.5.6p9: the difference of the subscripts; the pointers must point into the same array.
            const std::uint64_t size = sizeOf(TypeTable::pointee(lhsType), where);
            if (size == 0) {
                undefined("subtraction of pointers to objects of size 0", where);
            }
            const value::IntegerFormat difference = types_.ptrdiffFormat();
            const Int128 bytes = value::Value::integer(lhs.bits() - rhs.bits(), difference).signedBits();
            return value::Value::integer(static_cast<UInt128>(bytes / static_cast<Int128>(size)),
                                         types_.describe(resultType).integer);
        }
        if (left.representation == Representation::Pointer) {
            return pointerOffset(lhs, rhs, lhsType, op == BinaryOperator::Sub, where);
        }
        if (right.representation == Representation::Pointer) {
            return pointerOffset(rhs, lhs, rhsType, false, where);
        }
    }
    const TypeInfo result = types_.describe(resultType);
    if (op == BinaryOperator::Shl || op == BinaryOperator::Shr) {
        return checked(
            value::shift(op == BinaryOperator::Shl ? value::ShiftOperator::Left : value::ShiftOperator::Right, lhs,
                         left.integer, rhs, types_.describe(rhsType).integer),
            where);
    }
    if (result.representation == Representation::Floating) {
        return value::floatBinary(floatOperatorOf(op), lhs, rhs, result.floating);
    }
    return checked(value::integerBinary(integerOperatorOf(op), lhs, rhs, result.integer), where);
}

// C17 6.5.6p8: pointer plus integer moves by whole elements. The index is canonical, i.e. sign-extended if its type
// is signed. The address wraps around like the hardware's; only an access through an invalid pointer is an error.
value::Value Execution::pointerOffset(const value::Value& pointer, const value::Value& index, ast::QualType pointerType,
                                      bool subtract, const ast::Expr* where) {
    // Modulo 2^128, which the truncation to the pointer width keeps correct for negative indexes.
    const UInt128 offset = index.bits() * UInt128{sizeOf(TypeTable::pointee(pointerType), where)};
    const UInt128 address = subtract ? pointer.bits() - offset : pointer.bits() + offset;
    return value::Value::integer(address, types_.pointerFormat());
}

value::Value Execution::comparison(BinaryOperator op, const ast::BinaryExpr* expr) {
    const value::Value lhs = rvalue(expr->lhs());
    const value::Value rhs = rvalue(expr->rhs());
    const TypeInfo type = types_.describe(expr->lhs()->type());
    const bool holds = type.representation == Representation::Floating
                           ? value::floatCompare(comparisonOf(op), lhs, rhs, type.floating)
                           : value::integerCompare(comparisonOf(op), lhs, rhs, type.integer);
    return value::Value::integer(holds ? 1 : 0, types_.intFormat());
}

// C17 6.5.13-14: the right operand is evaluated only if the left one does not decide the result.
value::Value Execution::logical(const ast::BinaryExpr* expr) {
    const bool isAnd = expr->op() == BinaryOperator::LogicalAnd;
    bool holds = test(expr->lhs());
    if (holds == isAnd) {
        holds = test(expr->rhs());
    }
    return value::Value::integer(holds ? 1 : 0, types_.intFormat());
}

// C17 6.5.16.1: the right operand, converted by sema, replaces the value of the object; the result is the value the
// object then holds. GCC computes the address of the left operand first.
// The order of the operands of `=` (unspecified, C17 6.5.16p3) as GCC on x86 at -O0: the right operand first, unless
// it is a call, also under parentheses and conversions that keep the representation; then the address of the left
// operand comes first. (`arr[i()] = f()` calls i first, `arr[i()] = f() * 2` calls f first.)
bool Execution::isLeftOperandFirst(const ast::BinaryExpr* expr) const {
    const ast::Expr* rhs = ast::ignoreParens(expr->rhs());
    while (true) {
        const ast::Expr* operand = nullptr;
        ast::CastKind kind = ast::CastKind::NoOp;
        if (const auto* implicit = ast::dyn_cast<ast::ImplicitCastExpr>(rhs)) {
            operand = implicit->operand();
            kind = implicit->castKind();
        } else if (const auto* written = ast::dyn_cast<ast::CastExpr>(rhs)) {
            operand = written->operand();
            kind = written->castKind();
        } else {
            break;
        }
        const TypeInfo from = types_.describe(operand->type());
        const TypeInfo to = types_.describe(rhs->type());
        const bool keepsRepresentation =
            kind == ast::CastKind::NoOp || kind == ast::CastKind::BitCast
            || ((kind == ast::CastKind::IntegralCast || kind == ast::CastKind::IntegralToPointer
                 || kind == ast::CastKind::PointerToIntegral)
                && from.integer.bits == to.integer.bits);
        if (!keepsRepresentation) {
            break;
        }
        rhs = ast::ignoreParens(operand);
    }
    return ast::isa<ast::CallExpr>(rhs);
}

value::Value Execution::assignment(const ast::BinaryExpr* expr, bool isValueUsed) {
    if (isEvaluatingConstraint_) {
        stop(Termination::Unsupported, "a constraint must not assign", expr->location());
    }
    const ast::Expr* target = expr->lhs();
    if (types_.describe(target->type()).isBytes()) {
        const Address object = assignRecord(expr);
        return isValueUsed ? load({.address = object}, target->type(), target) : value::Value();
    }
    if (isLeftOperandFirst(expr)) {
        const LValue object = lvalue(target);
        return store(object, target->type(), rvalue(expr->rhs()), expr);
    }
    const value::Value stored = rvalue(expr->rhs());
    return store(lvalue(target), target->type(), stored, expr);
}

Address Execution::assignRecord(const ast::BinaryExpr* expr) {
    if (isEvaluatingConstraint_) {
        stop(Termination::Unsupported, "a constraint must not assign", expr->location());
    }
    const std::uint64_t size = types_.describe(expr->lhs()->type()).size;
    Address object = 0;
    Address source = 0;
    if (isLeftOperandFirst(expr)) {
        object = lvalue(expr->lhs()).address;
        source = recordObject(expr->rhs());
    } else {
        source = recordObject(expr->rhs());
        object = lvalue(expr->lhs()).address;
    }
    // C17 6.5.16.1p3: the objects must not overlap, unless they are the same.
    if (source != object && source < object + size && object < source + size) {
        undefined("an assignment between overlapping objects", expr);
    }
    copyObject(object, source, size, expr);
    return object;
}

// C17 6.5.16.2p3: E1 op= E2 is E1 = E1 op E2 with E1 evaluated once, computed in the computation type. GCC evaluates
// the right operand first.
value::Value Execution::compoundAssignment(const ast::BinaryExpr* expr) {
    if (isEvaluatingConstraint_) {
        stop(Termination::Unsupported, "a constraint must not assign", expr->location());
    }
    const ast::Expr* target = expr->lhs();
    const BinaryOperator op = operatorOf(expr->op());
    const value::Value rhs = rvalue(expr->rhs());
    const LValue object = lvalue(target);
    const value::Value old = load(object, target->type(), target);
    const TypeInfo type = types_.describe(target->type());
    value::Value result;
    if (type.representation == Representation::Pointer) {
        result = pointerOffset(old, rhs, target->type(), op == BinaryOperator::Sub, expr);
    } else {
        const ast::QualType computation = expr->computationType();
        const TypeInfo computationInfo = types_.describe(computation);
        const value::Value converted = convert(old, type, computationInfo, expr);
        result = arithmetic(op, converted, rhs, computation, expr->rhs()->type(), computation, expr);
        result = convert(result, computationInfo, type, expr);
    }
    return store(object, target->type(), result, expr);
}

// C17 6.5.2.4 and 6.5.3.1: ++E is E += 1. The operand is promoted (bit-fields as sema promotes them), incremented,
// and converted back; pointers move by one element.
value::Value Execution::increment(const ast::UnaryExpr* expr) {
    if (isEvaluatingConstraint_) {
        stop(Termination::Unsupported, "a constraint must not modify an object", expr->location());
    }
    const bool isIncrement = expr->op() == UnaryOperator::PostIncrement || expr->op() == UnaryOperator::PreIncrement;
    const bool isPrefix = expr->op() == UnaryOperator::PreIncrement || expr->op() == UnaryOperator::PreDecrement;
    const ast::Expr* target = expr->operand();
    const LValue object = lvalue(target);
    value::Value old = load(object, target->type(), target);
    const TypeInfo type = types_.describe(target->type());
    value::Value updated;
    switch (type.representation) {
    case Representation::Pointer: {
        const auto size = static_cast<UInt128>(sizeOf(TypeTable::pointee(target->type()), expr));
        updated = value::Value::integer(isIncrement ? old.bits() + size : old.bits() - size, type.integer);
        break;
    }
    case Representation::Floating:
        updated = value::floatBinary(isIncrement ? value::FloatOperator::Add : value::FloatOperator::Sub, old,
                                     one(type), type.floating);
        break;
    case Representation::Integer:
    case Representation::Bool: {
        value::IntegerFormat promoted = type.integer;
        const unsigned width = object.isBitField() ? object.bitWidth : type.integer.bits;
        if (width < 32) {
            promoted = types_.intFormat();
        } else if (object.isBitField() && width == 32) {
            promoted = {.bits = 32, .isSigned = type.integer.isSigned};
        }
        const value::Value operand = value::convertInteger(old, promoted);
        const value::Value result =
            checked(value::integerBinary(isIncrement ? value::IntegerOperator::Add : value::IntegerOperator::Sub,
                                         operand, value::Value::integer(1, promoted), promoted),
                    expr);
        updated = type.representation == Representation::Bool ? value::toBool(result)
                                                              : value::convertInteger(result, type.integer);
        break;
    }
    default:
        stop(Termination::Unsupported, "an increment of a value that is not a scalar", expr->location());
    }
    value::Value stored = store(object, target->type(), updated, expr);
    return isPrefix ? stored : old;
}

// C17 6.5.15: only the selected operand is evaluated. GNU `c ?: b` yields the condition, converted to the result type
// by sema, if it is not 0.
value::Value Execution::conditional(const ast::ConditionalExpr* expr, bool isValueUsed) {
    value::Value condition;
    const ast::Expr* selected = selectOperand(expr, condition);
    if (selected == nullptr) {
        return condition;
    }
    if (!isValueUsed || types_.describe(expr->type()).representation == Representation::Void) {
        effect(selected);
        return {};
    }
    return rvalue(selected);
}

const ast::Expr* Execution::selectOperand(const ast::ConditionalExpr* expr, value::Value& condition) {
    condition = rvalue(expr->condition());
    const bool outcome =
        condition.kind() == value::Value::Kind::Integer ? condition.bits() != 0 : value::toBool(condition).bits() != 0;
    afterCondition({.construct = ConditionEvent::Construct::Conditional,
                    .stmt = nullptr,
                    .conditional = expr,
                    .location = expr->questionLocation(),
                    .outcome = outcome,
                    .switchValue = {},
                    .switchLabel = nullptr});
    if (expr->trueExpr() == nullptr && outcome) {
        return nullptr;
    }
    return outcome ? expr->trueExpr() : expr->falseExpr();
}

value::Value Execution::statementExpression(const ast::StatementExpr* expr) {
    if (isEvaluatingConstraint_) {
        stop(Termination::Unsupported, "a constraint must not contain a statement expression", expr->location());
    }
    Frame& frame = *frame_;
    const InstructionRange& range = frame.body->statementExpressions.at(expr);
    frame.statementValue = value::Value();
    execute(frame, range.begin, &range);
    return std::exchange(frame.statementValue, value::Value());
}

std::optional<value::Value> Execution::constant(const ast::Expr* expr) {
    const value::IntegerFormat format = types_.describe(expr->type()).integer;
    if (const auto known = constants_.find(expr); known != constants_.end()) {
        return value::Value::integer(static_cast<UInt128>(known->second), format);
    }
    if (const std::optional<Int128> constant = sema::evaluateIntegerConstant(context_, expr)) {
        constants_.emplace(expr, *constant);
        return value::Value::integer(static_cast<UInt128>(*constant), format);
    }
    return std::nullopt;
}

// sizeof and _Alignof are integer constants except for variable length arrays, whose operand is evaluated (C17
// 6.5.3.4p2): `sizeof(m[i++])` increments i if m[i] is a variable length array.
value::Value Execution::sizeOrAlignment(const ast::SizeOfAlignOfExpr* expr) {
    if (std::optional<value::Value> known = constant(expr)) {
        return std::move(*known);
    }
    const value::IntegerFormat format = types_.describe(expr->type()).integer;
    const ast::QualType type = expr->operand() != nullptr ? expr->operand()->type() : expr->typeOperand();
    if (expr->operand() != nullptr) {
        effect(expr->operand());
    }
    if (expr->op() == ast::SizeOfAlignOfExpr::Operator::SizeOf) {
        return value::Value::integer(sizeOf(type, expr), format);
    }
    ast::QualType element = type;
    while (const auto* array = ast::dyn_cast<ast::ArrayType>(desugared(element))) {
        element = array->element();
    }
    return value::Value::integer(sema::alignOf(context_, element), format);
}

value::Value Execution::offsetOf(const ast::OffsetOfExpr* expr) {
    if (std::optional<value::Value> known = constant(expr)) {
        return std::move(*known);
    }
    stop(Termination::Unsupported, "offsetof with an index that is not constant", expr->location());
}

} // namespace nitwit::interp
