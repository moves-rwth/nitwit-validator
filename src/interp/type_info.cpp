#include "interp/type_info.hpp"

#include <optional>

#include "ast/casting.hpp"
#include "support/assert.hpp"

namespace nitwit::interp {

namespace {

using ast::BuiltinKind;

std::optional<ScalarType> scalarOf(BuiltinKind kind) {
    switch (kind) {
    case BuiltinKind::Bool:
        return ScalarType::Bool;
    case BuiltinKind::Char:
        return ScalarType::Char;
    case BuiltinKind::SignedChar:
        return ScalarType::SignedChar;
    case BuiltinKind::UnsignedChar:
        return ScalarType::UnsignedChar;
    case BuiltinKind::Short:
        return ScalarType::Short;
    case BuiltinKind::UnsignedShort:
        return ScalarType::UnsignedShort;
    case BuiltinKind::Int:
        return ScalarType::Int;
    case BuiltinKind::UnsignedInt:
        return ScalarType::UnsignedInt;
    case BuiltinKind::Long:
        return ScalarType::Long;
    case BuiltinKind::UnsignedLong:
        return ScalarType::UnsignedLong;
    case BuiltinKind::LongLong:
        return ScalarType::LongLong;
    case BuiltinKind::UnsignedLongLong:
        return ScalarType::UnsignedLongLong;
    case BuiltinKind::Int128:
        return ScalarType::Int128;
    case BuiltinKind::UnsignedInt128:
        return ScalarType::UnsignedInt128;
    case BuiltinKind::Float:
        return ScalarType::Float;
    case BuiltinKind::Double:
        return ScalarType::Double;
    case BuiltinKind::LongDouble:
        return ScalarType::LongDouble;
    case BuiltinKind::Void:
    case BuiltinKind::VaList:
        return std::nullopt;
    }
    return std::nullopt;
}

TypeInfo builtinInfo(const TargetInfo& target, BuiltinKind kind) {
    TypeInfo info;
    switch (kind) {
    case BuiltinKind::Void:
        info.representation = Representation::Void;
        info.size = 1; // GNU: sizeof(void) is 1
        return info;
    case BuiltinKind::VaList:
        // The ABI's va_list: char * on i386, an array of one 24-byte struct on x86-64.
        info.representation = Representation::VaList;
        info.size = target.dataModel() == DataModel::ILP32 ? 4 : 24;
        return info;
    case BuiltinKind::Float:
    case BuiltinKind::Double:
    case BuiltinKind::LongDouble:
        info.representation = Representation::Floating;
        info.floating = kind == BuiltinKind::Float    ? value::FloatFormat::Float
                        : kind == BuiltinKind::Double ? value::FloatFormat::Double
                                                      : value::FloatFormat::LongDouble;
        info.scalar = *scalarOf(kind);
        info.size = target.layout(info.scalar).size;
        return info;
    default:
        break;
    }
    const ScalarType scalar = *scalarOf(kind);
    info.representation = kind == BuiltinKind::Bool ? Representation::Bool : Representation::Integer;
    info.scalar = scalar;
    info.size = target.layout(scalar).size;
    info.integer = {.bits = static_cast<std::uint8_t>(info.size * 8), .isSigned = target.isSigned(scalar)};
    return info;
}

} // namespace

const ast::Type* desugared(ast::QualType type) {
    const ast::Type* current = type.type();
    while (true) {
        if (const auto* typedefType = ast::dyn_cast<ast::TypedefType>(current)) {
            current = typedefType->decl()->underlying().type();
        } else if (const auto* typeOf = ast::dyn_cast<ast::TypeofType>(current)) {
            NITWIT_ASSERT(!typeOf->resolved().isNull(), "typeof was not resolved by sema");
            current = typeOf->resolved().type();
        } else {
            return current;
        }
    }
}

TypeTable::TypeTable(const TargetInfo& target) : target_(target) {
    for (std::size_t i = 0; i < builtinCount; ++i) {
        builtins_[i] = builtinInfo(target, static_cast<BuiltinKind>(i));
    }
    pointer_.representation = Representation::Pointer;
    pointer_.scalar = ScalarType::Pointer;
    pointer_.size = target.layout(ScalarType::Pointer).size;
    pointer_.integer = {.bits = static_cast<std::uint8_t>(target.pointerBits()), .isSigned = false};
}

TypeInfo TypeTable::describeOther(ast::QualType type) const {
    const ast::Type* canonical = desugared(type);
    switch (canonical->kind()) {
    case ast::TypeKind::Builtin:
        return builtins_[static_cast<std::size_t>(ast::cast<ast::BuiltinType>(canonical)->builtinKind())];
    case ast::TypeKind::Pointer:
        return pointer_;
    case ast::TypeKind::Enum: {
        const ast::QualType integer = ast::cast<ast::EnumType>(canonical)->decl()->integerType();
        // GCC accepts uses of an enumeration declared but never defined; it behaves like unsigned int.
        return integer.isNull() ? builtins_[static_cast<std::size_t>(BuiltinKind::UnsignedInt)] : describe(integer);
    }
    case ast::TypeKind::Record: {
        TypeInfo info;
        info.representation = Representation::Record;
        const ast::RecordLayout* layout = ast::cast<ast::RecordType>(canonical)->decl()->layout();
        info.size = layout != nullptr ? layout->size : 0;
        return info;
    }
    case ast::TypeKind::Array: {
        const auto* array = ast::cast<ast::ArrayType>(canonical);
        const TypeInfo element = describe(array->element());
        TypeInfo info;
        info.representation = Representation::Array;
        info.isVariableArray = element.isVariableArray || array->sizeKind() == ast::ArrayType::SizeKind::Variable;
        if (!info.isVariableArray && array->sizeKind() == ast::ArrayType::SizeKind::Constant) {
            info.size = array->size() * element.size;
        }
        return info;
    }
    case ast::TypeKind::Function: {
        TypeInfo info;
        info.representation = Representation::Function;
        info.size = 1; // GNU
        return info;
    }
    case ast::TypeKind::Complex: {
        TypeInfo info;
        info.representation = Representation::Complex;
        info.size = 2 * describe(ast::cast<ast::ComplexType>(canonical)->element()).size;
        return info;
    }
    case ast::TypeKind::Typedef:
    case ast::TypeKind::Typeof:
        break;
    }
    NITWIT_UNREACHABLE("a desugared type with sugar");
}

ast::QualType TypeTable::pointee(ast::QualType pointerType) {
    return ast::cast<ast::PointerType>(desugared(pointerType))->pointee();
}

const ast::ArrayType* TypeTable::array(ast::QualType arrayType) {
    return ast::cast<ast::ArrayType>(desugared(arrayType));
}

const ast::RecordDecl* TypeTable::record(ast::QualType recordType) {
    return ast::cast<ast::RecordType>(desugared(recordType))->decl();
}

const ast::FunctionType* TypeTable::function(ast::QualType type) {
    const ast::Type* canonical = desugared(type);
    if (const auto* pointer = ast::dyn_cast<ast::PointerType>(canonical)) {
        canonical = desugared(pointer->pointee());
    }
    return ast::dyn_cast<ast::FunctionType>(canonical);
}

} // namespace nitwit::interp
