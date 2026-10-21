// Type queries (types.hpp) and the type rules of type_rules.hpp: classification, sizes and alignments as GCC computes
// them for i386 and x86-64, integer promotions, the usual arithmetic conversions, compatible and composite types.

#include "sema/types.hpp"

#include <algorithm>
#include <iterator>
#include <utility>
#include <vector>

#include "ast/casting.hpp"
#include "sema/type_rules.hpp"
#include "support/assert.hpp"

namespace nitwit::sema {

namespace detail {

namespace {

using ast::BuiltinKind;

// The builtin types whose representation the target defines (all but void and va_list).
constexpr std::pair<BuiltinKind, ScalarType> scalarKinds[] = {
    {BuiltinKind::Bool, ScalarType::Bool},
    {BuiltinKind::Char, ScalarType::Char},
    {BuiltinKind::SignedChar, ScalarType::SignedChar},
    {BuiltinKind::UnsignedChar, ScalarType::UnsignedChar},
    {BuiltinKind::Short, ScalarType::Short},
    {BuiltinKind::UnsignedShort, ScalarType::UnsignedShort},
    {BuiltinKind::Int, ScalarType::Int},
    {BuiltinKind::UnsignedInt, ScalarType::UnsignedInt},
    {BuiltinKind::Long, ScalarType::Long},
    {BuiltinKind::UnsignedLong, ScalarType::UnsignedLong},
    {BuiltinKind::LongLong, ScalarType::LongLong},
    {BuiltinKind::UnsignedLongLong, ScalarType::UnsignedLongLong},
    {BuiltinKind::Int128, ScalarType::Int128},
    {BuiltinKind::UnsignedInt128, ScalarType::UnsignedInt128},
    {BuiltinKind::Float, ScalarType::Float},
    {BuiltinKind::Double, ScalarType::Double},
    {BuiltinKind::LongDouble, ScalarType::LongDouble},
};

std::optional<ScalarType> scalarTypeOf(BuiltinKind kind) {
    const auto* found = std::ranges::find(scalarKinds, kind, &std::pair<BuiltinKind, ScalarType>::first);
    return found != std::end(scalarKinds) ? std::optional(found->second) : std::nullopt;
}

bool isSignedKind(BuiltinKind kind) {
    switch (kind) {
    case BuiltinKind::Char:
    case BuiltinKind::SignedChar:
    case BuiltinKind::Short:
    case BuiltinKind::Int:
    case BuiltinKind::Long:
    case BuiltinKind::LongLong:
    case BuiltinKind::Int128:
        return true;
    default:
        return false;
    }
}

BuiltinKind unsignedKindOf(BuiltinKind kind) {
    switch (kind) {
    case BuiltinKind::Char:
    case BuiltinKind::SignedChar:
        return BuiltinKind::UnsignedChar;
    case BuiltinKind::Short:
        return BuiltinKind::UnsignedShort;
    case BuiltinKind::Int:
        return BuiltinKind::UnsignedInt;
    case BuiltinKind::Long:
        return BuiltinKind::UnsignedLong;
    case BuiltinKind::LongLong:
        return BuiltinKind::UnsignedLongLong;
    case BuiltinKind::Int128:
        return BuiltinKind::UnsignedInt128;
    default:
        return kind;
    }
}

// __builtin_va_list: on i386 a `char *`, on x86-64 an array of one 24-byte struct; NITWIT treats both as opaque
// objects with that size and alignment.
std::uint64_t vaListSize(const TargetInfo& target) {
    return target.dataModel() == DataModel::ILP32 ? 4 : 24;
}

std::uint64_t vaListAlignment(const TargetInfo& target) {
    return target.dataModel() == DataModel::ILP32 ? 4 : 8;
}

ast::QualType enumIntegerType(const ast::EnumDecl* decl) {
    NITWIT_ASSERT(!decl->integerType().isNull(), "integer type of an enumeration that sema has not completed");
    return decl->integerType();
}

// The alignment of a type, following its sugar: a typedef with an `aligned` attribute sets it (GCC's
// TYPE_USER_ALIGN, which ADJUST_FIELD_ALIGN leaves alone). Otherwise the target's ABI alignment (`_Alignof`) or, with
// `preferred`, GCC's TYPE_ALIGN (`__alignof__`), which differ for 8-byte scalars on i386. A record's alignment is both.
std::uint64_t alignment(ast::AstContext& context, ast::QualType type, bool preferred) {
    const TargetInfo& target = context.target();
    const auto scalarAlignment = [&](ScalarType scalar) {
        const ScalarLayout layout = target.layout(scalar);
        return preferred ? layout.preferredAlign : layout.align;
    };
    switch (type->kind()) {
    case ast::TypeKind::Typedef: {
        const ast::TypedefDecl* decl = ast::cast<ast::TypedefType>(type.type())->decl();
        return decl->requestedAlignment() != 0 ? decl->requestedAlignment()
                                               : alignment(context, decl->underlying(), preferred);
    }
    case ast::TypeKind::Typeof:
        return alignment(context, ast::cast<ast::TypeofType>(type.type())->resolved(), preferred);
    case ast::TypeKind::Array:
        return alignment(context, ast::cast<ast::ArrayType>(type.type())->element(), preferred);
    case ast::TypeKind::Complex:
        return alignment(context, ast::cast<ast::ComplexType>(type.type())->element(), preferred);
    case ast::TypeKind::Builtin: {
        const BuiltinKind kind = ast::cast<ast::BuiltinType>(type.type())->builtinKind();
        if (kind == BuiltinKind::VaList) {
            return vaListAlignment(target);
        }
        const std::optional<ScalarType> scalar = scalarTypeOf(kind);
        return scalar ? scalarAlignment(*scalar) : 1;
    }
    case ast::TypeKind::Pointer:
        return scalarAlignment(ScalarType::Pointer);
    case ast::TypeKind::Function:
        return 1;
    case ast::TypeKind::Record:
        return layoutOf(ast::cast<ast::RecordType>(type.type())->decl()).alignment;
    case ast::TypeKind::Enum:
        return alignment(context, enumIntegerType(ast::cast<ast::EnumType>(type.type())->decl()), preferred);
    }
    NITWIT_UNREACHABLE("unknown type kind");
}

// C17 6.7.6.3p15: a prototype is compatible with a declaration without one if it has no ellipsis and every parameter
// type is compatible with its promoted type (int- and long-sized enumerations are).
bool isPrototypeCompatibleWithPromotions(ast::AstContext& context, const ast::FunctionType* prototype) {
    if (prototype->isVariadic()) {
        return false;
    }
    return std::ranges::all_of(prototype->parameters(), [&](ast::QualType parameter) {
        const ast::QualType canonical = context.canonical(parameter).unqualified();
        if (builtinKind(context, canonical) == BuiltinKind::Float) {
            return false;
        }
        return compatibleUnqualified(context, promotedType(context, canonical), canonical);
    });
}

} // namespace

TypeClass classify(ast::AstContext& context, ast::QualType type) {
    const ast::QualType canonical = context.canonical(type);
    switch (canonical->kind()) {
    case ast::TypeKind::Builtin:
        switch (ast::cast<ast::BuiltinType>(canonical.type())->builtinKind()) {
        case BuiltinKind::Void:
            return TypeClass::Void;
        case BuiltinKind::Bool:
            return TypeClass::Bool;
        case BuiltinKind::Float:
        case BuiltinKind::Double:
        case BuiltinKind::LongDouble:
            return TypeClass::Floating;
        case BuiltinKind::VaList:
            return TypeClass::VaList;
        default:
            return TypeClass::Integer;
        }
    case ast::TypeKind::Enum:
        return TypeClass::Integer;
    case ast::TypeKind::Pointer:
        return TypeClass::Pointer;
    case ast::TypeKind::Array:
        return TypeClass::Array;
    case ast::TypeKind::Function:
        return TypeClass::Function;
    case ast::TypeKind::Record:
        return TypeClass::Record;
    case ast::TypeKind::Complex:
        return TypeClass::Complex;
    case ast::TypeKind::Typedef:
    case ast::TypeKind::Typeof:
        break;
    }
    NITWIT_UNREACHABLE("canonical type with sugar");
}

ast::QualType desugar(ast::QualType type) {
    while (true) {
        if (const auto* typedefType = ast::dyn_cast<ast::TypedefType>(type.type())) {
            type = typedefType->decl()->underlying().withQualifiers(type.qualifiers());
        } else if (const auto* typeOf = ast::dyn_cast<ast::TypeofType>(type.type())) {
            NITWIT_ASSERT(!typeOf->resolved().isNull(), "desugaring an unresolved typeof");
            type = typeOf->resolved().withQualifiers(type.qualifiers());
        } else {
            return type;
        }
    }
}

std::optional<BuiltinKind> builtinKind(ast::AstContext& context, ast::QualType type) {
    const auto* builtin = ast::dyn_cast<ast::BuiltinType>(context.canonical(type).type());
    if (builtin == nullptr) {
        return std::nullopt;
    }
    return builtin->builtinKind();
}

ast::RecordDecl* recordOf(ast::QualType type) {
    const auto* record = ast::dyn_cast<ast::RecordType>(desugar(type).type());
    return record != nullptr ? record->decl() : nullptr;
}

ast::QualType pointeeOf(ast::QualType type) {
    return ast::cast<ast::PointerType>(desugar(type).type())->pointee();
}

const ast::ArrayType* arrayOf(ast::QualType type) {
    return ast::dyn_cast<ast::ArrayType>(desugar(type).type());
}

const ast::FunctionType* functionOf(ast::QualType type) {
    return ast::dyn_cast<ast::FunctionType>(desugar(type).type());
}

bool isVariablySized(ast::QualType type) {
    for (const ast::ArrayType* array = arrayOf(type); array != nullptr; array = arrayOf(array->element())) {
        if (array->sizeKind() == ast::ArrayType::SizeKind::Variable) {
            return true;
        }
    }
    return false;
}

bool isCharacterType(ast::AstContext& context, ast::QualType type) {
    const std::optional<BuiltinKind> kind = builtinKind(context, type);
    return kind == BuiltinKind::Char || kind == BuiltinKind::SignedChar || kind == BuiltinKind::UnsignedChar;
}

bool isIntegerClass(TypeClass type) {
    return type == TypeClass::Integer || type == TypeClass::Bool;
}

bool isArithmeticClass(TypeClass type) {
    return isIntegerClass(type) || type == TypeClass::Floating;
}

bool isScalarClass(TypeClass type) {
    return isArithmeticClass(type) || type == TypeClass::Pointer;
}

ast::QualType integerTypeOf(ast::AstContext& context, ast::QualType type) {
    if (const auto* enumType = ast::dyn_cast<ast::EnumType>(context.canonical(type).type())) {
        return enumIntegerType(enumType->decl());
    }
    return type;
}

int integerRank(BuiltinKind kind) {
    switch (kind) {
    case BuiltinKind::Bool:
        return 0;
    case BuiltinKind::Char:
    case BuiltinKind::SignedChar:
    case BuiltinKind::UnsignedChar:
        return 1;
    case BuiltinKind::Short:
    case BuiltinKind::UnsignedShort:
        return 2;
    case BuiltinKind::Int:
    case BuiltinKind::UnsignedInt:
        return 3;
    case BuiltinKind::Long:
    case BuiltinKind::UnsignedLong:
        return 4;
    case BuiltinKind::LongLong:
    case BuiltinKind::UnsignedLongLong:
        return 5;
    case BuiltinKind::Int128:
    case BuiltinKind::UnsignedInt128:
        return 6;
    default:
        NITWIT_UNREACHABLE("rank of a non-integer type");
    }
}

ast::QualType promotedType(ast::AstContext& context, ast::QualType type) {
    const ast::QualType canonical = context.canonical(integerTypeOf(context, type)).unqualified();
    const auto* builtin = ast::dyn_cast<ast::BuiltinType>(canonical.type());
    if (builtin == nullptr) {
        return type;
    }
    const BuiltinKind kind = builtin->builtinKind();
    const bool isInteger = classify(context, canonical) == TypeClass::Integer || kind == BuiltinKind::Bool;
    if (!isInteger) {
        return type;
    }
    // Every type of lower rank than int fits into int on x86 (C17 6.3.1.1p2).
    if (integerRank(kind) < integerRank(BuiltinKind::Int)) {
        return context.builtin(BuiltinKind::Int);
    }
    // An enumeration is replaced by its compatible type, as GCC's perform_integral_promotions does.
    return ast::isa<ast::EnumType>(context.canonical(type).type()) ? canonical : type;
}

ast::QualType commonArithmeticType(ast::AstContext& context, ast::QualType lhs, ast::QualType rhs) {
    const BuiltinKind left = *builtinKind(context, lhs);
    const BuiltinKind right = *builtinKind(context, rhs);
    // C17 6.3.1.8p1: the floating types first.
    for (const BuiltinKind floating : {BuiltinKind::LongDouble, BuiltinKind::Double, BuiltinKind::Float}) {
        if (left == floating || right == floating) {
            return context.builtin(floating);
        }
    }
    if (left == right) {
        return context.builtin(left);
    }
    const bool leftSigned = isSignedKind(left);
    const bool rightSigned = isSignedKind(right);
    if (leftSigned == rightSigned) {
        return context.builtin(integerRank(left) >= integerRank(right) ? left : right);
    }
    const BuiltinKind signedKind = leftSigned ? left : right;
    const BuiltinKind unsignedKind = leftSigned ? right : left;
    if (integerRank(unsignedKind) >= integerRank(signedKind)) {
        return context.builtin(unsignedKind);
    }
    // The signed type has the higher rank; it is used if it can represent all values of the unsigned type, which on
    // x86 is the case exactly if it is wider.
    if (sizeOf(context, context.builtin(signedKind)) > sizeOf(context, context.builtin(unsignedKind))) {
        return context.builtin(signedKind);
    }
    return context.builtin(unsignedKindOf(signedKind));
}

ast::QualType unqualified(ast::AstContext& context, ast::QualType type) {
    const ast::QualType canonical = context.canonical(type);
    if (canonical.qualifiers().empty()) {
        return type;
    }
    if (context.canonical(type.unqualified()).qualifiers().empty()) {
        return type.unqualified(); // the qualifiers are written on top of the sugar
    }
    return canonical.unqualified(); // the qualifiers come from a typedef
}

bool isConstQualified(ast::AstContext& context, ast::QualType type) {
    ast::QualType canonical = context.canonical(type);
    // canonical() leaves variable length arrays alone: their qualifiers stay on the array and the element keeps its
    // sugar, so the element is canonicalized with the array's qualifiers in each step.
    while (const auto* array = ast::dyn_cast<ast::ArrayType>(canonical.type())) {
        canonical = context.canonical(array->element().withQualifiers(canonical.qualifiers()));
    }
    return canonical.qualifiers().isConst;
}

std::uint64_t fieldAlignment(ast::AstContext& context, const ast::FieldDecl* field) {
    const ast::RecordDecl* record = field->parent();
    const std::uint64_t requested = field->requestedAlignment();
    std::uint64_t alignment = 1;
    if (record->isPacked() || field->isPacked()) {
        alignment = requested != 0 ? requested : 1;
    } else if (requested >= preferredAlignment(context, field->type())) {
        alignment = requested;
    } else {
        alignment = alignOf(context, field->type());
    }
    return record->maxFieldAlignment() != 0 ? std::min(alignment, record->maxFieldAlignment()) : alignment;
}

std::optional<std::uint64_t> fieldOffset(const ast::RecordDecl* record, const ast::FieldDecl* field) {
    const ast::RecordLayout& layout = layoutOf(record);
    for (const ast::FieldDecl* member : record->fields()) {
        if (member == field) {
            return layout.fieldOffsets[member->index()];
        }
        if (member->isAnonymousRecord()) {
            if (const std::optional<std::uint64_t> inner = fieldOffset(recordOf(member->type()), field)) {
                return layout.fieldOffsets[member->index()] + *inner;
            }
        }
    }
    return std::nullopt;
}

bool compatibleUnqualified(ast::AstContext& context, ast::QualType lhs, ast::QualType rhs) {
    const ast::QualType a = context.canonical(lhs).unqualified();
    const ast::QualType b = context.canonical(rhs).unqualified();
    if (a == b) {
        return true;
    }
    // GCC treats an enumeration as its compatible integer type.
    const auto* aEnum = ast::dyn_cast<ast::EnumType>(a.type());
    const auto* bEnum = ast::dyn_cast<ast::EnumType>(b.type());
    if ((aEnum != nullptr) != (bEnum != nullptr)) {
        const ast::QualType integer =
            context.canonical(aEnum != nullptr ? enumIntegerType(aEnum->decl()) : enumIntegerType(bEnum->decl()));
        return integer == (aEnum != nullptr ? b : a);
    }
    if (a->kind() != b->kind()) {
        return false;
    }
    switch (a->kind()) {
    case ast::TypeKind::Pointer:
        return compatible(context, ast::cast<ast::PointerType>(a.type())->pointee(),
                          ast::cast<ast::PointerType>(b.type())->pointee());
    case ast::TypeKind::Array: {
        const auto* aArray = ast::cast<ast::ArrayType>(a.type());
        const auto* bArray = ast::cast<ast::ArrayType>(b.type());
        if (!compatible(context, aArray->element(), bArray->element())) {
            return false;
        }
        const bool bothConstant = aArray->sizeKind() == ast::ArrayType::SizeKind::Constant
                                  && bArray->sizeKind() == ast::ArrayType::SizeKind::Constant;
        return !bothConstant || aArray->size() == bArray->size();
    }
    case ast::TypeKind::Function: {
        const auto* aFunction = ast::cast<ast::FunctionType>(a.type());
        const auto* bFunction = ast::cast<ast::FunctionType>(b.type());
        if (!compatible(context, aFunction->returnType(), bFunction->returnType())) {
            return false;
        }
        if (aFunction->hasPrototype() && bFunction->hasPrototype()) {
            if (aFunction->isVariadic() != bFunction->isVariadic()
                || aFunction->parameters().size() != bFunction->parameters().size()) {
                return false;
            }
            for (std::size_t i = 0; i < aFunction->parameters().size(); ++i) {
                if (!compatibleUnqualified(context, aFunction->parameters()[i], bFunction->parameters()[i])) {
                    return false;
                }
            }
            return true;
        }
        if (aFunction->hasPrototype()) {
            return isPrototypeCompatibleWithPromotions(context, aFunction);
        }
        if (bFunction->hasPrototype()) {
            return isPrototypeCompatibleWithPromotions(context, bFunction);
        }
        return true;
    }
    case ast::TypeKind::Complex:
        return compatible(context, ast::cast<ast::ComplexType>(a.type())->element(),
                          ast::cast<ast::ComplexType>(b.type())->element());
    default:
        return false; // builtins, records and enumerations are compatible only with themselves
    }
}

bool compatible(ast::AstContext& context, ast::QualType lhs, ast::QualType rhs) {
    return context.canonical(lhs).qualifiers() == context.canonical(rhs).qualifiers()
           && compatibleUnqualified(context, lhs, rhs);
}

ast::QualType composite(ast::AstContext& context, ast::QualType lhs, ast::QualType rhs) {
    const ast::QualType a = context.canonical(lhs);
    const ast::QualType b = context.canonical(rhs);
    if (a == b || a->kind() != b->kind()) {
        return a; // equal, or an enumeration and its integer type
    }
    const ast::Qualifiers qualifiers = a.qualifiers();
    switch (a->kind()) {
    case ast::TypeKind::Pointer:
        return context
            .pointerTo(composite(context, ast::cast<ast::PointerType>(a.type())->pointee(),
                                 ast::cast<ast::PointerType>(b.type())->pointee()))
            .withQualifiers(qualifiers);
    case ast::TypeKind::Array: {
        const auto* aArray = ast::cast<ast::ArrayType>(a.type());
        const auto* bArray = ast::cast<ast::ArrayType>(b.type());
        const ast::QualType element = composite(context, aArray->element(), bArray->element());
        // C17 6.2.7p3: a known constant size wins; otherwise a variable length array.
        for (const ast::ArrayType* array : {aArray, bArray}) {
            if (array->sizeKind() == ast::ArrayType::SizeKind::Constant) {
                return context.constantArrayOf(element, array->size());
            }
        }
        for (const ast::ArrayType* array : {aArray, bArray}) {
            if (array->sizeKind() == ast::ArrayType::SizeKind::Variable) {
                return ast::QualType(array);
            }
        }
        return context.arrayOf(element, nullptr);
    }
    case ast::TypeKind::Function: {
        const auto* aFunction = ast::cast<ast::FunctionType>(a.type());
        const auto* bFunction = ast::cast<ast::FunctionType>(b.type());
        const ast::QualType returnType = composite(context, aFunction->returnType(), bFunction->returnType());
        if (!aFunction->hasPrototype() || !bFunction->hasPrototype()) {
            const ast::FunctionType* prototype = aFunction->hasPrototype() ? aFunction : bFunction;
            return context.functionType(returnType, prototype->parameters(), prototype->isVariadic(),
                                        prototype->hasPrototype());
        }
        std::vector<ast::QualType> parameters;
        for (std::size_t i = 0; i < aFunction->parameters().size(); ++i) {
            parameters.push_back(
                composite(context, aFunction->parameters()[i], bFunction->parameters()[i]).unqualified());
        }
        return context.functionType(returnType, parameters, aFunction->isVariadic(), true);
    }
    case ast::TypeKind::Complex:
        return a;
    default:
        return a;
    }
}

std::uint64_t preferredAlignment(ast::AstContext& context, ast::QualType type) {
    return alignment(context, type, true);
}

ast::QualType sizeType(ast::AstContext& context) {
    return context.builtin(builtinKindOf(context.target().sizeType()));
}

ast::QualType ptrdiffType(ast::AstContext& context) {
    return context.builtin(builtinKindOf(context.target().ptrdiffType()));
}

ast::QualType wcharType(ast::AstContext& context) {
    return context.builtin(builtinKindOf(context.target().wcharType()));
}

BuiltinKind builtinKindOf(ScalarType type) {
    const auto* found = std::ranges::find(scalarKinds, type, &std::pair<BuiltinKind, ScalarType>::second);
    NITWIT_ASSERT(found != std::end(scalarKinds), "not a builtin scalar type");
    return found->first;
}

ast::QualType integerTypeForSize(ast::AstContext& context, std::uint64_t bytes, bool isSigned) {
    BuiltinKind kind = BuiltinKind::Int;
    switch (bytes) {
    case 1:
        kind = BuiltinKind::SignedChar;
        break;
    case 2:
        kind = BuiltinKind::Short;
        break;
    case 4:
        kind = BuiltinKind::Int;
        break;
    case 8:
        kind = context.target().dataModel() == DataModel::LP64 ? BuiltinKind::Long : BuiltinKind::LongLong;
        break;
    case 16:
        kind = BuiltinKind::Int128;
        break;
    default:
        NITWIT_UNREACHABLE("no integer type of this size");
    }
    return context.builtin(isSigned ? kind : unsignedKindOf(kind));
}

} // namespace detail

using detail::classify;
using detail::TypeClass;

bool isVoid(ast::AstContext& context, ast::QualType type) {
    return classify(context, type) == TypeClass::Void;
}

bool isInteger(ast::AstContext& context, ast::QualType type) {
    return detail::isIntegerClass(classify(context, type));
}

bool isSignedInteger(ast::AstContext& context, ast::QualType type) {
    return isInteger(context, type) && integerFormat(context, type).isSigned;
}

bool isFloating(ast::AstContext& context, ast::QualType type) {
    return classify(context, type) == TypeClass::Floating;
}

bool isArithmetic(ast::AstContext& context, ast::QualType type) {
    return detail::isArithmeticClass(classify(context, type));
}

bool isPointer(ast::AstContext& context, ast::QualType type) {
    return classify(context, type) == TypeClass::Pointer;
}

bool isScalar(ast::AstContext& context, ast::QualType type) {
    return detail::isScalarClass(classify(context, type));
}

bool isRecord(ast::AstContext& context, ast::QualType type) {
    return classify(context, type) == TypeClass::Record;
}

ast::QualType pointee(ast::AstContext& context, ast::QualType type) {
    return ast::cast<ast::PointerType>(context.canonical(type).type())->pointee();
}

value::IntegerFormat integerFormat(ast::AstContext& context, ast::QualType type) {
    const ast::QualType canonical = context.canonical(detail::integerTypeOf(context, type));
    if (ast::isa<ast::PointerType>(canonical.type())) {
        return {.bits = static_cast<std::uint8_t>(context.target().pointerBits()), .isSigned = false};
    }
    const ast::BuiltinKind kind = ast::cast<ast::BuiltinType>(canonical.type())->builtinKind();
    const std::optional<ScalarType> scalar = detail::scalarTypeOf(kind);
    NITWIT_ASSERT(scalar.has_value() && isInteger(context, canonical), "integer format of a non-integer type");
    return {.bits = static_cast<std::uint8_t>(context.target().layout(*scalar).size * 8),
            .isSigned = context.target().isSigned(*scalar)};
}

value::FloatFormat floatFormat(ast::AstContext& context, ast::QualType type) {
    switch (*detail::builtinKind(context, type)) {
    case ast::BuiltinKind::Float:
        return value::FloatFormat::Float;
    case ast::BuiltinKind::Double:
        return value::FloatFormat::Double;
    case ast::BuiltinKind::LongDouble:
        return value::FloatFormat::LongDouble;
    default:
        NITWIT_UNREACHABLE("float format of a non-floating type");
    }
}

std::uint64_t sizeOf(ast::AstContext& context, ast::QualType type) {
    const ast::QualType canonical = context.canonical(type);
    const TargetInfo& target = context.target();
    switch (canonical->kind()) {
    case ast::TypeKind::Builtin: {
        const ast::BuiltinKind kind = ast::cast<ast::BuiltinType>(canonical.type())->builtinKind();
        if (kind == ast::BuiltinKind::VaList) {
            return detail::vaListSize(target);
        }
        const std::optional<ScalarType> scalar = detail::scalarTypeOf(kind);
        return scalar ? target.layout(*scalar).size : 1; // GNU: sizeof(void) is 1
    }
    case ast::TypeKind::Pointer:
        return target.layout(ScalarType::Pointer).size;
    case ast::TypeKind::Array: {
        const auto* array = ast::cast<ast::ArrayType>(canonical.type());
        return array->size() * sizeOf(context, array->element());
    }
    case ast::TypeKind::Function:
        return 1; // GNU
    case ast::TypeKind::Record:
        return layoutOf(ast::cast<ast::RecordType>(canonical.type())->decl()).size;
    case ast::TypeKind::Enum:
        return sizeOf(context, detail::enumIntegerType(ast::cast<ast::EnumType>(canonical.type())->decl()));
    case ast::TypeKind::Complex:
        return 2 * sizeOf(context, ast::cast<ast::ComplexType>(canonical.type())->element());
    case ast::TypeKind::Typedef:
    case ast::TypeKind::Typeof:
        break;
    }
    NITWIT_UNREACHABLE("canonical type with sugar");
}

std::uint64_t alignOf(ast::AstContext& context, ast::QualType type) {
    return detail::alignment(context, type, false);
}

const ast::RecordLayout& layoutOf(const ast::RecordDecl* record) {
    NITWIT_ASSERT(record->layout() != nullptr, "layout of a record that sema has not laid out");
    return *record->layout();
}

} // namespace nitwit::sema
