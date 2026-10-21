// Expressions (C17 6.5.1-6.5.4) other than the operators of operators.cpp: primary expressions, calls, member
// access, subscripts, casts, compound literals, sizeof, and the GNU builtins with their own syntax.

#include <format>
#include <limits>
#include <string>

#include "ast/casting.hpp"
#include "sema/constant.hpp"
#include "sema/sema_impl.hpp"
#include "sema/types.hpp"
#include "support/assert.hpp"

namespace nitwit::sema::detail {

namespace {

using ast::BuiltinKind;

// The value range of the integer types in the order C17 6.4.4.1p5 tries them.
bool fits(ast::AstContext& context, std::uint64_t value, BuiltinKind kind) {
    const value::IntegerFormat format = integerFormat(context, context.builtin(kind));
    const unsigned valueBits = format.isSigned ? format.bits - 1u : format.bits;
    return valueBits >= 64 || value < (std::uint64_t{1} << valueBits);
}

ast::QualType characterType(ast::AstContext& context, lex::CharEncoding encoding) {
    switch (encoding) {
    case lex::CharEncoding::Plain:
    case lex::CharEncoding::Wide: // wchar_t is int on x86 Linux
        return context.builtin(BuiltinKind::Int);
    case lex::CharEncoding::Utf8:
        return context.builtin(BuiltinKind::UnsignedChar);
    case lex::CharEncoding::Utf16:
        return context.builtin(BuiltinKind::UnsignedShort);
    case lex::CharEncoding::Utf32:
        return context.builtin(BuiltinKind::UnsignedInt);
    }
    NITWIT_UNREACHABLE("unknown character encoding");
}

ast::QualType stringElementType(ast::AstContext& context, lex::CharEncoding encoding) {
    switch (encoding) {
    case lex::CharEncoding::Plain:
    case lex::CharEncoding::Utf8:
        return context.builtin(BuiltinKind::Char);
    case lex::CharEncoding::Wide:
        return context.builtin(BuiltinKind::Int);
    case lex::CharEncoding::Utf16:
        return context.builtin(BuiltinKind::UnsignedShort);
    case lex::CharEncoding::Utf32:
        return context.builtin(BuiltinKind::UnsignedInt);
    }
    NITWIT_UNREACHABLE("unknown character encoding");
}

ast::QualType floatingLiteralType(ast::AstContext& context, lex::FloatingSuffix suffix) {
    switch (suffix) {
    case lex::FloatingSuffix::F:
    case lex::FloatingSuffix::F32:
        return context.builtin(BuiltinKind::Float);
    case lex::FloatingSuffix::None:
    case lex::FloatingSuffix::F64:
    case lex::FloatingSuffix::F32x:
        return context.builtin(BuiltinKind::Double);
    case lex::FloatingSuffix::L:
    case lex::FloatingSuffix::F64x:
    case lex::FloatingSuffix::F128:
        return context.builtin(BuiltinKind::LongDouble);
    }
    NITWIT_UNREACHABLE("unknown floating suffix");
}

const ast::FunctionDecl* directFunction(const ast::Expr* callee) {
    const auto* reference = ast::dyn_cast<ast::DeclRefExpr>(ast::ignoreParens(callee));
    return reference != nullptr ? ast::dyn_cast<ast::FunctionDecl>(reference->decl()) : nullptr;
}

// Whether every declaration of a function is implicit, so that a builtin of that name keeps its builtin meaning.
bool isOnlyImplicitlyDeclared(const ast::FunctionDecl* function) {
    for (; function != nullptr; function = function->previous()) {
        if (!function->isImplicit()) {
            return false;
        }
    }
    return true;
}

} // namespace

Int128 Sema::integerConstant(ast::Expr* expr, std::string_view what) {
    const std::optional<Int128> value = evaluateIntegerConstant(context_, expr);
    if (!value) {
        error(expr->location(), std::format("{} is not an integer constant", what));
    }
    return *value;
}

void Sema::expr(ast::Expr* expr) {
    if (!expr->type().isNull()) {
        return;
    }
    switch (expr->kind()) {
    case ast::ExprKind::IntegerLiteral:
        return integerLiteral(ast::cast<ast::IntegerLiteralExpr>(expr));
    case ast::ExprKind::FloatingLiteral:
        return expr->setType(floatingLiteralType(context_, ast::cast<ast::FloatingLiteralExpr>(expr)->value().suffix),
                             ast::ValueCategory::RValue);
    case ast::ExprKind::CharacterLiteral:
        return expr->setType(characterType(context_, ast::cast<ast::CharacterLiteralExpr>(expr)->value().encoding),
                             ast::ValueCategory::RValue);
    case ast::ExprKind::StringLiteral: {
        // C17 6.4.5p6: an array of the code units plus the terminating zero; an lvalue.
        const auto* literal = ast::cast<ast::StringLiteralExpr>(expr);
        return expr->setType(
            context_.constantArrayOf(stringElementType(context_, literal->encoding()), literal->codeUnits().size() + 1),
            ast::ValueCategory::LValue);
    }
    case ast::ExprKind::DeclRef:
        return declRef(ast::cast<ast::DeclRefExpr>(expr));
    case ast::ExprKind::Paren: {
        ast::Expr* inner = ast::cast<ast::ParenExpr>(expr)->inner();
        this->expr(inner);
        return expr->setType(inner->type(), inner->category());
    }
    case ast::ExprKind::Unary:
        return unary(ast::cast<ast::UnaryExpr>(expr));
    case ast::ExprKind::Binary:
        return binary(ast::cast<ast::BinaryExpr>(expr));
    case ast::ExprKind::Conditional:
        return conditional(ast::cast<ast::ConditionalExpr>(expr));
    case ast::ExprKind::Call:
        return call(ast::cast<ast::CallExpr>(expr));
    case ast::ExprKind::Member:
        return member(ast::cast<ast::MemberExpr>(expr));
    case ast::ExprKind::ArraySubscript:
        return subscript(ast::cast<ast::ArraySubscriptExpr>(expr));
    case ast::ExprKind::Cast:
        return explicitCast(ast::cast<ast::CastExpr>(expr));
    case ast::ExprKind::CompoundLiteral:
        return compoundLiteral(ast::cast<ast::CompoundLiteralExpr>(expr));
    case ast::ExprKind::InitList:
    case ast::ExprKind::DesignatedInit:
        error(expr->location(), "a braced initializer is not allowed here");
    case ast::ExprKind::SizeOfAlignOf:
        return sizeOfAlignOf(ast::cast<ast::SizeOfAlignOfExpr>(expr));
    case ast::ExprKind::OffsetOf:
        return offsetOf(ast::cast<ast::OffsetOfExpr>(expr));
    case ast::ExprKind::VaArg:
        return vaArg(ast::cast<ast::VaArgExpr>(expr));
    case ast::ExprKind::StatementExpr:
        return statementExpr(ast::cast<ast::StatementExpr>(expr));
    case ast::ExprKind::GenericSelection:
        return genericSelection(ast::cast<ast::GenericSelectionExpr>(expr));
    case ast::ExprKind::ChooseExpr:
        return chooseExpr(ast::cast<ast::ChooseExpr>(expr));
    case ast::ExprKind::TypesCompatible: {
        const auto* compatible = ast::cast<ast::TypesCompatibleExpr>(expr);
        resolveType(compatible->first(), expr->location());
        resolveType(compatible->second(), expr->location());
        return expr->setType(context_.builtin(BuiltinKind::Int), ast::ValueCategory::RValue);
    }
    case ast::ExprKind::Result:
        // The return value of the call a constraint observes.
        if (resultType_.isNull()) {
            error(expr->location(), "'\\result' is only allowed in constraints at the return of a call");
        }
        return expr->setType(unqualified(context_, resultType_), ast::ValueCategory::RValue);
    case ast::ExprKind::ImplicitCast:
    case ast::ExprKind::ImplicitZero:
        break; // created by sema with their types
    }
    NITWIT_UNREACHABLE("expression without a type");
}

// C17 6.4.4.1p5: the first type of the list for the literal's base and suffix that can represent its value. A decimal
// constant without suffix u that fits no signed type is unsigned, as in GCC.
void Sema::integerLiteral(ast::IntegerLiteralExpr* literal) {
    const lex::IntegerLiteral& value = literal->value();
    std::vector<BuiltinKind> candidates;
    if (value.hasUnsignedSuffix) {
        candidates = {BuiltinKind::UnsignedInt, BuiltinKind::UnsignedLong, BuiltinKind::UnsignedLongLong};
    } else if (value.isDecimal) {
        candidates = {BuiltinKind::Int, BuiltinKind::Long, BuiltinKind::LongLong};
    } else {
        candidates = {BuiltinKind::Int,          BuiltinKind::UnsignedInt, BuiltinKind::Long,
                      BuiltinKind::UnsignedLong, BuiltinKind::LongLong,    BuiltinKind::UnsignedLongLong};
    }
    for (const BuiltinKind kind : candidates) {
        const int rank = integerRank(kind);
        const int minimumRank = value.longCount == 0   ? integerRank(BuiltinKind::Int)
                                : value.longCount == 1 ? integerRank(BuiltinKind::Long)
                                                       : integerRank(BuiltinKind::LongLong);
        if (rank >= minimumRank && fits(context_, value.value, kind)) {
            return literal->setType(context_.builtin(kind), ast::ValueCategory::RValue);
        }
    }
    warning(literal->location(), "integer constant is so large that it is unsigned");
    const BuiltinKind kind = fits(context_, value.value, BuiltinKind::UnsignedLong) && value.longCount < 2
                                 ? BuiltinKind::UnsignedLong
                                 : BuiltinKind::UnsignedLongLong;
    literal->setType(context_.builtin(kind), ast::ValueCategory::RValue);
}

void Sema::declRef(ast::DeclRefExpr* reference) {
    ast::ValueDecl* decl = reference->decl();
    if (decl == nullptr) {
        error(reference->location(), std::format("'{}' undeclared", reference->name().text()));
    }
    switch (decl->kind()) {
    case ast::DeclKind::Var:
        resolveType(decl->type(), reference->location());
        return reference->setType(decl->type(), ast::ValueCategory::LValue);
    case ast::DeclKind::Function:
        // A function designator; like Clang, NITWIT treats it as an lvalue that decays to the function's address.
        return reference->setType(decl->type(), ast::ValueCategory::LValue);
    case ast::DeclKind::EnumConstant:
        if (decl->type().isNull()) {
            error(reference->location(), std::format("enumeration constant '{}' used before its enumeration is "
                                                     "complete",
                                                     decl->name().text()));
        }
        return reference->setType(decl->type(), ast::ValueCategory::RValue);
    default:
        break;
    }
    NITWIT_UNREACHABLE("reference to a declaration without a value");
}

// C17 6.5.2.2: the callee is converted to a pointer to a function; arguments are converted as if by assignment to
// the parameter types of a prototype, and get the default argument promotions otherwise and after an ellipsis.
void Sema::call(ast::CallExpr* call) {
    ast::Expr* callee = call->callee();
    expr(callee);
    const ast::FunctionDecl* direct = directFunction(callee);
    if (direct != nullptr && (direct->name().text().starts_with("__builtin_") || isOnlyImplicitlyDeclared(direct))) {
        if (const std::optional<BuiltinInfo> builtin = lookupBuiltin(context_, direct->name().text());
            builtin && builtin->generic != GenericBuiltin::None) {
            return genericBuiltinCall(call, builtin->generic);
        }
    }
    ast::Expr* calleeValue = rvalue(callee);
    call->setCallee(calleeValue);
    const ast::FunctionType* function = nullptr;
    if (classify(context_, calleeValue->type()) == TypeClass::Pointer) {
        function = functionOf(pointeeOf(calleeValue->type()));
    }
    if (function == nullptr) {
        error(call->location(), "called object is not a function or function pointer");
    }
    const std::string name = direct != nullptr ? std::string(direct->name().text()) : std::string("function");
    const std::span<ast::Expr*> arguments = call->arguments();
    const std::span<const ast::QualType> parameters = function->parameters();
    if (function->hasPrototype()) {
        if (arguments.size() < parameters.size()) {
            error(call->rightParenLocation(), std::format("too few arguments to function '{}'", name));
        }
        if (arguments.size() > parameters.size() && !function->isVariadic()) {
            error(arguments[parameters.size()]->location(), std::format("too many arguments to function '{}'", name));
        }
    }
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        ast::Expr* argument = arguments[i];
        expr(argument);
        if (classify(context_, argument->type()) == TypeClass::Void) {
            error(argument->location(), "invalid use of void expression");
        }
        if (function->hasPrototype() && i < parameters.size()) {
            const AssignmentContext context{.kind = AssignmentContext::Kind::Argument,
                                            .location = argument->location(),
                                            .argumentIndex = i + 1,
                                            .function = direct};
            arguments[i] = convertForAssignment(argument, parameters[i], context);
        } else {
            if (classify(context_, argument->type()) == TypeClass::Complex) {
                error(argument->location(), "complex arithmetic is not supported");
            }
            arguments[i] = defaultArgumentPromotion(argument);
        }
    }
    const ast::QualType returnType = function->returnType();
    if (classify(context_, returnType) == TypeClass::Record) {
        requireComplete(returnType, call->location(),
                        std::format("calling a function with incomplete return type '{}'", typeName(returnType)));
    }
    call->setType(unqualified(context_, returnType), ast::ValueCategory::RValue);
}

