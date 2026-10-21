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
// assignments, increments and statement expressions.
const ast::FieldDecl* bitFieldOf(const ast::Expr* expr) {
    while (true) {
        expr = ast::ignoreParens(expr);
        if (const auto* statement = ast::dyn_cast<ast::StatementExpr>(expr)) {
            const ast::ExpressionStmt* last = statement->valueStatement();
            if (last == nullptr) {
                return nullptr;
            }
            expr = last->expr();
            continue;
        }
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

struct SplitPointee {
    ast::QualType type;         // canonical, without the qualifiers below
    ast::Qualifiers qualifiers; // of the pointee, or of the elements of an array pointee
};

// A pointer target for the assignment rules. GCC applies C23's rule that an array type has the qualifiers of its
// elements (C23 6.2.5p28) in all modes, warning only with -pedantic, so `float (*)[5]` converts to
// `const float (*)[5]` silently and back with a warning about the discarded qualifier.
SplitPointee splitPointee(ast::AstContext& context, ast::QualType pointee) {
    pointee = context.canonical(pointee);
    const ast::ArrayType* array = arrayOf(pointee);
    if (array == nullptr || array->sizeKind() == ast::ArrayType::SizeKind::Variable) {
        return {.type = pointee.unqualified(), .qualifiers = pointee.qualifiers()};
    }
    const SplitPointee element = splitPointee(context, array->element());
    const ast::Qualifiers qualifiers = pointee.qualifiers() | element.qualifiers;
    if (element.qualifiers.empty()) {
        return {.type = pointee.unqualified(), .qualifiers = qualifiers};
    }
    const ast::QualType stripped = array->sizeKind() == ast::ArrayType::SizeKind::Constant
                                       ? context.constantArrayOf(element.type, array->size())
                                       : context.arrayOf(element.type, nullptr);
    return {.type = stripped, .qualifiers = qualifiers};
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

// GCC's wording of the diagnostics of a conversion as if by assignment, by where it happens.
Sema::ConversionWording Sema::conversionWording(ast::QualType from, ast::QualType to,
                                                const AssignmentContext& context) {
    const std::string source = typeName(from);
    const std::string target = typeName(to);
    switch (context.kind) {
    case AssignmentContext::Kind::Assignment:
        return {.subject = std::format("assignment to '{}' from '{}'", target, source),
                .action = "assignment",
                .incompatiblePointers =
                    std::format("assignment to '{}' from incompatible pointer type '{}'", target, source),
                .incompatibleTypes =
                    std::format("incompatible types when assigning to type '{}' from type '{}'", target, source)};
    case AssignmentContext::Kind::Initialization:
        return {.subject = std::format("initialization of '{}' from '{}'", target, source),
                .action = "initialization",
                .incompatiblePointers =
                    std::format("initialization of '{}' from incompatible pointer type '{}'", target, source),
                .incompatibleTypes =
                    std::format("incompatible types when initializing type '{}' using type '{}'", target, source)};
    case AssignmentContext::Kind::Argument: {
        const std::string argument =
            std::format("passing argument {} of '{}'", context.argumentIndex,
                        context.function != nullptr ? context.function->name().text() : "the called function");
        return {.subject = argument,
                .action = argument,
                .incompatiblePointers = argument + " from incompatible pointer type",
                .incompatibleTypes =
                    std::format("incompatible type for argument {} of '{}'", context.argumentIndex,
                                context.function != nullptr ? context.function->name().text() : "the called function")};
    }
    case AssignmentContext::Kind::Return:
        return {.subject = std::format("returning '{}' from a function with return type '{}'", source, target),
                .action = "return",
                .incompatiblePointers =
                    std::format("returning '{}' from a function with incompatible return type '{}'", source, target),
                .incompatibleTypes =
                    std::format("incompatible types when returning type '{}' but '{}' was expected", source, target)};
    }
    NITWIT_UNREACHABLE("unknown assignment context");
}

// The conversion of an argument to a GNU transparent union parameter, as GCC's convert_for_assignment does it: an
// argument of a member's type, else a pointer that converts to a pointer member without a diagnostic (void pointers,
// added qualifiers), else a null pointer constant for a pointer member is converted to that member's type and passed
// as the union; the ToUnion cast's operand has the member's type. Null if the argument fits no member.
ast::Expr* Sema::transparentUnionArgument(ast::Expr* expr, ast::QualType target) {
    ast::RecordDecl* record = recordOf(target);
    if (!record->isUnion() || !isTransparentUnion(target) || compatibleUnqualified(context_, expr->type(), target)) {
        return nullptr;
    }
    const bool isNull = isNullPointerConstant(expr);
    ast::Expr* value = rvalue(expr);
    const auto asMember = [&](ast::QualType memberType, ast::CastKind kind) {
        return implicitCast(implicitCast(value, memberType, kind), target, ast::CastKind::ToUnion);
    };
    for (const ast::FieldDecl* member : record->fields()) {
        const ast::QualType memberType = unqualified(context_, member->type());
        if (compatibleUnqualified(context_, memberType, value->type())) {
            return implicitCast(convert(value, memberType), target, ast::CastKind::ToUnion);
        }
    }
    if (isPointer(context_, value->type())) {
        const SplitPointee source = splitPointee(context_, pointeeOf(value->type()));
        for (const ast::FieldDecl* member : record->fields()) {
            const ast::QualType memberType = unqualified(context_, member->type());
            if (!isPointer(context_, memberType)) {
                continue;
            }
            const SplitPointee targetPointee = splitPointee(context_, pointeeOf(memberType));
            const bool eitherVoid = classify(context_, targetPointee.type) == TypeClass::Void
                                    || classify(context_, source.type) == TypeClass::Void;
            if ((eitherVoid || compatibleUnqualified(context_, targetPointee.type, source.type))
                && qualifiersInclude(targetPointee.qualifiers, source.qualifiers)) {
                return asMember(memberType, castKind(value, value->type(), memberType));
            }
        }
    }
    if (isNull) {
        for (const ast::FieldDecl* member : record->fields()) {
            const ast::QualType memberType = unqualified(context_, member->type());
            if (isPointer(context_, memberType)) {
                return asMember(memberType, ast::CastKind::NullToPointer);
            }
        }
    }
    return nullptr;
}

// C17 6.5.16.1p1 with GCC's extensions: integer/pointer conversions and incompatible pointer types are accepted with
// a warning (GCC 14 rejects them unless -fpermissive, but older sv-benchmarks rely on them).
ast::Expr* Sema::convertForAssignment(ast::Expr* expr, ast::QualType type, const AssignmentContext& context) {
    const ast::QualType target = unqualified(context_, type);
    const TypeClass targetClass = classify(context_, target);
    if (targetClass == TypeClass::Record && context.kind == AssignmentContext::Kind::Argument) {
        if (ast::Expr* argument = transparentUnionArgument(expr, target)) {
            return argument;
        }
    }

    const bool isNull = targetClass == TypeClass::Pointer && isNullPointerConstant(expr);
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
            const auto [targetPointee, targetQualifiers] = splitPointee(context_, pointeeOf(target));
            const auto [sourcePointee, sourceQualifiers] = splitPointee(context_, pointeeOf(source));
            const bool eitherVoid = classify(context_, targetPointee) == TypeClass::Void
                                    || classify(context_, sourcePointee) == TypeClass::Void;
            if (compatibleUnqualified(context_, targetPointee, sourcePointee) || eitherVoid) {
                if (!qualifiersInclude(targetQualifiers, sourceQualifiers)) {
                    warning(context.location, std::format("{} discards '{}' qualifier from pointer target type",
                                                          conversionWording(source, target, context).action,
                                                          droppedQualifier(targetQualifiers, sourceQualifiers)));
                }
                return convert(value, target);
            }
            // GCC accepts pointers to integer types that differ only in signedness silently (-Wpointer-sign is off).
            const bool differInSignOnly = isIntegerClass(classify(context_, targetPointee))
                                          && isIntegerClass(classify(context_, sourcePointee))
                                          && sizeOf(context_, targetPointee) == sizeOf(context_, sourcePointee);
            if (!differInSignOnly) {
                warning(context.location, conversionWording(source, target, context).incompatiblePointers);
            }
            return implicitCast(value, target, ast::CastKind::BitCast);
        }
        if (isIntegerClass(sourceClass)) {
            warning(context.location,
                    conversionWording(source, target, context).subject + " makes pointer from integer without a cast");
            return implicitCast(value, target, ast::CastKind::IntegralToPointer);
        }
    }
    if (targetClass == TypeClass::Integer && sourceClass == TypeClass::Pointer) {
        warning(context.location,
                conversionWording(source, target, context).subject + " makes integer from pointer without a cast");
        return implicitCast(value, target, ast::CastKind::PointerToIntegral);
    }
    if (targetClass == TypeClass::Complex || sourceClass == TypeClass::Complex) {
        error(context.location, "complex arithmetic is not supported");
    }
    error(context.location, conversionWording(source, target, context).incompatibleTypes);
}

} // namespace nitwit::sema::detail
