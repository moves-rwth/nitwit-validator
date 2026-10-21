#pragma once

// Declarations. The parser creates one Decl per declaration as written and binds every name use to the declaration it
// refers to (C name binding is purely lexical). Redeclarations of the same entity (`extern int x; int x = 1;`, a
// prototype and the definition of a function) are linked through `previous()`; semantic analysis merges their types.

#include <cstdint>
#include <span>

#include "ast/type.hpp"
#include "support/int128.hpp"
#include "support/interner.hpp"
#include "support/source_manager.hpp"

namespace nitwit::ast {

class Expr;
class CompoundStmt;

enum class DeclKind : std::uint8_t { Var, Function, Field, EnumConstant, Typedef, Record, Enum, StaticAssert };

/// A GNU attribute, e.g. `aligned(8)` or `mode(__QI__)`. The name has leading and trailing "__" removed. Identifier
/// arguments are DeclRefExprs without a declaration.
struct Attribute {
    Symbol name;
    SourceLocation location;
    std::span<Expr* const> arguments;
};

class Decl {
public:
    Decl(const Decl&) = delete;
    Decl& operator=(const Decl&) = delete;

    [[nodiscard]] DeclKind kind() const { return kind_; }
    /// Empty for anonymous records, enums, bit-fields and parameters.
    [[nodiscard]] Symbol name() const { return name_; }
    /// The location of the name, or of the keyword if there is no name.
    [[nodiscard]] SourceLocation location() const { return location_; }
    [[nodiscard]] std::span<const Attribute> attributes() const { return attributes_; }
    void setAttributes(std::span<const Attribute> attributes) { attributes_ = attributes; }

protected:
    Decl(DeclKind kind, Symbol name, SourceLocation location) : kind_(kind), name_(name), location_(location) {}
    ~Decl() = default;

private:
    DeclKind kind_;
    Symbol name_;
    SourceLocation location_;
    std::span<const Attribute> attributes_;
};

/// A declaration with a type that names an object, a function, a member or an enumeration constant.
class ValueDecl : public Decl {
public:
    static bool classof(const Decl* decl) {
        return decl->kind() == DeclKind::Var || decl->kind() == DeclKind::Function || decl->kind() == DeclKind::Field
               || decl->kind() == DeclKind::EnumConstant;
    }

    [[nodiscard]] QualType type() const { return type_; }
    /// Sema replaces the written type when it merges redeclarations or completes an array from its initializer.
    void setType(QualType type) { type_ = type; }

protected:
    ValueDecl(DeclKind kind, Symbol name, SourceLocation location, QualType type)
        : Decl(kind, name, location), type_(type) {}
    ~ValueDecl() = default;

private:
    QualType type_;
};

enum class StorageClass : std::uint8_t { None, Extern, Static, Auto, Register };

class VarDecl final : public ValueDecl {
public:
    VarDecl(Symbol name, SourceLocation location, QualType type, StorageClass storage, bool isFileScope,
            bool isParameter)
        : ValueDecl(DeclKind::Var, name, location, type), storage_(storage), isFileScope_(isFileScope),
          isParameter_(isParameter) {}
    static bool classof(const Decl* decl) { return decl->kind() == DeclKind::Var; }

    [[nodiscard]] StorageClass storage() const { return storage_; }
    [[nodiscard]] bool isFileScope() const { return isFileScope_; }
    [[nodiscard]] bool isParameter() const { return isParameter_; }
    [[nodiscard]] bool isThreadLocal() const { return isThreadLocal_; }
    void setThreadLocal() { isThreadLocal_ = true; }
    /// Static storage duration (C17 6.2.4p3): file scope, or `static`/`extern` in a block.
    [[nodiscard]] bool hasStaticStorage() const {
        return isFileScope_ || storage_ == StorageClass::Static || storage_ == StorageClass::Extern;
    }

    [[nodiscard]] Expr* initializer() const { return initializer_; }
    void setInitializer(Expr* initializer) { initializer_ = initializer; }

    /// The previous declaration of the same object, or null.
    [[nodiscard]] VarDecl* previous() const { return previous_; }
    void setPrevious(VarDecl* previous) { previous_ = previous; }

    /// Sema: for automatic variables and parameters, the index among the locals of the enclosing function
    /// (FunctionDecl::locals()); the interpreter uses it to find the variable's frame slot.
    [[nodiscard]] std::uint32_t localIndex() const { return localIndex_; }
    void setLocalIndex(std::uint32_t index) { localIndex_ = index; }