void Sema::genericBuiltinCall(ast::CallExpr* call, GenericBuiltin builtin) {
    const std::string name(directFunction(call->callee())->name().text());
    call->setCallee(rvalue(call->callee()));
    const std::span<ast::Expr*> arguments = call->arguments();
    const auto requireArguments = [&](std::size_t count) {
        if (arguments.size() != count) {
            error(call->location(), std::format("wrong number of arguments to function '{}'", name));
        }
        for (ast::Expr* argument : arguments) {
            expr(argument);
        }
    };
    // The va_list arguments that a builtin modifies stay lvalues; the interpreter writes through them. CIL passes the
    // address of a va_list as `(__va_list_tag *)&ap` or `&ap` instead (see isVaListAddress); that pointer is an rvalue.
    const auto requireVaList = [&](std::size_t index) {
        ast::Expr* argument = arguments[index];
        if (isVaListAddress(valueType(argument))) {
            arguments[index] = rvalue(argument);
            return;
        }
        if (classify(context_, argument->type()) != TypeClass::VaList || !argument->isLValue()) {
            error(argument->location(), std::format("argument of '{}' is not an lvalue of type 'va_list'", name));
        }
    };
    const ast::QualType intType = context_.builtin(BuiltinKind::Int);
    switch (builtin) {
    case GenericBuiltin::Overflow: {
        requireArguments(3);
        for (std::size_t i = 0; i < 2; ++i) {
            arguments[i] = rvalue(arguments[i]);
            if (!isIntegerClass(classify(context_, arguments[i]->type()))) {
                error(arguments[i]->location(),
                      std::format("argument {} in call to function '{}' does not have integral type", i + 1, name));
            }
        }
        arguments[2] = rvalue(arguments[2]);
        const ast::QualType result = arguments[2]->type();
        if (classify(context_, result) != TypeClass::Pointer
            || classify(context_, pointeeOf(result)) != TypeClass::Integer
            || isConstQualified(context_, pointeeOf(result))) {
            error(arguments[2]->location(),
                  std::format("argument 3 in call to function '{}' does not have pointer to integral type", name));
        }
        return call->setType(context_.builtin(BuiltinKind::Bool), ast::ValueCategory::RValue);
    }
    case GenericBuiltin::ConstantP:
        requireArguments(1); // the argument is not evaluated
        return call->setType(intType, ast::ValueCategory::RValue);
    case GenericBuiltin::FloatClassify:
        requireArguments(1);
        arguments[0] = rvalue(arguments[0]);
        if (classify(context_, arguments[0]->type()) != TypeClass::Floating) {
            error(arguments[0]->location(), std::format("non-floating-point argument in call to function '{}'", name));
        }
        return call->setType(intType, ast::ValueCategory::RValue);
    case GenericBuiltin::FloatCompare:
        requireArguments(2);
        if (!isArithmeticClass(classify(context_, arguments[0]->type()))
            || !isArithmeticClass(classify(context_, arguments[1]->type()))) {
            error(call->location(), std::format("non-floating-point arguments in call to function '{}'", name));
        }
        (void)usualArithmeticConversions(arguments[0], arguments[1]);
        return call->setType(intType, ast::ValueCategory::RValue);
    case GenericBuiltin::VaStart:
        requireArguments(2);
        requireVaList(0);
        if (!function_ || !functionOf(function_->function->type())->isVariadic()) {
            error(call->location(), "'va_start' used in function with fixed arguments");
        }
        return call->setType(context_.builtin(BuiltinKind::Void), ast::ValueCategory::RValue);
    case GenericBuiltin::VaEnd:
        requireArguments(1);
        requireVaList(0);
        return call->setType(context_.builtin(BuiltinKind::Void), ast::ValueCategory::RValue);
    case GenericBuiltin::VaCopy:
        requireArguments(2);
        requireVaList(0);
        arguments[1] = vaListValue(arguments[1]);
        if (classify(context_, arguments[1]->type()) != TypeClass::VaList) {
            error(arguments[1]->location(), std::format("argument 2 of '{}' is not of type 'va_list'", name));
        }
        return call->setType(context_.builtin(BuiltinKind::Void), ast::ValueCategory::RValue);
    case GenericBuiltin::None:
        break;
    }
    NITWIT_UNREACHABLE("not a generic builtin");
}

