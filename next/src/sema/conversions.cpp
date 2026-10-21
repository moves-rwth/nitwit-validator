// Implicit conversions (C17 6.3) as explicit ImplicitCastExprs: lvalue conversion and decay, integer promotions, the
// usual arithmetic conversions, default argument promotions and the conversions as if by assignment, with GCC's
// diagnostics.

#include <format>
#include <string>

#include "ast/casting.hpp"
#include "sema/constant.hpp"
#include "sema/sema_impl.hpp"
#include "sema/types.hpp"
#include "support/assert.hpp"

namespace nitwit::sema::detail {

namespace {

// The bit-field an expression designates or whose value it has, for GCC's promotion of bit-fields by their width:
// GCC types such an expression with the bit-field's own integer type, also through parentheses, the comma operator,
// assignments and increments.
const ast::FieldDecl* bitFieldOf(const ast::Expr* expr) {
    while (true) {
        expr = ast::ignoreParens(expr);
        if (const auto* member = ast::dyn_cast<ast::MemberExpr>(expr)) {
            return member->field() != nullptr && member->field()->isBitField() ? member->field() : nullptr;
        }
        if (const auto* unary = ast::dyn_cast<ast::UnaryExpr>(expr)) {
            switch (unary->op()) {
            case ast::UnaryOperator::Extension:
            case ast::UnaryOperator::PreIncrement:
            case ast::UnaryOperator::PreDecrement:
            case ast::UnaryOperator::PostIncrement:
            case ast::UnaryOperator::PostDecrement:
                expr = unary->operand();
                continue;
            default:
                return nullptr;
            }
        }
        if (const auto* binary = ast::dyn_cast<ast::BinaryExpr>(expr)) {
            if (binary->op() == ast::BinaryOperator::Comma) {
                expr = binary->rhs();
                continue;
            }
            if (binary->op() >= ast::BinaryOperator::Assign && binary->op() <= ast::BinaryOperator::OrAssign) {
                expr = binary->lhs();
                continue;
            }
        }
        if (const auto* cast = ast::dyn_cast<ast::ImplicitCastExpr>(expr);
            cast != nullptr && cast->castKind() == ast::CastKind::LValueToRValue) {
            expr = cast->operand();
            continue;
        }
        return nullptr;
    }
}

bool qualifiersInclude(ast::Qualifiers outer, ast::Qualifiers inner) {
    return (outer.isConst || !inner.isConst) && (outer.isVolatile || !inner.isVolatile);
}

std::string_view droppedQualifier(ast::Qualifiers target, ast::Qualifiers source) {
    return source.isConst && !target.isConst ? "const" : "volatile";
}

} // namespace

ast::Expr* Sema::implicitCast(ast::Expr* expr, ast::QualType type, ast::CastKind kind) {
    auto* cast = context_.create<ast::ImplicitCastExpr>(kind, expr);
    cast->setType(type, ast::ValueCategory::RValue);
    return cast;
}

ast::QualType Sema::valueType(const ast::Expr* expr) {
    const ast::QualType type = expr->type();
    switch (classify(context_, type)) {
    case TypeClass::Array: {
        // C17 6.3.2.1p3; the qualifiers of the array apply to the elements.
        const ast::QualType array = desugar(type);
        return context_.pointerTo(
            ast::cast<ast::ArrayType>(array.type())->element().withQualifiers(array.qualifiers()));
    }
    case TypeClass::Function:
        return context_.pointerTo(type);
    default:
        return unqualified(context_, type);
    }
}

ast::Expr* Sema::rvalue(ast::Expr* expr) {
    switch (classify(context_, expr->type())) {
    case TypeClass::Array:
        return implicitCast(expr, valueType(expr), ast::CastKind::ArrayToPointerDecay);
    case TypeClass::Function:
        return implicitCast(expr, valueType(expr), ast::CastKind::FunctionToPointerDecay);
    case TypeClass::Record:
        if (expr->isLValue() && !isComplete(expr->type())) {
            error(expr->location(), std::format("invalid use of undefined type '{}'", typeName(expr->type())));
        }
        break;
    default:
        break;
    }
    if (!expr->isLValue()) {
        return expr;
    }
    return implicitCast(expr, valueType(expr), ast::CastKind::LValueToRValue);
}

ast::QualType Sema::promotedTypeOf(const ast::Expr* expr) {
    if (const ast::FieldDecl* field = bitFieldOf(expr)) {
        // GCC's perform_integral_promotions: a bit-field narrower than int becomes int. GCC gives a wider bit-field
        // an integer type of its width; for a width of 32 that behaves as int or unsigned int.
        if (field->bitWidth() < 32) {
            return context_.builtin(ast::BuiltinKind::Int);
        }
        if (field->bitWidth() == 32) {
            return context_.builtin(isSignedInteger(context_, field->type()) ? ast::BuiltinKind::Int
                                                                             : ast::BuiltinKind::UnsignedInt);
        }
    }
    return promotedType(context_, valueType(expr));
}

ast::Expr* Sema::promote(ast::Expr* expr) {
    const ast::QualType promoted = promotedTypeOf(expr);
    return convert(rvalue(expr), promoted);
}

ast::Expr* Sema::defaultArgumentPromotion(ast::Expr* expr) {
    if (builtinKind(context_, expr->type()) == ast::BuiltinKind::Float) {
        return convert(rvalue(expr), context_.builtin(ast::BuiltinKind::Double));
    }
    return promote(expr);
}

ast::CastKind Sema::castKind(const ast::Expr* expr, ast::QualType from, ast::QualType to) {
    const TypeClass source = classify(context_, from);
    const TypeClass target = classify(context_, to);
    if (target == TypeClass::Void) {
        return ast::CastKind::ToVoid;
    }
    if (target == TypeClass::Bool) {
        switch (source) {
        case TypeClass::Bool:
            return ast::CastKind::NoOp;
        case TypeClass::Floating:
            return ast::CastKind::FloatingToBoolean;
        case TypeClass::Pointer:
            return ast::CastKind::PointerToBoolean;
        default:
            return ast::CastKind::IntegralToBoolean;
        }
    }
    if (target == TypeClass::Integer) {
        switch (source) {
        case TypeClass::Floating:
            return ast::CastKind::FloatingToIntegral;
        case TypeClass::Pointer:
            return ast::CastKind::PointerToIntegral;
        default:
            return ast::CastKind::IntegralCast;
        }
    }
    if (target == TypeClass::Floating) {
        return source == TypeClass::Floating ? ast::CastKind::FloatingCast : ast::CastKind::IntegralToFloating;
    }
    if (target == TypeClass::Pointer) {
        if (source != TypeClass::Pointer) {
            return isNullPointerConstant(expr) ? ast::CastKind::NullToPointer : ast::CastKind::IntegralToPointer;
        }
        const ast::QualType sourcePointee = context_.canonical(pointeeOf(from));
        const ast::QualType targetPointee = context_.canonical(pointeeOf(to));
        return sourcePointee.unqualified() == targetPointee.unqualified() ? ast::CastKind::NoOp
                                                                          : ast::CastKind::BitCast;
    }
    if (target == TypeClass::Record && source == TypeClass::Record
        && context_.canonical(from).unqualified() != context_.canonical(to).unqualified()) {
        return ast::CastKind::ToUnion;
    }
    return ast::CastKind::NoOp;
}

ast::Expr* Sema::convert(ast::Expr* expr, ast::QualType type) {
    const ast::QualType target = unqualified(context_, type);
    if (context_.canonical(expr->type()).unqualified() == context_.canonical(target)) {
        return expr;
    }
    return implicitCast(expr, target, castKind(expr, expr->type(), target));
}

ast::QualType Sema::usualArithmeticConversions(ast::Expr*& lhs, ast::Expr*& rhs) {
    lhs = promote(lhs);
    rhs = promote(rhs);
    const ast::QualType common = commonArithmeticType(context_, lhs->type(), rhs->type());
    lhs = convert(lhs, common);
    rhs = convert(rhs, common);
    return common;
}

// On x86-64, GCC's va_list is an array of one `struct __va_list_tag`, so a va_list argument is the address of the
// va_list, and CIL-generated programs pass `(__va_list_tag *)&ap` or `&ap` where GCC expects one (some even declare
// arrays of va_lists). NITWIT's va_list is an object, so such a pointer stands for the (first) va_list it points to.
bool Sema::isVaListAddress(ast::QualType type) {
    if (context_.target().dataModel() != DataModel::LP64 || classify(context_, type) != TypeClass::Pointer) {
        return false;
    }
    ast::QualType pointee = pointeeOf(type);
    while (const ast::ArrayType* array = arrayOf(pointee)) {
        pointee = array->element();
    }
    const ast::RecordDecl* record = recordOf(pointee);
    return classify(context_, pointee) == TypeClass::VaList
           || (record != nullptr && record->name().text() == "__va_list_tag");
}

// The attribute is written on the union or on a typedef of it.
bool Sema::isTransparentUnion(ast::QualType type) {
    while (const auto* typedefType = ast::dyn_cast<ast::TypedefType>(type.type())) {
        if (findAttribute(typedefType->decl(), "transparent_union") != nullptr) {
            return true;
        }
        type = typedefType->decl()->underlying();
    }
    const ast::RecordDecl* record = recordOf(type);
    return record != nullptr && findAttribute(record, "transparent_union") != nullptr;
}

ast::Expr* Sema::vaListValue(ast::Expr* expr) {
    ast::Expr* value = rvalue(expr);
    if (!isVaListAddress(value->type())) {
        return value;
    }
    const ast::QualType vaList = context_.builtin(ast::BuiltinKind::VaList);
    ast::Expr* pointer = convert(value, context_.pointerTo(vaList));
    auto* object = context_.create<ast::UnaryExpr>(expr->range(), ast::UnaryOperator::Dereference, pointer);
    object->setType(vaList, ast::ValueCategory::LValue);
    return rvalue(object);
}

bool Sema::isNullPointerConstant(const ast::Expr* expr) {
    expr = ast::ignoreParens(expr);
    if (const auto* castExpr = ast::dyn_cast<ast::CastExpr>(expr)) {
        const ast::QualType target = context_.canonical(castExpr->type());
        if (const auto* pointer = ast::dyn_cast<ast::PointerType>(target.type());
            pointer != nullptr && pointer->pointee().qualifiers().empty()
            && classify(context_, pointer->pointee()) == TypeClass::Void) {
            return isNullPointerConstant(castExpr->operand());
        }
    }
    if (const auto* implicit = ast::dyn_cast<ast::ImplicitCastExpr>(expr);
        implicit != nullptr && implicit->castKind() == ast::CastKind::LValueToRValue) {
        return false;
    }
    if (!isIntegerClass(classify(context_, expr->type()))) {
        return false;
    }
    const std::optional<Int128> value = evaluateIntegerConstant(context_, expr);
    return value && *value == 0;
}

ast::Expr* Sema::condition(ast::Expr* expr, std::string_view what) {
    this->expr(expr);
    ast::Expr* value = rvalue(expr);
    const TypeClass type = classify(context_, value->type());
    if (type == TypeClass::Complex) {
        error(expr->location(), "complex arithmetic is not supported");
    }
    if (!isScalarClass(type)) {
        error(expr->location(), std::format("used {} type value where scalar is required in {}",
                                            type == TypeClass::Record ? "struct or union" : "non-scalar", what));
    }
    return value;
}

void Sema::warnPointerConversion(std::string_view problem, ast::QualType from, ast::QualType to,
                                 const AssignmentContext& context) {
    std::string subject;
    switch (context.kind) {
    case AssignmentContext::Kind::Assignment:
        subject = std::format("assignment to '{}' from '{}'", typeName(to), typeName(from));
        break;
    case AssignmentContext::Kind::Initialization:
        subject = std::format("initialization of '{}' from '{}'", typeName(to), typeName(from));
        break;
    case AssignmentContext::Kind::Argument:
        subject = std::format("passing argument {} of '{}'", context.argumentIndex,
                              context.function != nullptr ? context.function->name().text() : "the called function");
        break;
    case AssignmentContext::Kind::Return:
        subject = std::format("returning '{}' from a function with return type '{}'", typeName(from), typeName(to));
        break;
    }
    warning(context.location, std::format("{} {}", subject, problem));
}

void Sema::warnIncompatiblePointers(ast::QualType from, ast::QualType to, const AssignmentContext& context) {
    switch (context.kind) {
    case AssignmentContext::Kind::Assignment:
        return warning(context.location, std::format("assignment to '{}' from incompatible pointer type '{}'",
                                                     typeName(to), typeName(from)));
    case AssignmentContext::Kind::Initialization:
        return warning(context.location, std::format("initialization of '{}' from incompatible pointer type '{}'",
                                                     typeName(to), typeName(from)));
    case AssignmentContext::Kind::Argument:
        return warnPointerConversion("from incompatible pointer type", from, to, context);
    case AssignmentContext::Kind::Return:
        return warning(context.location,
                       std::format("returning '{}' from a function with incompatible return type '{}'", typeName(from),
                                   typeName(to)));
    }
}

// C17 6.5.16.1p1 with GCC's extensions: integer/pointer conversions and incompatible pointer types are accepted with
// a warning (GCC 14 rejects them unless -fpermissive, but older sv-benchmarks rely on them).
ast::Expr* Sema::convertForAssignment(ast::Expr* expr, ast::QualType type, const AssignmentContext& context) {
    const ast::QualType target = unqualified(context_, type);
    const TypeClass targetClass = classify(context_, target);

    // GNU transparent unions: an argument of a member's type is passed as the union.
    if (targetClass == TypeClass::Record && context.kind == AssignmentContext::Kind::Argument) {
        ast::RecordDecl* record = recordOf(target);
        if (record->isUnion() && isTransparentUnion(target) && !compatibleUnqualified(context_, expr->type(), target)) {
            ast::Expr* value = rvalue(expr);
            for (const ast::FieldDecl* member : record->fields()) {
                if (compatibleUnqualified(context_, member->type(), value->type())
                    || (isPointer(context_, member->type()) && isNullPointerConstant(expr))) {
                    return implicitCast(value, target, ast::CastKind::ToUnion);
                }
            }
        }
    }

    const bool isNull = isNullPointerConstant(expr);
    ast::Expr* value = targetClass == TypeClass::VaList ? vaListValue(expr) : rvalue(expr);
    const ast::QualType source = value->type();
    const TypeClass sourceClass = classify(context_, source);
    if (isArithmeticClass(targetClass) && isArithmeticClass(sourceClass)) {
        return convert(value, target);
    }
    if (targetClass == TypeClass::Bool && sourceClass == TypeClass::Pointer) {
        return convert(value, target);
    }
    if ((targetClass == TypeClass::Record || targetClass == TypeClass::VaList || targetClass == TypeClass::Complex)
        && compatibleUnqualified(context_, source, target)) {
        return value;
    }
    if (targetClass == TypeClass::Pointer) {
        if (isNull) {
            return implicitCast(value, target, ast::CastKind::NullToPointer);
        }
        if (sourceClass == TypeClass::Pointer) {
            const ast::QualType targetPointee = context_.canonical(pointeeOf(target));
            const ast::QualType sourcePointee = context_.canonical(pointeeOf(source));
            const bool eitherVoid = classify(context_, targetPointee) == TypeClass::Void
                                    || classify(context_, sourcePointee) == TypeClass::Void;
            if (compatibleUnqualified(context_, targetPointee, sourcePointee) || eitherVoid) {
                if (!qualifiersInclude(targetPointee.qualifiers(), sourcePointee.qualifiers())) {
                    const std::string problem =
                        std::format("discards '{}' qualifier from pointer target type",
                                    droppedQualifier(targetPointee.qualifiers(), sourcePointee.qualifiers()));
                    switch (context.kind) {
                    case AssignmentContext::Kind::Assignment:
                        warning(context.location, "assignment " + problem);
                        break;
                    case AssignmentContext::Kind::Initialization:
                        warning(context.location, "initialization " + problem);
                        break;
                    case AssignmentContext::Kind::Return:
                        warning(context.location, "return " + problem);
                        break;
                    case AssignmentContext::Kind::Argument:
                        warnPointerConversion(problem, source, target, context);
                        break;
                    }
                }
                return convert(value, target);
            }
            // GCC accepts pointers to integer types that differ only in signedness silently (-Wpointer-sign is off).
            const bool differInSignOnly = isIntegerClass(classify(context_, targetPointee))
                                          && isIntegerClass(classify(context_, sourcePointee))
                                          && sizeOf(context_, targetPointee) == sizeOf(context_, sourcePointee);
            if (!differInSignOnly) {
                warnIncompatiblePointers(source, target, context);
            }
            return implicitCast(value, target, ast::CastKind::BitCast);
        }
        if (isIntegerClass(sourceClass)) {
            warnPointerConversion("makes pointer from integer without a cast", source, target, context);
            return implicitCast(value, target, ast::CastKind::IntegralToPointer);
        }
    }
    if (targetClass == TypeClass::Integer && sourceClass == TypeClass::Pointer) {
        warnPointerConversion("makes integer from pointer without a cast", source, target, context);
        return implicitCast(value, target, ast::CastKind::PointerToIntegral);
    }
    if (targetClass == TypeClass::Complex || sourceClass == TypeClass::Complex) {
        error(context.location, "complex arithmetic is not supported");
    }
    switch (context.kind) {
    case AssignmentContext::Kind::Assignment:
        error(context.location, std::format("incompatible types when assigning to type '{}' from type '{}'",
                                            typeName(target), typeName(source)));
    case AssignmentContext::Kind::Initialization:
        error(context.location, std::format("incompatible types when initializing type '{}' using type '{}'",
                                            typeName(target), typeName(source)));
    case AssignmentContext::Kind::Argument:
        error(context.location,
              std::format("incompatible type for argument {} of '{}'", context.argumentIndex,
                          context.function != nullptr ? context.function->name().text() : "the called function"));
    case AssignmentContext::Kind::Return:
        error(context.location, std::format("incompatible types when returning type '{}' but '{}' was expected",
                                            typeName(source), typeName(target)));
    }
    NITWIT_UNREACHABLE("unknown assignment context");
}

} // namespace nitwit::sema::detail
