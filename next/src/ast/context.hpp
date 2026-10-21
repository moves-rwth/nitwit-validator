#pragma once

// Owns all AST nodes and types of one program, in an arena, and creates and uniques types.

#include <cstdint>
#include <memory>
#include <memory_resource>
#include <new>
#include <span>
#include <type_traits>
#include <utility>

#include "ast/type.hpp"
#include "support/interner.hpp"
#include "support/target.hpp"

namespace nitwit::ast {

class Expr;

class AstContext {
public:
    AstContext(const TargetInfo& target, StringInterner& interner);
    ~AstContext();
    AstContext(const AstContext&) = delete;
    AstContext& operator=(const AstContext&) = delete;

    [[nodiscard]] const TargetInfo& target() const { return target_; }
    [[nodiscard]] StringInterner& interner() { return interner_; }

    /// Creates a node (Decl, Expr, Stmt) in the arena. Nodes are never destroyed, so they must be trivially
    /// destructible: they hold spans into the arena, not containers.
    template <typename T, typename... Args>
    T* create(Args&&... args) {
        static_assert(std::is_trivially_destructible_v<T>, "AST nodes must be trivially destructible");
        return new (arena_.allocate(sizeof(T), alignof(T))) T(std::forward<Args>(args)...);
    }

    /// Copies elements into the arena, for the spans that nodes hold.
    template <typename T>
    std::span<T> copy(std::span<const T> elements) {
        static_assert(std::is_trivially_destructible_v<T> && std::is_trivially_copyable_v<T>);
        if (elements.empty()) {
            return {};
        }
        auto* memory = static_cast<T*>(arena_.allocate(elements.size_bytes(), alignof(T)));
        std::uninitialized_copy(elements.begin(), elements.end(), memory);
        return {memory, elements.size()};
    }

    // ---- Types ----

    [[nodiscard]] QualType builtin(BuiltinKind kind) const;
    [[nodiscard]] QualType pointerTo(QualType pointee);
    /// An array as written: `[size]` (a new, not uniqued type, resolved by sema) or `[]` if sizeExpression is null.
    [[nodiscard]] QualType arrayOf(QualType element, Expr* sizeExpression);
    [[nodiscard]] QualType constantArrayOf(QualType element, std::uint64_t size);
    /// Parameter types must already be adjusted (arrays and functions to pointers).
    [[nodiscard]] QualType functionType(QualType returnType, std::span<const QualType> parameters, bool isVariadic,
                                        bool hasPrototype);
    [[nodiscard]] QualType recordType(RecordDecl* decl);
    [[nodiscard]] QualType enumType(EnumDecl* decl);
    [[nodiscard]] QualType typedefType(TypedefDecl* decl);
    [[nodiscard]] QualType complexOf(QualType element);
    /// `typeof(expression)` or the type of an `__auto_type` declaration; a new type, resolved by sema.
    [[nodiscard]] QualType typeofExpression(Expr* expression, bool isAutoType);

    /// The canonical type: typedef and typeof replaced by what they stand for, array sizes resolved, the result
    /// uniqued. Qualifiers of typedef'd types are merged in; qualifiers of an array type move to its elements
    /// (C17 6.7.3p10). Precondition: sema resolved every array size and typeof the type refers to. Variable length
    /// array types are their own canonical types.
    [[nodiscard]] QualType canonical(QualType type);

private:
    const TargetInfo& target_;
    StringInterner& interner_;
    std::pmr::monotonic_buffer_resource arena_;
    struct Types; // builtin singletons and uniquing tables
    std::unique_ptr<Types> types_;
};

} // namespace nitwit::ast