// C17 6.5.2.3: `.` on a struct or union, `->` on a pointer to one. A member of an anonymous struct or union is reached
// through implicit member expressions for the anonymous members, so that field() is a direct member of the base.
void Sema::member(ast::MemberExpr* member) {
    ast::Expr* base = member->base();
    expr(base);
    ast::QualType recordType;
    bool isLValue = true;
    if (member->isArrow()) {
        base = rvalue(base);
        member->setBase(base);
        if (classify(context_, base->type()) != TypeClass::Pointer) {
            error(member->location(), std::format("invalid type argument of '->' (have '{}')", typeName(base->type())));
        }
        recordType = desugar(pointeeOf(base->type()));
    } else {
        recordType = desugar(base->type());
        isLValue = base->isLValue();
    }
    ast::RecordDecl* record = recordOf(recordType);
    if (record == nullptr) {
        error(member->memberLocation(),
              std::format("request for member '{}' in something not a structure or union", member->member().text()));
    }
    requireComplete(recordType, member->memberLocation(),
                    std::format("invalid use of undefined type '{}'", typeName(recordType)));
    const std::vector<ast::FieldDecl*> path = findField(record, member->member());
    if (path.empty()) {
        error(member->memberLocation(), std::format("'{}' has no member named '{}'", typeName(recordType.unqualified()),
                                                    member->member().text()));
    }
    const auto category = isLValue ? ast::ValueCategory::LValue : ast::ValueCategory::RValue;
    ast::Qualifiers qualifiers = recordType.qualifiers();
    for (std::size_t i = 0; i + 1 < path.size(); ++i) {
        auto* anonymous = context_.create<ast::MemberExpr>(member->range(), base, Symbol(), i == 0 && member->isArrow(),
                                                           member->memberLocation());
        anonymous->setField(path[i]);
        const ast::QualType type = path[i]->type().withQualifiers(qualifiers);
        anonymous->setType(type, category);
        qualifiers = desugar(type).qualifiers();
        base = anonymous;
    }
    if (path.size() > 1) {
        member->setBase(base);
        member->setArrow(false);
    }
    member->setField(path.back());
    member->setType(path.back()->type().withQualifiers(qualifiers), category);
}

