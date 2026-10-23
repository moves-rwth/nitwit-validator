#include "ast/context.hpp"

#include <array>
#include <cstddef>
#include <functional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ast/casting.hpp"
#include "ast/decl.hpp"
#include "support/assert.hpp"

namespace nitwit::ast {

namespace {

constexpr std::size_t builtinCount = std::to_underlying(BuiltinKind::VaList) + 1;

std::size_t combine(std::size_t seed, std::size_t value) {
    return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2));
}

std::size_t hashQualType(QualType type) {
    const Qualifiers q = type.qualifiers();
    const std::size_t bits =
        (q.isConst ? 1u : 0u) | (q.isVolatile ? 2u : 0u) | (q.isRestrict ? 4u : 0u) | (q.isAtomic ? 8u : 0u);
    return combine(std::hash<const Type*>{}(type.type()), bits);
}

struct QualTypeHash {
    std::size_t operator()(QualType type) const { return hashQualType(type); }
};

struct ArrayKey {
    QualType element;
    std::uint64_t size;
    friend bool operator==(const ArrayKey&, const ArrayKey&) = default;
};

struct ArrayKeyHash {
    std::size_t operator()(const ArrayKey& key) const {
        return combine(hashQualType(key.element), std::hash<std::uint64_t>{}(key.size));
    }
};

struct FunctionKey {
    QualType returnType;
    std::vector<QualType> parameters;
    bool isVariadic;
    bool hasPrototype;
    friend bool operator==(const FunctionKey&, const FunctionKey&) = default;
};

struct FunctionKeyHash {
    std::size_t operator()(const FunctionKey& key) const {
        std::size_t hash =
            combine(hashQualType(key.returnType), (key.isVariadic ? 1u : 0u) | (key.hasPrototype ? 2u : 0u));
        for (const QualType parameter : key.parameters) {
            hash = combine(hash, hashQualType(parameter));
        }
        return hash;
    }
};

} // namespace

// The uniquing tables. They are only looked up, never iterated, so their order cannot affect the output.
struct AstContext::Types {
    std::array<const BuiltinType*, builtinCount> builtins{};
    std::unordered_map<QualType, const PointerType*, QualTypeHash> pointers;
    std::unordered_map<ArrayKey, const ArrayType*, ArrayKeyHash> constantArrays;
    std::unordered_map<QualType, const ArrayType*, QualTypeHash> incompleteArrays;
    std::unordered_map<FunctionKey, const FunctionType*, FunctionKeyHash> functions;
    std::unordered_map<const Decl*, const Type*> declTypes; // records, enums and typedefs
    std::unordered_map<QualType, const ComplexType*, QualTypeHash> complexes;
};

AstContext::AstContext(const TargetInfo& target, StringInterner& interner)
    : target_(target), interner_(interner), types_(std::make_unique<Types>()) {
    for (std::size_t i = 0; i < builtinCount; ++i) {
        types_->builtins[i] = create<BuiltinType>(static_cast<BuiltinKind>(i));
    }
}

AstContext::~AstContext() = default;

QualType AstContext::builtin(BuiltinKind kind) const {
    return QualType(types_->builtins[std::to_underlying(kind)]);
}

QualType AstContext::pointerTo(QualType pointee) {
    const PointerType*& type = types_->pointers[pointee];
    if (type == nullptr) {
        type = create<PointerType>(pointee);
    }
    return QualType(type);
}

QualType AstContext::arrayOf(QualType element, Expr* sizeExpression) {
    if (sizeExpression != nullptr) {
        return QualType(create<ArrayType>(element, sizeExpression));
    }
    const ArrayType*& type = types_->incompleteArrays[element];
    if (type == nullptr) {
        type = create<ArrayType>(element, nullptr);
    }
    return QualType(type);
}

QualType AstContext::constantArrayOf(QualType element, std::uint64_t size) {
    const ArrayType*& type = types_->constantArrays[ArrayKey{element, size}];
    if (type == nullptr) {
        type = create<ArrayType>(element, size);
    }
    return QualType(type);
}