    /// GNU `aligned` attribute or _Alignas, resolved by sema; 0 if none.
    [[nodiscard]] std::uint64_t requestedAlignment() const { return requestedAlignment_; }
    void setRequestedAlignment(std::uint64_t alignment) { requestedAlignment_ = alignment; }

private:
    StorageClass storage_;
    bool isFileScope_;
    bool isParameter_;
    bool isThreadLocal_ = false;
    Expr* initializer_ = nullptr;
    VarDecl* previous_ = nullptr;
    std::uint32_t localIndex_ = 0;
    std::uint64_t requestedAlignment_ = 0;
};

class FunctionDecl final : public ValueDecl {
public:
    FunctionDecl(Symbol name, SourceLocation location, QualType type, StorageClass storage)
        : ValueDecl(DeclKind::Function, name, location, type), storage_(storage) {}
    static bool classof(const Decl* decl) { return decl->kind() == DeclKind::Function; }

    [[nodiscard]] StorageClass storage() const { return storage_; }
    [[nodiscard]] bool isInline() const { return isInline_; }
    void setInline() { isInline_ = true; }
    [[nodiscard]] bool isNoreturn() const { return isNoreturn_; }
    void setNoreturn() { isNoreturn_ = true; }
    /// Declared implicitly by a call to an undeclared identifier (C89, still accepted by GCC).
    [[nodiscard]] bool isImplicit() const { return isImplicit_; }
    void setImplicit() { isImplicit_ = true; }

    /// The parameters of a definition (and of a prototype, where they are named or not).
    [[nodiscard]] std::span<VarDecl* const> parameters() const { return parameters_; }
    void setParameters(std::span<VarDecl* const> parameters) { parameters_ = parameters; }

    /// Null for a declaration that is not a definition.
    [[nodiscard]] CompoundStmt* body() const { return body_; }
    void setBody(CompoundStmt* body) { body_ = body; }

    [[nodiscard]] FunctionDecl* previous() const { return previous_; }
    void setPrevious(FunctionDecl* previous) { previous_ = previous; }

    /// Sema: the parameters followed by all automatic variables of the body, in declaration order.
    [[nodiscard]] std::span<VarDecl* const> locals() const { return locals_; }
    void setLocals(std::span<VarDecl* const> locals) { locals_ = locals; }

private:
    StorageClass storage_;
    bool isInline_ = false;
    bool isNoreturn_ = false;
    bool isImplicit_ = false;
    std::span<VarDecl* const> parameters_;
    CompoundStmt* body_ = nullptr;
    FunctionDecl* previous_ = nullptr;
    std::span<VarDecl* const> locals_;
};

class RecordDecl;

class FieldDecl final : public ValueDecl {
public:
    FieldDecl(Symbol name, SourceLocation location, QualType type, Expr* bitWidth)
        : ValueDecl(DeclKind::Field, name, location, type), bitWidth_(bitWidth) {}
    static bool classof(const Decl* decl) { return decl->kind() == DeclKind::Field; }

    [[nodiscard]] RecordDecl* parent() const { return parent_; }
    void setParent(RecordDecl* parent, std::uint32_t index) {
        parent_ = parent;
        index_ = index;
    }
    /// The position among the fields of the parent record.
    [[nodiscard]] std::uint32_t index() const { return index_; }

    [[nodiscard]] bool isBitField() const { return bitWidth_ != nullptr; }
    [[nodiscard]] Expr* bitWidthExpression() const { return bitWidth_; }
    /// Sema: the evaluated bit width.
    [[nodiscard]] std::uint32_t bitWidth() const { return bitWidthValue_; }
    void setBitWidth(std::uint32_t width) { bitWidthValue_ = width; }

    /// An unnamed member whose written type is a struct or union (C17 6.7.2.1p13); its members are found through it.
    [[nodiscard]] bool isAnonymousRecord() const;

    /// Sema: from `aligned` or _Alignas; 0 if none.
    [[nodiscard]] std::uint64_t requestedAlignment() const { return requestedAlignment_; }
    void setRequestedAlignment(std::uint64_t alignment) { requestedAlignment_ = alignment; }
    [[nodiscard]] bool isPacked() const { return isPacked_; }
    void setPacked() { isPacked_ = true; }

private:
    Expr* bitWidth_;
    RecordDecl* parent_ = nullptr;
    std::uint32_t index_ = 0;
    std::uint32_t bitWidthValue_ = 0;
    std::uint64_t requestedAlignment_ = 0;
    bool isPacked_ = false;
};

class EnumConstantDecl final : public ValueDecl {
public:
    EnumConstantDecl(Symbol name, SourceLocation location, Expr* initializer)
        : ValueDecl(DeclKind::EnumConstant, name, location, QualType()), initializer_(initializer) {}
    static bool classof(const Decl* decl) { return decl->kind() == DeclKind::EnumConstant; }