// C17 6.5.2.1: one operand is a pointer to a complete object type, the other an integer; `i[a]` is swapped.
void Sema::subscript(ast::ArraySubscriptExpr* subscript) {
    expr(subscript->base());
    expr(subscript->index());
    ast::Expr* base = rvalue(subscript->base());
    ast::Expr* index = subscript->index();
    if (classify(context_, base->type()) != TypeClass::Pointer) {
        if (classify(context_, valueType(index)) != TypeClass::Pointer) {
            error(subscript->location(), "subscripted value is neither array nor pointer");
        }
        index = subscript->base();
        base = rvalue(subscript->index());
    }
    if (!isIntegerClass(classify(context_, index->type()))) {
        error(index->location(), "array subscript is not an integer");
    }
    index = promote(index);
    const ast::QualType element = pointeeOf(base->type());
    const TypeClass elementClass = classify(context_, element);
    if (elementClass != TypeClass::Void && elementClass != TypeClass::Function && !isVariablySized(element)) {
        requireComplete(element, subscript->location(),
                        std::format("invalid use of undefined type '{}'", typeName(element)));
    }
    subscript->setOperands(base, index);
    subscript->setType(element,
                       elementClass == TypeClass::Void ? ast::ValueCategory::RValue : ast::ValueCategory::LValue);
}