QualType AstContext::functionType(QualType returnType, std::span<const QualType> parameters, bool isVariadic,
                                  bool hasPrototype) {
    FunctionKey key{returnType, {parameters.begin(), parameters.end()}, isVariadic, hasPrototype};
    const FunctionType*& type = types_->functions[std::move(key)];
    if (type == nullptr) {
        type = create<FunctionType>(returnType, copy(parameters), isVariadic, hasPrototype);
    }
    return QualType(type);
}

QualType AstContext::recordType(RecordDecl* decl) {
    const Type*& type = types_->declTypes[decl];
    if (type == nullptr) {
        type = create<RecordType>(decl);
    }
    return QualType(type);
}

QualType AstContext::enumType(EnumDecl* decl) {
    const Type*& type = types_->declTypes[decl];
    if (type == nullptr) {
        type = create<EnumType>(decl);
    }
    return QualType(type);
}

QualType AstContext::typedefType(TypedefDecl* decl) {
    const Type*& type = types_->declTypes[decl];
    if (type == nullptr) {
        type = create<TypedefType>(decl);
    }
    return QualType(type);
}

QualType AstContext::complexOf(QualType element) {
    const ComplexType*& type = types_->complexes[element];
    if (type == nullptr) {
        type = create<ComplexType>(element);
    }
    return QualType(type);
}

QualType AstContext::typeofExpression(Expr* expression, bool isAutoType) {
    return QualType(create<TypeofType>(expression, isAutoType));
}

QualType AstContext::canonical(QualType type) {
    const Qualifiers qualifiers = type.qualifiers();
    switch (type->kind()) {
    case TypeKind::Builtin:
    case TypeKind::Record:
    case TypeKind::Enum:
        return type;
    case TypeKind::Pointer:
        return pointerTo(canonical(cast<PointerType>(type.type())->pointee())).withQualifiers(qualifiers);
    case TypeKind::Array: {
        const auto* array = cast<ArrayType>(type.type());
        NITWIT_ASSERT(array->sizeKind() != ArrayType::SizeKind::Unresolved, "canonical type of an unresolved array");
        if (array->sizeKind() == ArrayType::SizeKind::Variable) {
            return type;
        }
        // The qualifiers of an array type apply to its elements (C17 6.7.3p10).
        const QualType element = canonical(array->element().withQualifiers(qualifiers));
        if (array->sizeKind() == ArrayType::SizeKind::Constant) {
            return constantArrayOf(element, array->size());
        }
        return arrayOf(element, nullptr);
    }
    case TypeKind::Function: {
        // The return type is unqualified (C17 6.7.6.3p5), and the qualifiers of parameters do not affect the type
        // (C17 6.7.6.3p15).
        const auto* function = cast<FunctionType>(type.type());
        std::vector<QualType> parameters;
        parameters.reserve(function->parameters().size());
        for (const QualType parameter : function->parameters()) {
            parameters.push_back(canonical(parameter).unqualified());
        }
        return functionType(canonical(function->returnType()).unqualified(), parameters, function->isVariadic(),
                            function->hasPrototype())
            .withQualifiers(qualifiers);
    }
    case TypeKind::Typedef:
        return canonical(cast<TypedefType>(type.type())->decl()->underlying().withQualifiers(qualifiers));
    case TypeKind::Complex:
        return complexOf(canonical(cast<ComplexType>(type.type())->element())).withQualifiers(qualifiers);
    case TypeKind::Typeof: {
        const QualType resolved = cast<TypeofType>(type.type())->resolved();
        NITWIT_ASSERT(!resolved.isNull(), "canonical type of an unresolved typeof");
        return canonical(resolved.withQualifiers(qualifiers));
    }
    }
    NITWIT_UNREACHABLE("unknown type kind");
}

} // namespace nitwit::ast