    [[nodiscard]] Expr* initializer() const { return initializer_; }
    /// Sema: the value (C17 6.7.2.2p3: the previous value plus one if there is no initializer).
    [[nodiscard]] Int128 value() const { return value_; }
    void setValue(Int128 value) { value_ = value; }

private:
    Expr* initializer_;
    Int128 value_ = 0;
};

class TypedefDecl final : public Decl {
public:
    TypedefDecl(Symbol name, SourceLocation location, QualType underlying)
        : Decl(DeclKind::Typedef, name, location), underlying_(underlying) {}
    static bool classof(const Decl* decl) { return decl->kind() == DeclKind::Typedef; }

    [[nodiscard]] QualType underlying() const { return underlying_; }
    /// Sema replaces the type for the `mode` attribute (e.g. `typedef int int8_t __attribute__((mode(QI)))`).
    void setUnderlying(QualType type) { underlying_ = type; }

private:
    QualType underlying_;
};

/// The byte offsets and bit-field positions of a record's fields, computed by sema.
struct RecordLayout {
    std::uint64_t size = 0;
    std::uint64_t alignment = 1;
    std::span<const std::uint64_t> fieldOffsets; // in bytes; for a bit-field, the byte holding its first bit
    std::span<const std::uint8_t> bitOffsets;    // bit-fields: first bit within that byte (0 = least significant)
};

/// A struct or union. All declarations of the same tag in the same scope are the same RecordDecl entity (the parser
/// resolves tags, C17 6.7.2.3); the members are set when the definition is parsed.
class RecordDecl final : public Decl {
public:
    RecordDecl(Symbol name, SourceLocation location, bool isUnion)
        : Decl(DeclKind::Record, name, location), isUnion_(isUnion) {}
    static bool classof(const Decl* decl) { return decl->kind() == DeclKind::Record; }

    [[nodiscard]] bool isUnion() const { return isUnion_; }
    [[nodiscard]] bool isComplete() const { return isComplete_; }
    [[nodiscard]] std::span<FieldDecl* const> fields() const { return fields_; }
    /// Sets the members and marks the record complete.
    void complete(std::span<FieldDecl* const> fields);

    // Sema: attributes and layout.
    [[nodiscard]] bool isPacked() const { return isPacked_; }
    void setPacked() { isPacked_ = true; }
    [[nodiscard]] std::uint64_t requestedAlignment() const { return requestedAlignment_; }
    void setRequestedAlignment(std::uint64_t alignment) { requestedAlignment_ = alignment; }
    /// Null until sema computed it.
    [[nodiscard]] const RecordLayout* layout() const { return layout_; }
    void setLayout(const RecordLayout* layout) { layout_ = layout; }

private:
    bool isUnion_;
    bool isComplete_ = false;
    bool isPacked_ = false;
    std::span<FieldDecl* const> fields_;
    std::uint64_t requestedAlignment_ = 0;
    const RecordLayout* layout_ = nullptr;
};

class EnumDecl final : public Decl {
public:
    EnumDecl(Symbol name, SourceLocation location) : Decl(DeclKind::Enum, name, location) {}
    static bool classof(const Decl* decl) { return decl->kind() == DeclKind::Enum; }

    [[nodiscard]] bool isComplete() const { return isComplete_; }
    [[nodiscard]] std::span<EnumConstantDecl* const> constants() const { return constants_; }
    void complete(std::span<EnumConstantDecl* const> constants) {
        constants_ = constants;
        isComplete_ = true;
    }

    /// Sema: the compatible integer type, as GCC chooses it (unsigned int if no value is negative, ...).
    [[nodiscard]] QualType integerType() const { return integerType_; }
    void setIntegerType(QualType type) { integerType_ = type; }

private:
    bool isComplete_ = false;
    std::span<EnumConstantDecl* const> constants_;
    QualType integerType_;
};

/// _Static_assert; checked by sema.
class StaticAssertDecl final : public Decl {
public:
    StaticAssertDecl(SourceLocation location, Expr* condition, Expr* message)
        : Decl(DeclKind::StaticAssert, Symbol(), location), condition_(condition), message_(message) {}
    static bool classof(const Decl* decl) { return decl->kind() == DeclKind::StaticAssert; }

    [[nodiscard]] Expr* condition() const { return condition_; }
    /// A StringLiteralExpr, or null (C23 allows omitting it).
    [[nodiscard]] Expr* message() const { return message_; }

private:
    Expr* condition_;
    Expr* message_;
};

/// The whole program: the external declarations in source order.
struct TranslationUnit {
    std::span<Decl* const> decls;
};

} // namespace nitwit::ast