// C17 6.5.4: a cast to void, or between scalar types; GNU: to a union type from the type of one of its members.
void Sema::explicitCast(ast::CastExpr* castExpr) {
    resolveType(castExpr->writtenType(), castExpr->location());
    ast::Expr* operand = castExpr->operand();
    expr(operand);
    const ast::QualType target = unqualified(context_, castExpr->writtenType());
    const TypeClass targetClass = classify(context_, target);
    if (targetClass == TypeClass::Void) {
        castExpr->setCastKind(ast::CastKind::ToVoid);
        return castExpr->setType(target, ast::ValueCategory::RValue);
    }
    ast::Expr* value = rvalue(operand);
    const TypeClass sourceClass = classify(context_, value->type());
    if (targetClass == TypeClass::Record) {
        ast::RecordDecl* record = recordOf(target);
        requireComplete(target, castExpr->location(), "conversion to incomplete type");
        bool isValid = compatibleUnqualified(context_, value->type(), target);
        for (const ast::FieldDecl* field : record->fields()) {
            isValid = isValid || (record->isUnion() && compatibleUnqualified(context_, field->type(), value->type()));
        }
        if (!isValid) {
            error(castExpr->location(), record->isUnion() ? "cast to union type from type not present in union"
                                                          : "conversion to non-scalar type requested");
        }
    } else if (!isScalarClass(targetClass)) {
        error(castExpr->location(), targetClass == TypeClass::Complex ? "complex arithmetic is not supported"
                                                                      : "conversion to non-scalar type requested");
    } else if (!isScalarClass(sourceClass)) {
        error(castExpr->location(), sourceClass == TypeClass::Complex
                                        ? "complex arithmetic is not supported"
                                        : "aggregate value used where a scalar was expected");
    } else if ((targetClass == TypeClass::Pointer && sourceClass == TypeClass::Floating)
               || (targetClass == TypeClass::Floating && sourceClass == TypeClass::Pointer)) {
        error(castExpr->location(), "cannot convert between pointer and floating type");
    }
    castExpr->setOperand(value);
    castExpr->setCastKind(castKind(operand, value->type(), target));
    castExpr->setType(target, ast::ValueCategory::RValue);
}

