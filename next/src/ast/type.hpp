#pragma once

// C types. The parser builds them through AstContext as written, including typedef names, array sizes as expressions
// and typeof. Semantic analysis then resolves array sizes and typeof in place. From then on,
// AstContext::canonical() gives the canonical type: sugar removed, uniqued, so that two canonical types are the same
// type exactly if they are the same pointer.

#include <cstdint>
#include <span>

namespace nitwit::ast {

class Expr;
class RecordDecl;
class EnumDecl;
class TypedefDecl;
class Type;

enum class TypeKind : std::uint8_t { Builtin, Pointer, Array, Function, Record, Enum, Typedef, Complex, Typeof };

enum class BuiltinKind : std::uint8_t {
    Void,
    Bool,
    Char,
    SignedChar,
    UnsignedChar,
    Short,
    UnsignedShort,
    Int,
    UnsignedInt,
    Long,
    UnsignedLong,
    LongLong,
    UnsignedLongLong,
    Int128,
    UnsignedInt128,
    Float,
    Double,
    LongDouble, // also _Float64x; _Float128 and __float128 are approximated by it (sema warns)
    VaList,     // __builtin_va_list; an opaque handle for the interpreter
};

struct Qualifiers {
    bool isConst = false;
    bool isVolatile = false;
    bool isRestrict = false;
    bool isAtomic = false;

    [[nodiscard]] bool empty() const { return !isConst && !isVolatile && !isRestrict && !isAtomic; }
    [[nodiscard]] Qualifiers operator|(Qualifiers other) const {
        return {isConst || other.isConst, isVolatile || other.isVolatile, isRestrict || other.isRestrict,
                isAtomic || other.isAtomic};
    }
    friend bool operator==(const Qualifiers&, const Qualifiers&) = default;
};

/// A type plus qualifiers. Equality is exact (typedef sugar counts); compare AstContext::canonical() for identity.
class QualType {
public:
    QualType() = default;
    explicit QualType(const Type* type, Qualifiers qualifiers = {}) : type_(type), qualifiers_(qualifiers) {}

    [[nodiscard]] bool isNull() const { return type_ == nullptr; }
    [[nodiscard]] const Type* type() const { return type_; }
    [[nodiscard]] const Type* operator->() const { return type_; }
    [[nodiscard]] Qualifiers qualifiers() const { return qualifiers_; }
    [[nodiscard]] QualType withQualifiers(Qualifiers added) const { return QualType(type_, qualifiers_ | added); }
    [[nodiscard]] QualType unqualified() const { return QualType(type_); }

    friend bool operator==(const QualType&, const QualType&) = default;

private:
    const Type* type_ = nullptr;
    Qualifiers qualifiers_;
};

class Type {
public:
    Type(const Type&) = delete;
    Type& operator=(const Type&) = delete;

    [[nodiscard]] TypeKind kind() const { return kind_; }

protected:
    explicit Type(TypeKind kind) : kind_(kind) {}
    ~Type() = default;

private:
    TypeKind kind_;
};

class BuiltinType final : public Type {
public:
    explicit BuiltinType(BuiltinKind builtinKind) : Type(TypeKind::Builtin), builtinKind_(builtinKind) {}
    static bool classof(const Type* type) { return type->kind() == TypeKind::Builtin; }

    [[nodiscard]] BuiltinKind builtinKind() const { return builtinKind_; }

private:
    BuiltinKind builtinKind_;
};

class PointerType final : public Type {
public:
    explicit PointerType(QualType pointee) : Type(TypeKind::Pointer), pointee_(pointee) {}
    static bool classof(const Type* type) { return type->kind() == TypeKind::Pointer; }

    [[nodiscard]] QualType pointee() const { return pointee_; }

private:
    QualType pointee_;
};

/// An array type. As written, its size is an expression (or absent); sema resolves it to a constant size or marks the
/// array as a variable length array, whose size the interpreter evaluates at the declaration.
class ArrayType final : public Type {
public:
    enum class SizeKind : std::uint8_t { Unresolved, Constant, Incomplete, Variable };

    ArrayType(QualType element, Expr* sizeExpression)
        : Type(TypeKind::Array), element_(element), sizeExpression_(sizeExpression),
          sizeKind_(sizeExpression != nullptr ? SizeKind::Unresolved : SizeKind::Incomplete) {}
    ArrayType(QualType element, std::uint64_t size)
        : Type(TypeKind::Array), element_(element), sizeKind_(SizeKind::Constant), size_(size) {}
    static bool classof(const Type* type) { return type->kind() == TypeKind::Array; }

