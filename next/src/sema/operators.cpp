// The unary, binary, conditional and assignment operators (C17 6.5.3, 6.5.5-6.5.17).

#include <format>
#include <string>
#include <vector>

#include "ast/casting.hpp"
#include "sema/sema_impl.hpp"
#include "sema/types.hpp"
#include "support/assert.hpp"

namespace nitwit::sema::detail {

namespace {

using ast::BinaryOperator;
using ast::UnaryOperator;

std::string_view spelling(BinaryOperator op) {
    switch (op) {
    case BinaryOperator::Mul:
    case BinaryOperator::MulAssign:
        return "*";
    case BinaryOperator::Div:
    case BinaryOperator::DivAssign:
        return "/";
    case BinaryOperator::Rem:
    case BinaryOperator::RemAssign:
        return "%";
    case BinaryOperator::Add:
    case BinaryOperator::AddAssign:
        return "+";
    case BinaryOperator::Sub:
    case BinaryOperator::SubAssign:
        return "-";
    case BinaryOperator::Shl:
    case BinaryOperator::ShlAssign:
        return "<<";
    case BinaryOperator::Shr:
    case BinaryOperator::ShrAssign:
        return ">>";
    case BinaryOperator::Less:
        return "<";
    case BinaryOperator::Greater:
        return ">";
    case BinaryOperator::LessEqual:
        return "<=";
    case BinaryOperator::GreaterEqual:
        return ">=";
    case BinaryOperator::Equal:
        return "==";
    case BinaryOperator::NotEqual:
        return "!=";
    case BinaryOperator::BitwiseAnd:
    case BinaryOperator::AndAssign:
        return "&";
    case BinaryOperator::BitwiseXor:
    case BinaryOperator::XorAssign:
        return "^";
    case BinaryOperator::BitwiseOr:
    case BinaryOperator::OrAssign:
        return "|";
    case BinaryOperator::LogicalAnd:
        return "&&";
    case BinaryOperator::LogicalOr:
        return "||";
    case BinaryOperator::Assign:
        return "=";
    case BinaryOperator::Comma:
        return ",";
    }
    NITWIT_UNREACHABLE("unknown binary operator");
}

// The operator of a compound assignment, e.g. Add for `+=`.
BinaryOperator arithmeticOperatorOf(BinaryOperator op) {
    switch (op) {
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

bool needsIntegerOperands(BinaryOperator op) {
    return op == BinaryOperator::Rem || op == BinaryOperator::Shl || op == BinaryOperator::Shr
           || op == BinaryOperator::BitwiseAnd || op == BinaryOperator::BitwiseXor || op == BinaryOperator::BitwiseOr;
}

bool hasConstMember(ast::AstContext& context, const ast::RecordDecl* record) {
    for (const ast::FieldDecl* field : record->fields()) {
        if (isConstQualified(context, field->type())) {
            return true;
        }
        if (const ast::RecordDecl* inner = recordOf(field->type());
            inner != nullptr && hasConstMember(context, inner)) {
            return true;
        }
    }
    return false;
}

} // namespace

void Sema::rejectComplex(const ast::Expr* expr, SourceLocation location) {
    if (classify(context_, expr->type()) == TypeClass::Complex) {
        error(location, "complex arithmetic is not supported");
    }
}

// C17 6.5.16p2, 6.3.2.1p1: the operand of an assignment or increment is a modifiable lvalue. `operation` is
// "assignment", "increment" or "decrement", for GCC's wording.
void Sema::requireModifiableLvalue(const ast::Expr* expr, SourceLocation location, std::string_view operation) {
    const std::string notAnLvalue = operation == "assignment"
                                        ? std::string("lvalue required as left operand of assignment")
                                        : std::format("lvalue required as {} operand", operation);
    std::string readOnly = std::format("{} of read-only location", operation);
    const ast::Expr* inner = ast::ignoreParens(expr);
    if (const auto* reference = ast::dyn_cast<ast::DeclRefExpr>(inner)) {
        readOnly = std::format("{} of read-only variable '{}'", operation, reference->name().text());
    } else if (const auto* member = ast::dyn_cast<ast::MemberExpr>(inner);
               member != nullptr && member->field() != nullptr) {
        readOnly = isConstQualified(context_, member->field()->type())
                       ? std::format("{} of read-only member '{}'", operation, member->member().text())
                       : std::format("{} of member '{}' in read-only object", operation, member->member().text());
    }
    if (!expr->isLValue()) {
        error(location, notAnLvalue);
    }
    const TypeClass type = classify(context_, expr->type());
    if (type == TypeClass::Array) {
        error(location, std::format("{} to expression with array type", operation));
    }
    if (type == TypeClass::Function || type == TypeClass::Void) {
        error(location, notAnLvalue);
    }
    if (isConstQualified(context_, expr->type())) {
        error(location, readOnly);
    }
    if (type == TypeClass::Record) {
        requireComplete(expr->type(), location,
                        std::format("invalid use of undefined type '{}'", typeName(expr->type())));
        if (hasConstMember(context_, recordOf(expr->type()))) {
            error(location, readOnly);
        }
    }
}

void Sema::unary(ast::UnaryExpr* unary) {
    ast::Expr* operand = unary->operand();
    expr(operand);
    const SourceLocation location = unary->location();
    switch (unary->op()) {
    case UnaryOperator::PreIncrement:
    case UnaryOperator::PreDecrement:
    case UnaryOperator::PostIncrement:
    case UnaryOperator::PostDecrement: {
        // C17 6.5.2.4, 6.5.3.1: the operand stays an lvalue; the interpreter adds 1 in the operand's promoted type
        // (or by the pointee's size) and converts the result back.
        rejectComplex(operand, location);
        const TypeClass type = classify(context_, operand->type());
        const std::string_view operation =
            unary->op() == UnaryOperator::PreIncrement || unary->op() == UnaryOperator::PostIncrement ? "increment"
                                                                                                      : "decrement";
        if (!isArithmeticClass(type) && type != TypeClass::Pointer) {
            error(location, std::format("wrong type argument to {}", operation));
        }
        requireModifiableLvalue(operand, location, operation);
        if (type == TypeClass::Pointer) {
            const ast::QualType pointee = pointeeOf(operand->type());
            const TypeClass pointeeClass = classify(context_, pointee);
            if (pointeeClass != TypeClass::Void && pointeeClass != TypeClass::Function && !isVariablySized(pointee)) {
                requireComplete(pointee, location,
                                std::format("increment of pointer to an incomplete type '{}'", typeName(pointee)));
            }
        }
        return unary->setType(valueType(operand), ast::ValueCategory::RValue);
    }
    case UnaryOperator::AddressOf: {
        // C17 6.5.3.2p1: a function designator, the result of `*` or `[]`, or an lvalue that is not a bit-field and
        // not declared `register`.
        const ast::Expr* inner = ast::ignoreParens(operand);
        const auto* innerUnary = ast::dyn_cast<ast::UnaryExpr>(inner);
        const bool isDereference = innerUnary != nullptr && innerUnary->op() == UnaryOperator::Dereference;
        if (!isDereference && !operand->isLValue()) {
            error(location, "lvalue required as unary '&' operand");
        }
        if (const auto* member = ast::dyn_cast<ast::MemberExpr>(inner);
            member != nullptr && member->field()->isBitField()) {
            error(location, std::format("cannot take address of bit-field '{}'", member->member().text()));
        }
        if (const auto* reference = ast::dyn_cast<ast::DeclRefExpr>(inner)) {
            if (const auto* var = ast::dyn_cast<ast::VarDecl>(reference->decl());
                var != nullptr && var->storage() == ast::StorageClass::Register) {
                error(location, std::format("address of register variable '{}' requested", var->name().text()));
            }
        }
        return unary->setType(context_.pointerTo(operand->type()), ast::ValueCategory::RValue);
    }
    case UnaryOperator::Dereference: {
        ast::Expr* pointer = rvalue(operand);
        unary->setOperand(pointer);
        if (classify(context_, pointer->type()) != TypeClass::Pointer) {
            error(location, std::format("invalid type argument of unary '*' (have '{}')", typeName(pointer->type())));
        }
        const ast::QualType pointee = pointeeOf(pointer->type());
        if (classify(context_, pointee) == TypeClass::Void) {
            warning(location, "dereferencing 'void *' pointer");
            return unary->setType(pointee, ast::ValueCategory::RValue);
        }
        return unary->setType(pointee, ast::ValueCategory::LValue);
    }
    case UnaryOperator::Plus:
    case UnaryOperator::Minus:
    case UnaryOperator::BitwiseNot: {
        rejectComplex(operand, location);
        const TypeClass type = classify(context_, operand->type());
        const bool isValid = unary->op() == UnaryOperator::BitwiseNot ? isIntegerClass(type) : isArithmeticClass(type);
        if (!isValid) {
            error(location,
                  std::format("wrong type argument to unary {}", unary->op() == UnaryOperator::Plus    ? "plus"
                                                                 : unary->op() == UnaryOperator::Minus ? "minus"
                                                                                                       : "complement"));
        }
        ast::Expr* value = promote(operand);
        unary->setOperand(value);
        return unary->setType(value->type(), ast::ValueCategory::RValue);
    }
    case UnaryOperator::LogicalNot: {
        // C17 6.5.3.3p5: the operand is a scalar, compared with 0; the result is an int.
        rejectComplex(operand, location);
        ast::Expr* value = rvalue(operand);
        if (!isScalarClass(classify(context_, value->type()))) {
            error(location, "wrong type argument to unary exclamation mark");
        }
        unary->setOperand(value);
        return unary->setType(context_.builtin(ast::BuiltinKind::Int), ast::ValueCategory::RValue);
    }
    case UnaryOperator::Real:
    case UnaryOperator::Imag: {
        // GNU: the parts of a complex value; for a real operand, the value itself and 0.
        const TypeClass type = classify(context_, operand->type());
        if (type == TypeClass::Complex) {
            const ast::QualType element =
                ast::cast<ast::ComplexType>(context_.canonical(operand->type()).type())->element();
            return unary->setType(element, operand->category());
        }
        if (!isArithmeticClass(type)) {
            error(location, "wrong type argument to __real__ or __imag__");
        }
        if (unary->op() == UnaryOperator::Real) {
            return unary->setType(operand->type(), operand->category());
        }
        unary->setOperand(rvalue(operand));
        return unary->setType(valueType(operand), ast::ValueCategory::RValue);
    }
    case UnaryOperator::Extension:
        return unary->setType(operand->type(), operand->category());
    }
    NITWIT_UNREACHABLE("unknown unary operator");
}

// Left-nested chains of binary operators (`a + b + c + ...`, long in generated programs) are analyzed in a loop, so
// that the recursion depth does not grow with their length.
void Sema::binary(ast::BinaryExpr* binary) {
    std::vector<ast::BinaryExpr*> chain;
    for (ast::Expr* node = binary;;) {
        auto* nested = ast::dyn_cast<ast::BinaryExpr>(node);
        if (nested == nullptr || !nested->type().isNull()) {
            expr(node);
            break;
        }
        chain.push_back(nested);
        node = nested->lhs();
    }
    for (auto nested = chain.rbegin(); nested != chain.rend(); ++nested) {
        binaryOperator(*nested);
    }
}

void Sema::binaryOperator(ast::BinaryExpr* binary) {
    expr(binary->rhs());
    switch (binary->op()) {
    case BinaryOperator::Mul:
    case BinaryOperator::Div:
    case BinaryOperator::Rem:
    case BinaryOperator::BitwiseAnd:
    case BinaryOperator::BitwiseXor:
    case BinaryOperator::BitwiseOr:
        return arithmetic(binary);
    case BinaryOperator::Add:
    case BinaryOperator::Sub:
        return additive(binary);
    case BinaryOperator::Shl:
    case BinaryOperator::Shr:
        return shift(binary);
    case BinaryOperator::Less:
    case BinaryOperator::Greater:
    case BinaryOperator::LessEqual:
    case BinaryOperator::GreaterEqual:
    case BinaryOperator::Equal:
    case BinaryOperator::NotEqual:
        return comparison(binary);
    case BinaryOperator::LogicalAnd:
    case BinaryOperator::LogicalOr:
        return logical(binary);
    case BinaryOperator::Assign:
        return assignment(binary);
    case BinaryOperator::Comma: {
        // C17 6.5.17p2: the left operand is a void expression; the result is the right operand's value.
        ast::Expr* rhs = rvalue(binary->rhs());
        binary->setOperands(binary->lhs(), rhs);
        return binary->setType(rhs->type(), ast::ValueCategory::RValue);
    }
    default:
        return compoundAssignment(binary);
    }
}

void Sema::invalidOperands(const ast::BinaryExpr* binary) {
    error(binary->operatorLocation(),
          std::format("invalid operands to binary {} (have '{}' and '{}')", spelling(binary->op()),
                      typeName(binary->lhs()->type()), typeName(binary->rhs()->type())));
}

// C17 6.5.5, 6.5.10-12: arithmetic (or integer) operands after the usual arithmetic conversions.
void Sema::arithmetic(ast::BinaryExpr* binary) {
    ast::Expr* lhs = binary->lhs();
    ast::Expr* rhs = binary->rhs();
    rejectComplex(lhs, binary->operatorLocation());
    rejectComplex(rhs, binary->operatorLocation());
    const TypeClass left = classify(context_, valueType(lhs));
    const TypeClass right = classify(context_, valueType(rhs));
    const bool isValid = needsIntegerOperands(binary->op()) ? isIntegerClass(left) && isIntegerClass(right)
                                                            : isArithmeticClass(left) && isArithmeticClass(right);
    if (!isValid) {
        invalidOperands(binary);
    }
    const ast::QualType common = usualArithmeticConversions(lhs, rhs);
    binary->setOperands(lhs, rhs);
    binary->setType(common, ast::ValueCategory::RValue);
}

// The pointee of pointer arithmetic: a complete object type; GNU also allows void and functions, as size 1.
void Sema::checkPointerArithmetic(ast::QualType pointer, SourceLocation location) {
    const ast::QualType pointee = pointeeOf(pointer);
    const TypeClass type = classify(context_, pointee);
    if (type != TypeClass::Void && type != TypeClass::Function && !isVariablySized(pointee)) {
        requireComplete(pointee, location,
                        std::format("arithmetic on pointer to an incomplete type '{}'", typeName(pointee)));
    }
}

// C17 6.5.6: arithmetic operands, a pointer and an integer, or (for -) two pointers to compatible types.
void Sema::additive(ast::BinaryExpr* binary) {
    ast::Expr* lhs = binary->lhs();
    ast::Expr* rhs = binary->rhs();
    rejectComplex(lhs, binary->operatorLocation());
    rejectComplex(rhs, binary->operatorLocation());
    const TypeClass left = classify(context_, valueType(lhs));
    const TypeClass right = classify(context_, valueType(rhs));
    if (isArithmeticClass(left) && isArithmeticClass(right)) {
        return arithmetic(binary);
    }
    const bool isAdd = binary->op() == BinaryOperator::Add;
    if (left == TypeClass::Pointer && isIntegerClass(right)) {
        lhs = rvalue(lhs);
        rhs = promote(rhs);
        checkPointerArithmetic(lhs->type(), binary->operatorLocation());
        binary->setOperands(lhs, rhs);
        return binary->setType(lhs->type(), ast::ValueCategory::RValue);
    }
    if (isAdd && isIntegerClass(left) && right == TypeClass::Pointer) {
        lhs = promote(lhs);
        rhs = rvalue(rhs);
        checkPointerArithmetic(rhs->type(), binary->operatorLocation());
        binary->setOperands(lhs, rhs);
        return binary->setType(rhs->type(), ast::ValueCategory::RValue);
    }
    if (!isAdd && left == TypeClass::Pointer && right == TypeClass::Pointer) {
        lhs = rvalue(lhs);
        rhs = rvalue(rhs);
        if (!compatibleUnqualified(context_, pointeeOf(lhs->type()), pointeeOf(rhs->type()))) {
            invalidOperands(binary);
        }
        checkPointerArithmetic(lhs->type(), binary->operatorLocation());
        binary->setOperands(lhs, rhs);
        return binary->setType(ptrdiffType(context_), ast::ValueCategory::RValue);
    }
    invalidOperands(binary);
}

// C17 6.5.7: integer operands, promoted separately; the result has the promoted left type.
void Sema::shift(ast::BinaryExpr* binary) {
    ast::Expr* lhs = binary->lhs();
    ast::Expr* rhs = binary->rhs();
    if (!isIntegerClass(classify(context_, valueType(lhs))) || !isIntegerClass(classify(context_, valueType(rhs)))) {
        invalidOperands(binary);
    }
    lhs = promote(lhs);
    rhs = promote(rhs);
    binary->setOperands(lhs, rhs);
    binary->setType(lhs->type(), ast::ValueCategory::RValue);
}

// C17 6.5.8-9: arithmetic operands, or pointers. GCC accepts pointers to incompatible types and pointers compared with
// integers with a warning; a null pointer constant is converted to the other operand's pointer type.
void Sema::comparison(ast::BinaryExpr* binary) {
    ast::Expr* lhs = binary->lhs();
    ast::Expr* rhs = binary->rhs();
    rejectComplex(lhs, binary->operatorLocation());
    rejectComplex(rhs, binary->operatorLocation());
    const ast::QualType intType = context_.builtin(ast::BuiltinKind::Int);
    const TypeClass left = classify(context_, valueType(lhs));
    const TypeClass right = classify(context_, valueType(rhs));
    if (isArithmeticClass(left) && isArithmeticClass(right)) {
        (void)usualArithmeticConversions(lhs, rhs);
        binary->setOperands(lhs, rhs);
        return binary->setType(intType, ast::ValueCategory::RValue);
    }
    if (left == TypeClass::Pointer && right == TypeClass::Pointer) {
        lhs = rvalue(lhs);
        rhs = rvalue(rhs);
        const ast::QualType leftPointee = pointeeOf(lhs->type());
        const ast::QualType rightPointee = pointeeOf(rhs->type());
        const bool eitherVoid =
            classify(context_, leftPointee) == TypeClass::Void || classify(context_, rightPointee) == TypeClass::Void;
        if (!compatibleUnqualified(context_, leftPointee, rightPointee) && !eitherVoid) {
            warning(binary->operatorLocation(), "comparison of distinct pointer types lacks a cast");
        }
        binary->setOperands(lhs, rhs);
        return binary->setType(intType, ast::ValueCategory::RValue);
    }
    if (left == TypeClass::Pointer && isIntegerClass(right)) {
        if (!isNullPointerConstant(rhs)) {
            warning(binary->operatorLocation(), "comparison between pointer and integer");
        }
        lhs = rvalue(lhs);
        rhs = implicitCast(rvalue(rhs), lhs->type(), castKind(rhs, valueType(rhs), lhs->type()));
        binary->setOperands(lhs, rhs);
        return binary->setType(intType, ast::ValueCategory::RValue);
    }
    if (isIntegerClass(left) && right == TypeClass::Pointer) {
        if (!isNullPointerConstant(lhs)) {
            warning(binary->operatorLocation(), "comparison between pointer and integer");
        }
        rhs = rvalue(rhs);
        lhs = implicitCast(rvalue(lhs), rhs->type(), castKind(lhs, valueType(lhs), rhs->type()));
        binary->setOperands(lhs, rhs);
        return binary->setType(intType, ast::ValueCategory::RValue);
    }
    invalidOperands(binary);
}

// C17 6.5.13-14: scalar operands, compared with 0; the result is an int.
void Sema::logical(ast::BinaryExpr* binary) {
    ast::Expr* lhs = rvalue(binary->lhs());
    ast::Expr* rhs = rvalue(binary->rhs());
    rejectComplex(lhs, binary->operatorLocation());
    rejectComplex(rhs, binary->operatorLocation());
    if (!isScalarClass(classify(context_, lhs->type())) || !isScalarClass(classify(context_, rhs->type()))) {
        invalidOperands(binary);
    }
    binary->setOperands(lhs, rhs);
    binary->setType(context_.builtin(ast::BuiltinKind::Int), ast::ValueCategory::RValue);
}

// C17 6.5.16.1: the right operand is converted to the type of the left one; the result is the stored value.
void Sema::assignment(ast::BinaryExpr* binary) {
    ast::Expr* lhs = binary->lhs();
    requireModifiableLvalue(lhs, binary->operatorLocation(), "assignment");
    const AssignmentContext context{.kind = AssignmentContext::Kind::Assignment,
                                    .location = binary->operatorLocation(),
                                    .argumentIndex = 0,
                                    .function = nullptr};
    ast::Expr* rhs = convertForAssignment(binary->rhs(), lhs->type(), context);
    binary->setOperands(lhs, rhs);
    binary->setType(valueType(lhs), ast::ValueCategory::RValue);
}

// C17 6.5.16.2: `E1 op= E2` computes E1 op E2 in the computation type, then converts back to the type of E1.
void Sema::compoundAssignment(ast::BinaryExpr* binary) {
    ast::Expr* lhs = binary->lhs();
    ast::Expr* rhs = binary->rhs();
    const BinaryOperator op = arithmeticOperatorOf(binary->op());
    rejectComplex(lhs, binary->operatorLocation());
    rejectComplex(rhs, binary->operatorLocation());
    requireModifiableLvalue(lhs, binary->operatorLocation(), "assignment");
    const TypeClass left = classify(context_, lhs->type());
    const TypeClass right = classify(context_, valueType(rhs));
    ast::QualType computation;
    if ((op == BinaryOperator::Add || op == BinaryOperator::Sub) && left == TypeClass::Pointer) {
        if (!isIntegerClass(right)) {
            invalidOperands(binary);
        }
        checkPointerArithmetic(lhs->type(), binary->operatorLocation());
        rhs = promote(rhs);
        computation = valueType(lhs);
    } else {
        const bool isValid = needsIntegerOperands(op) ? isIntegerClass(left) && isIntegerClass(right)
                                                      : isArithmeticClass(left) && isArithmeticClass(right);
        if (!isValid) {
            invalidOperands(binary);
        }
        const ast::QualType promotedLeft = promotedTypeOf(lhs);
        rhs = promote(rhs);
        if (op == BinaryOperator::Shl || op == BinaryOperator::Shr) {
            computation = promotedLeft;
        } else {
            computation = commonArithmeticType(context_, promotedLeft, rhs->type());
            rhs = convert(rhs, computation);
        }
    }
    binary->setComputationType(computation);
    binary->setOperands(lhs, rhs);
    binary->setType(valueType(lhs), ast::ValueCategory::RValue);
}

// C17 6.5.15. GNU `c ?: b`: the condition, converted to the result type, is also the second operand; converting it
// before the test does not change whether it is 0, so the interpreter tests the converted value.
void Sema::conditional(ast::ConditionalExpr* conditional) {
    const bool isBinary = conditional->trueExpr() == nullptr;
    ast::Expr* condition = conditional->condition();
    expr(condition);
    if (!isBinary) {
        expr(conditional->trueExpr());
    }
    expr(conditional->falseExpr());
    ast::Expr* test = rvalue(condition);
    rejectComplex(test, conditional->questionLocation());
    if (!isScalarClass(classify(context_, test->type()))) {
        error(condition->location(), "used non-scalar type value where scalar is required");
    }
    ast::Expr* originalSecond = isBinary ? condition : conditional->trueExpr();
    ast::Expr* originalThird = conditional->falseExpr();
    ast::Expr* second = isBinary ? test : rvalue(originalSecond);
    ast::Expr* third = rvalue(originalThird);
    rejectComplex(second, conditional->questionLocation());
    rejectComplex(third, conditional->questionLocation());
    const TypeClass secondClass = classify(context_, second->type());
    const TypeClass thirdClass = classify(context_, third->type());
    ast::QualType type;
    if (isArithmeticClass(secondClass) && isArithmeticClass(thirdClass)) {
        type = usualArithmeticConversions(second, third);
    } else if (secondClass == TypeClass::Void || thirdClass == TypeClass::Void) {
        type = context_.builtin(ast::BuiltinKind::Void); // GNU allows one void operand
    } else if ((secondClass == TypeClass::Record || secondClass == TypeClass::VaList)
               && compatibleUnqualified(context_, second->type(), third->type())) {
        type = second->type();
    } else if (secondClass == TypeClass::Pointer && thirdClass == TypeClass::Pointer) {
        const ast::QualType secondPointee = context_.canonical(pointeeOf(second->type()));
        const ast::QualType thirdPointee = context_.canonical(pointeeOf(third->type()));
        const ast::Qualifiers qualifiers = secondPointee.qualifiers() | thirdPointee.qualifiers();
        if (isNullPointerConstant(originalThird)) {
            type = second->type();
        } else if (isNullPointerConstant(originalSecond)) {
            type = third->type();
        } else if (compatibleUnqualified(context_, secondPointee, thirdPointee)) {
            type = context_.pointerTo(composite(context_, secondPointee.unqualified(), thirdPointee.unqualified())
                                          .withQualifiers(qualifiers));
        } else {
            if (classify(context_, secondPointee) != TypeClass::Void
                && classify(context_, thirdPointee) != TypeClass::Void) {
                warning(conditional->questionLocation(), "pointer type mismatch in conditional expression");
            }
            type = context_.pointerTo(context_.builtin(ast::BuiltinKind::Void).withQualifiers(qualifiers));
        }
    } else if (secondClass == TypeClass::Pointer && isIntegerClass(thirdClass)) {
        if (!isNullPointerConstant(originalThird)) {
            warning(conditional->questionLocation(), "pointer/integer type mismatch in conditional expression");
        }
        type = second->type();
    } else if (isIntegerClass(secondClass) && thirdClass == TypeClass::Pointer) {
        if (!isNullPointerConstant(originalSecond)) {
            warning(conditional->questionLocation(), "pointer/integer type mismatch in conditional expression");
        }
        type = third->type();
    } else {
        error(conditional->questionLocation(), "type mismatch in conditional expression");
    }
    if (classify(context_, type) != TypeClass::Void) {
        if (classify(context_, type) == TypeClass::Pointer) {
            second = implicitPointerConversion(second, originalSecond, type);
            third = implicitPointerConversion(third, originalThird, type);
        }
    }
    conditional->setOperands(isBinary ? second : test, isBinary ? nullptr : second, third);
    conditional->setType(type, ast::ValueCategory::RValue);
}

// Converts an rvalue to a pointer type without diagnostics: a null pointer constant, an integer, or another pointer.
ast::Expr* Sema::implicitPointerConversion(ast::Expr* value, const ast::Expr* original, ast::QualType type) {
    if (context_.canonical(value->type()) == context_.canonical(type)) {
        return value;
    }
    return implicitCast(value, type, castKind(original, value->type(), type));
}

} // namespace nitwit::sema::detail