// C17 6.5.2.5: an lvalue of the type name, with an initializer like that of an object.
void Sema::compoundLiteral(ast::CompoundLiteralExpr* literal) {
    resolveType(literal->writtenType(), literal->location());
    ast::QualType type = literal->writtenType();
    if (isVariablySized(type)) {
        error(literal->location(), "compound literal has variable size");
    }
    ast::InitListExpr* list = literal->initializer();
    if (isScalarClass(classify(context_, type))) {
        // The InitListExpr stays: it holds the converted value, or nothing for `{}` (zero).
        std::vector<ast::Expr*> elements;
        if (!list->elements().empty()) {
            ast::QualType scalar = type;
            elements.push_back(initializer(scalar, list->elements().front()));
        }
        list->setElements(context_.copy<ast::Expr*>(elements));
        list->setSemanticForm();
        list->setType(unqualified(context_, type), ast::ValueCategory::RValue);
    } else {
        ast::Expr* result = initializer(type, list);
        NITWIT_ASSERT(result == list || ast::isa<ast::StringLiteralExpr>(result),
                      "the initializer of a compound literal stays its InitListExpr");
        if (result != list) {
            list->setElements(context_.copy<ast::Expr*>(std::vector<ast::Expr*>{result}));
            list->setSemanticForm();
            list->setType(type, ast::ValueCategory::RValue);
        }
    }
    literal->setType(type, ast::ValueCategory::LValue);
}