    [[nodiscard]] QualType element() const { return element_; }
    [[nodiscard]] SizeKind sizeKind() const { return sizeKind_; }
    /// The size expression as written; null for `[]` and for arrays created with a constant size.
    [[nodiscard]] Expr* sizeExpression() const { return sizeExpression_; }
    /// Number of elements; requires SizeKind::Constant.
    [[nodiscard]] std::uint64_t size() const;

    // Resolution by sema.
    void resolveConstantSize(std::uint64_t size) const;
    void resolveVariableSize() const;

private:
    QualType element_;
    Expr* sizeExpression_ = nullptr;
    mutable SizeKind sizeKind_;
    mutable std::uint64_t size_ = 0;
};

class FunctionType final : public Type {
public:
    /// Parameter types are already adjusted: arrays and functions to pointers (C17 6.7.6.3p7-8).
    FunctionType(QualType returnType, std::span<const QualType> parameters, bool isVariadic, bool hasPrototype)
        : Type(TypeKind::Function), returnType_(returnType), parameters_(parameters), isVariadic_(isVariadic),
          hasPrototype_(hasPrototype) {}
    static bool classof(const Type* type) { return type->kind() == TypeKind::Function; }

    [[nodiscard]] QualType returnType() const { return returnType_; }
    [[nodiscard]] std::span<const QualType> parameters() const { return parameters_; }
    [[nodiscard]] bool isVariadic() const { return isVariadic_; }
    /// False for `int f()`, which says nothing about the parameters (C17 6.7.6.3p14).
    [[nodiscard]] bool hasPrototype() const { return hasPrototype_; }

private:
    QualType returnType_;
    std::span<const QualType> parameters_; // in the AstContext arena
    bool isVariadic_;
    bool hasPrototype_;
};

/// A struct or union; the same RecordDecl entity gives the same RecordType.
class RecordType final : public Type {
public:
    explicit RecordType(RecordDecl* decl) : Type(TypeKind::Record), decl_(decl) {}
    static bool classof(const Type* type) { return type->kind() == TypeKind::Record; }

    [[nodiscard]] RecordDecl* decl() const { return decl_; }

private:
    RecordDecl* decl_;
};

class EnumType final : public Type {
public:
    explicit EnumType(EnumDecl* decl) : Type(TypeKind::Enum), decl_(decl) {}
    static bool classof(const Type* type) { return type->kind() == TypeKind::Enum; }

    [[nodiscard]] EnumDecl* decl() const { return decl_; }

private:
    EnumDecl* decl_;
};

/// Sugar: a use of a typedef name.
class TypedefType final : public Type {
public:
    explicit TypedefType(TypedefDecl* decl) : Type(TypeKind::Typedef), decl_(decl) {}
    static bool classof(const Type* type) { return type->kind() == TypeKind::Typedef; }

    [[nodiscard]] TypedefDecl* decl() const { return decl_; }

private:
    TypedefDecl* decl_;
};

/// _Complex T. Parsed and typed so that headers declaring complex functions work; the interpreter rejects values.
class ComplexType final : public Type {
public:
    explicit ComplexType(QualType element) : Type(TypeKind::Complex), element_(element) {}
    static bool classof(const Type* type) { return type->kind() == TypeKind::Complex; }

    [[nodiscard]] QualType element() const { return element_; }

private:
    QualType element_;
};

/// Sugar: `typeof(expression)`, and the type of an `__auto_type` declaration (its initializer's type after decay).
/// `typeof(type-name)` is just that type. Sema resolves it.
class TypeofType final : public Type {
public:
    TypeofType(Expr* expression, bool isAutoType)
        : Type(TypeKind::Typeof), expression_(expression), isAutoType_(isAutoType) {}
    static bool classof(const Type* type) { return type->kind() == TypeKind::Typeof; }

    [[nodiscard]] Expr* expression() const { return expression_; }
    [[nodiscard]] bool isAutoType() const { return isAutoType_; }
    /// The type of the expression; null until sema resolved it.
    [[nodiscard]] QualType resolved() const { return resolved_; }
    void resolve(QualType type) const { resolved_ = type; }

private:
    Expr* expression_;
    bool isAutoType_;
    mutable QualType resolved_;
};

} // namespace nitwit::ast
