// Structs, unions and enumerations (C17 6.7.2.1-6.7.2.3): members, bit-field widths, enumeration values and types.

#include <bit>
#include <format>
#include <limits>

#include "ast/casting.hpp"
#include "sema/sema_impl.hpp"
#include "sema/types.hpp"
#include "support/assert.hpp"

namespace nitwit::sema::detail {

namespace {

constexpr Int128 intMin = std::numeric_limits<std::int32_t>::min();
constexpr Int128 intMax = std::numeric_limits<std::int32_t>::max();

// GCC's tree_int_cst_min_precision: the bits a value needs in a signed or unsigned type.
unsigned minimumPrecision(Int128 value, bool isUnsigned) {
    if (value < 0) {
        value = ~value;
    }
    if (value == 0) {
        return 1;
    }
    unsigned bits = 0;
    for (auto magnitude = static_cast<UInt128>(value); magnitude != 0; magnitude >>= 1) {
        ++bits;
    }
    return bits + (isUnsigned ? 0 : 1);
}

bool fitsInt(Int128 value) {
    return value >= intMin && value <= intMax;
}

} // namespace

bool Sema::isComplete(ast::QualType type) {
    const ast::QualType canonical = context_.canonical(type);
    switch (canonical->kind()) {
    case ast::TypeKind::Builtin:
        return ast::cast<ast::BuiltinType>(canonical.type())->builtinKind() != ast::BuiltinKind::Void;
    case ast::TypeKind::Array: {
        const auto* array = ast::cast<ast::ArrayType>(canonical.type());
        return array->sizeKind() != ast::ArrayType::SizeKind::Incomplete && isComplete(array->element());
    }
    case ast::TypeKind::Record: {
        ast::RecordDecl* record = ast::cast<ast::RecordType>(canonical.type())->decl();
        if (!record->isComplete() || completing_.contains(record)) {
            return false;
        }
        completeRecord(record);
        return true;
    }
    case ast::TypeKind::Enum: {
        ast::EnumDecl* enumDecl = ast::cast<ast::EnumType>(canonical.type())->decl();
        if (!enumDecl->isComplete() || completing_.contains(enumDecl)) {
            return false;
        }
        completeEnum(enumDecl, enumDecl->location());
        return true;
    }
    case ast::TypeKind::Function:
        return false;
    case ast::TypeKind::Pointer:
    case ast::TypeKind::Complex:
        return true;
    case ast::TypeKind::Typedef:
    case ast::TypeKind::Typeof:
        break;
    }
    NITWIT_UNREACHABLE("canonical type with sugar");
}

void Sema::requireComplete(ast::QualType type, SourceLocation location, std::string_view what) {
    if (!isComplete(type)) {
        error(location, std::string(what));
    }
}

std::vector<ast::FieldDecl*> Sema::findField(ast::RecordDecl* record, Symbol name) {
    for (ast::FieldDecl* field : record->fields()) {
        if (field->name() == name) {
            return {field};
        }
        if (field->isAnonymousRecord()) {
            std::vector<ast::FieldDecl*> path = findField(recordOf(field->type()), name);
            if (!path.empty()) {
                path.insert(path.begin(), field);
                return path;
            }
        }
    }
    return {};
}

void Sema::completeRecord(ast::RecordDecl* record) {
    if (record->layout() != nullptr) {
        return;
    }
    NITWIT_ASSERT(record->isComplete() && !completing_.contains(record), "completing an incomplete record");
    completing_.insert(record);
    checkUnsupportedAttributes(record);
    if (findAttribute(record, "packed") != nullptr) {
        record->setPacked();
    }
    if (const ast::Attribute* aligned = findAttribute(record, "aligned")) {
        record->setRequestedAlignment(alignmentArgument(*aligned));
    }
    const std::span<ast::FieldDecl* const> fields = record->fields();
    for (std::size_t i = 0; i < fields.size(); ++i) {
        field(fields[i], i + 1 == fields.size());
    }
    record->setLayout(computeLayout(record));
    completing_.erase(record);
}

// C17 6.7.2.1p3-4, p11-12, p18: a member has a complete object type, except a flexible array member at the end of a
// struct; a bit-field has an integer type and a width no larger than the type's.
void Sema::field(ast::FieldDecl* field, bool isLast) {
    checkUnsupportedAttributes(field);
    const SourceLocation location = field->location();
    const std::string name = field->name().empty() ? std::string("<anonymous>") : std::string(field->name().text());
    resolveType(field->type(), location);
    if (const std::optional<ast::QualType> type = modeType(field, field->type())) {
        field->setType(*type);
    }
    if (const ast::Attribute* aligned = findAttribute(field, "aligned")) {
        field->setRequestedAlignment(alignmentArgument(*aligned));
    }
    if (findAttribute(field, "packed") != nullptr) {
        field->setPacked();
    }

    const ast::QualType type = field->type();
    const TypeClass typeClass = classify(context_, type);
    if (typeClass == TypeClass::Function) {
        error(location, std::format("field '{}' declared as a function", name));
    }
    if (isVariablySized(type)) {
        error(location, "a member of a structure or union cannot have a variably modified type");
    }
    const ast::ArrayType* array = arrayOf(type);
    if (array != nullptr && array->sizeKind() == ast::ArrayType::SizeKind::Incomplete) {
        if (!isLast) {
            error(location, "flexible array member not at end of struct");
        }
        if (field->parent()->isUnion()) {
            error(location, "flexible array member in union");
        }
        requireComplete(array->element(), location, std::format("field '{}' has incomplete type", name));
    } else {
        requireComplete(type, location, std::format("field '{}' has incomplete type", name));
    }

    if (!field->isBitField()) {
        return;
    }
    ast::Expr* width = field->bitWidthExpression();
    expr(width);
    if (!isIntegerClass(typeClass)) {
        error(location, std::format("bit-field '{}' has invalid type", name));
    }
    if (!isIntegerClass(classify(context_, width->type()))) {
        error(width->location(), std::format("bit-field '{}' width not an integer constant", name));
    }
    const Int128 value = integerConstant(width, std::format("bit-field '{}' width", name));
    const Int128 maximum = typeClass == TypeClass::Bool ? 1 : static_cast<Int128>(sizeOf(context_, type) * 8);
    if (value < 0) {
        error(width->location(), std::format("negative width in bit-field '{}'", name));
    }
    if (value > maximum) {
        error(width->location(), std::format("width of '{}' exceeds its type", name));
    }
    if (value == 0 && !field->name().empty()) {
        error(width->location(), std::format("zero width for bit-field '{}'", name));
    }
    field->setBitWidth(static_cast<std::uint32_t>(value));
}

void Sema::completeEnum(ast::EnumDecl* enumDecl, SourceLocation use) {
    if (!enumDecl->integerType().isNull()) {
        return;
    }
    if (completing_.contains(enumDecl)) {
        error(use, "use of an enumeration in its own definition");
    }
    completing_.insert(enumDecl);
    checkUnsupportedAttributes(enumDecl);
    const ast::EnumConstantDecl* previous = nullptr;
    for (ast::EnumConstantDecl* constant : enumDecl->constants()) {
        enumConstant(constant, previous);
        previous = constant;
    }
    NITWIT_ASSERT(!enumDecl->constants().empty(), "the parser rejects empty enumerations");
    Int128 minimum = enumDecl->constants().front()->value();
    Int128 maximum = minimum;
    for (const ast::EnumConstantDecl* constant : enumDecl->constants()) {
        minimum = std::min(minimum, constant->value());
        maximum = std::max(maximum, constant->value());
    }

    // GCC's finish_enum: int or unsigned int unless a value needs more bits or the enumeration is packed, then the
    // narrowest integer type that holds all values.
    const bool isUnsigned = minimum >= 0;
    const unsigned precision = std::max(minimumPrecision(minimum, isUnsigned), minimumPrecision(maximum, isUnsigned));
    const bool isPacked = findAttribute(enumDecl, "packed") != nullptr;
    std::uint64_t bytes = 4;
    if (isPacked || precision > 32) {
        if (precision > 64) {
            error(enumDecl->location(), "enumeration values exceed range of largest integer");
        }
        bytes = std::bit_ceil(std::max(8u, precision)) / 8;
    }
    enumDecl->setIntegerType(integerTypeForSize(context_, bytes, !isUnsigned));

    // GCC (C23 rules): if a value does not fit into int, all enumeration constants get the enumerated type.
    if (!fitsInt(minimum) || !fitsInt(maximum)) {
        for (ast::EnumConstantDecl* constant : enumDecl->constants()) {
            constant->setType(context_.enumType(enumDecl));
        }
    }
    completing_.erase(enumDecl);
}

// C17 6.7.2.2p3: a constant without initializer has the value of the previous one plus one. While the enumeration is
// being defined, a constant has type int if its value fits, else the type of its value (GCC, C23 rules).
void Sema::enumConstant(ast::EnumConstantDecl* constant, const ast::EnumConstantDecl* previous) {
    checkUnsupportedAttributes(constant);
    const std::string name(constant->name().text());
    Int128 value = 0;
    ast::QualType type = context_.builtin(ast::BuiltinKind::Int);
    if (ast::Expr* init = constant->initializer()) {
        expr(init);
        if (!isIntegerClass(classify(context_, init->type()))) {
            error(init->location(), std::format("enumerator value for '{}' is not an integer constant", name));
        }
        value = integerConstant(init, std::format("enumerator value for '{}'", name));
        if (!fitsInt(value)) {
            type = promotedType(context_, valueType(init));
        }
    } else if (previous != nullptr) {
        // GCC computes the next value in the type of the previous constant and rejects an overflow.
        const value::IntegerFormat format = integerFormat(context_, previous->type());
        const UInt128 maximum =
            format.isSigned ? (UInt128{1} << (format.bits - 1)) - 1 : ~UInt128{0} >> (128 - format.bits);
        if (previous->value() >= 0 && static_cast<UInt128>(previous->value()) == maximum) {
            error(constant->location(), "overflow in enumeration values");
        }
        value = previous->value() + 1;
        if (!fitsInt(value)) {
            type = previous->type();
        }
    }
    constant->setValue(value);
    constant->setType(type);
}

} // namespace nitwit::sema::detail