void Sema::sizeOfAlignOf(ast::SizeOfAlignOfExpr* sizeOf) {
    ast::QualType type;
    const std::string_view name = sizeOf->op() == ast::SizeOfAlignOfExpr::Operator::SizeOf ? "sizeof" : "__alignof__";
    if (ast::Expr* operand = sizeOf->operand()) {
        expr(operand);
        type = operand->type();
        if (const auto* member = ast::dyn_cast<ast::MemberExpr>(ast::ignoreParens(operand));
            member != nullptr && member->field()->isBitField()) {
            error(operand->location(), std::format("'{}' applied to a bit-field", name));
        }
    } else {
        type = sizeOf->typeOperand();
        resolveType(type, sizeOf->location());
    }
    const TypeClass typeClass = classify(context_, type);
    if (typeClass != TypeClass::Function && typeClass != TypeClass::Void && !isVariablySized(type)) {
        requireComplete(type, sizeOf->location(),
                        std::format("invalid application of '{}' to incomplete type '{}'", name, typeName(type)));
    }
    sizeOf->setType(sizeType(context_), ast::ValueCategory::RValue);
}

void Sema::offsetOf(ast::OffsetOfExpr* offsetOf) {
    resolveType(offsetOf->recordType(), offsetOf->location());
    ast::QualType current = offsetOf->recordType();
    for (ast::OffsetOfExpr::Component& component : offsetOf->components()) {
        if (component.isField) {
            ast::RecordDecl* record = recordOf(current);
            if (record == nullptr) {
                error(offsetOf->location(), "'__builtin_offsetof' of a member of a non-structure type");
            }
            requireComplete(current, offsetOf->location(),
                            std::format("invalid use of undefined type '{}'", typeName(current)));
            const std::vector<ast::FieldDecl*> path = findField(record, component.fieldName);
            if (path.empty()) {
                error(offsetOf->location(),
                      std::format("'{}' has no member named '{}'", typeName(current), component.fieldName.text()));
            }
            if (path.back()->isBitField()) {
                error(offsetOf->location(), "attempt to take address of bit-field structure member");
            }
            component.field = path.back();
            current = path.back()->type();
            continue;
        }
        const ast::ArrayType* array = arrayOf(current);
        if (array == nullptr) {
            error(offsetOf->location(), "subscripted value is not an array in '__builtin_offsetof'");
        }
        expr(component.index);
        if (!isIntegerClass(classify(context_, component.index->type()))) {
            error(component.index->location(), "array subscript is not an integer");
        }
        component.index = promote(component.index);
        current = array->element();
    }
    offsetOf->setType(sizeType(context_), ast::ValueCategory::RValue);
}

void Sema::vaArg(ast::VaArgExpr* vaArg) {
    ast::Expr* list = vaArg->list();
    expr(list);
    if (classify(context_, list->type()) != TypeClass::VaList || !list->isLValue()) {
        error(list->location(), "first argument to 'va_arg' not of type 'va_list'");
    }
    resolveType(vaArg->argumentType(), vaArg->location());
    requireComplete(vaArg->argumentType(), vaArg->location(), "second argument to 'va_arg' is of incomplete type");
    vaArg->setType(unqualified(context_, vaArg->argumentType()), ast::ValueCategory::RValue);
}

// GNU: the value of a statement expression is that of its last expression statement, after lvalue conversion.
void Sema::statementExpr(ast::StatementExpr* statementExpr) {
    if (!function_) {
        error(statementExpr->location(), "a statement expression is only allowed inside a function");
    }
    ast::CompoundStmt* body = statementExpr->body();
    stmt(body);
    ast::QualType type = context_.builtin(BuiltinKind::Void);
    if (!body->body().empty()) {
        if (auto* last = ast::dyn_cast<ast::ExpressionStmt>(body->body().back());
            last != nullptr && classify(context_, last->expr()->type()) != TypeClass::Void) {
            last->setExpr(rvalue(last->expr()));
            type = last->expr()->type();
        }
    }
    statementExpr->setType(type, ast::ValueCategory::RValue);
}

// C17 6.5.1.1: the association whose type is compatible with the controlling expression's type after lvalue
// conversion, else `default`.
void Sema::genericSelection(ast::GenericSelectionExpr* generic) {
    ast::Expr* controlling = generic->controlling();
    expr(controlling);
    const ast::QualType controllingType = valueType(controlling);
    ast::Expr* selected = nullptr;
    ast::Expr* defaultResult = nullptr;
    for (const ast::GenericSelectionExpr::Association& association : generic->associations()) {
        expr(association.result);
        if (association.type.isNull()) {
            defaultResult = association.result;
            continue;
        }
        resolveType(association.type, association.result->location());
        if (selected == nullptr && compatible(context_, controllingType, association.type)) {
            selected = association.result;
        }
    }
    if (selected == nullptr) {
        selected = defaultResult;
    }
    if (selected == nullptr) {
        error(controlling->location(), std::format("'_Generic' selector of type '{}' is not compatible with any "
                                                   "association",
                                                   typeName(controllingType)));
    }
    generic->setSelected(selected);
    generic->setType(selected->type(), selected->category());
}

void Sema::chooseExpr(ast::ChooseExpr* choose) {
    expr(choose->condition());
    if (!isIntegerClass(classify(context_, choose->condition()->type()))) {
        error(choose->condition()->location(), "first argument to '__builtin_choose_expr' not a constant");
    }
    const Int128 value = integerConstant(choose->condition(), "first argument to '__builtin_choose_expr'");
    expr(choose->first());
    expr(choose->second());
    ast::Expr* selected = value != 0 ? choose->first() : choose->second();
    choose->setSelected(selected);
    choose->setType(selected->type(), selected->category());
}

} // namespace nitwit::sema::detail
