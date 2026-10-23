#pragma once

// The type rules of C that the analyzer's source files share: classification, GCC's alignments, integer promotions,
// the usual arithmetic conversions, compatibility and composite types. Private to the module. All functions expect
// types whose array sizes and typeofs sema has resolved.

#include <cstdint>
#include <optional>

#include "ast/context.hpp"
#include "ast/decl.hpp"
#include "ast/type.hpp"

namespace nitwit::sema::detail {

/// The kinds of canonical types the rules distinguish. Enumerations count as Integer, `_Bool` as Bool.
enum class TypeClass : std::uint8_t {
    Void,
    Bool,
    Integer,
    Floating,
    Complex,
    Pointer,
    Array,
    Function,
    Record,
    VaList
};

[[nodiscard]] TypeClass classify(ast::AstContext& context, ast::QualType type);

/// Removes typedef and typeof sugar at the top of a type; the qualifiers of the sugar are kept.
[[nodiscard]] ast::QualType desugar(ast::QualType type);

/// The builtin kind of a canonical builtin type, or nullopt for other types (also for enumerations).
[[nodiscard]] std::optional<ast::BuiltinKind> builtinKind(ast::AstContext& context, ast::QualType type);

/// The record a type names (after desugaring), or null.
[[nodiscard]] ast::RecordDecl* recordOf(ast::QualType type);

/// The pointee of a pointer type with its sugar; precondition: the type is a pointer.
[[nodiscard]] ast::QualType pointeeOf(ast::QualType type);

/// The array type a type names (after desugaring), or null.
[[nodiscard]] const ast::ArrayType* arrayOf(ast::QualType type);

/// The function type a type names (after desugaring), or null.
[[nodiscard]] const ast::FunctionType* functionOf(ast::QualType type);

/// A variable length array, or an array of them (C17 6.7.6.2p4).
[[nodiscard]] bool isVariablySized(ast::QualType type);

/// `char`, `signed char` and `unsigned char` (C17 6.2.5p15).
[[nodiscard]] bool isCharacterType(ast::AstContext& context, ast::QualType type);

/// Integer types other than _Bool, the arithmetic types and the scalar types of C17 6.2.5, without complex types.
[[nodiscard]] bool isIntegerClass(TypeClass type);
[[nodiscard]] bool isArithmeticClass(TypeClass type);
[[nodiscard]] bool isScalarClass(TypeClass type);

/// The integer type of an integer or enumeration type: the enumeration's compatible integer type, else the type.
[[nodiscard]] ast::QualType integerTypeOf(ast::AstContext& context, ast::QualType type);

/// The integer conversion rank of C17 6.3.1.1p1 for a builtin integer kind; _Bool has the lowest.
[[nodiscard]] int integerRank(ast::BuiltinKind kind);

/// The integer promotions (C17 6.3.1.1p2) of a non-bit-field type, as GCC applies them to enumerations: their
/// compatible type is promoted. Other types are returned unchanged.
[[nodiscard]] ast::QualType promotedType(ast::AstContext& context, ast::QualType type);

/// The common real type of the usual arithmetic conversions (C17 6.3.1.8) of two promoted arithmetic types.
[[nodiscard]] ast::QualType commonArithmeticType(ast::AstContext& context, ast::QualType lhs, ast::QualType rhs);

/// The unqualified version of a type, keeping typedef sugar where the qualifiers are not part of it.
[[nodiscard]] ast::QualType unqualified(ast::AstContext& context, ast::QualType type);

/// Whether a type is const at the top level, also through typedefs, or an array of const elements.
[[nodiscard]] bool isConstQualified(ast::AstContext& context, ast::QualType type);

/// Compatible types (C17 6.2.7p1, 6.7.6.1-3); enumerations are compatible with their integer type, as in GCC.
[[nodiscard]] bool compatible(ast::AstContext& context, ast::QualType lhs, ast::QualType rhs);
/// Compatible after removing the top-level qualifiers of both types.
[[nodiscard]] bool compatibleUnqualified(ast::AstContext& context, ast::QualType lhs, ast::QualType rhs);

/// The composite type of two compatible types (C17 6.2.7p3); canonical.
[[nodiscard]] ast::QualType composite(ast::AstContext& context, ast::QualType lhs, ast::QualType rhs);

/// GCC's TYPE_ALIGN of a type in bytes: __alignof__(type), which on i386 is 8 for double and long long.
[[nodiscard]] std::uint64_t preferredAlignment(ast::AstContext& context, ast::QualType type);

/// The alignment of a non-bit-field member in its laid-out record (GCC's DECL_ALIGN): `packed`, `aligned` and
/// `#pragma pack` applied to the type's alignment as a member.
[[nodiscard]] std::uint64_t fieldAlignment(ast::AstContext& context, const ast::FieldDecl* field);

/// The byte offset of a field in a laid-out record, also through anonymous members; nullopt if it is not a member.
[[nodiscard]] std::optional<std::uint64_t> fieldOffset(const ast::RecordDecl* record, const ast::FieldDecl* field);

/// The types `size_t`, `ptrdiff_t` and `wchar_t` of the target.
[[nodiscard]] ast::QualType sizeType(ast::AstContext& context);
[[nodiscard]] ast::QualType ptrdiffType(ast::AstContext& context);
[[nodiscard]] ast::QualType wcharType(ast::AstContext& context);

/// The builtin kind of a target scalar type.
[[nodiscard]] ast::BuiltinKind builtinKindOf(ScalarType type);

/// The integer type of the given size in bytes and signedness, as GCC's c_common_type_for_size picks it: int before
/// long before long long.
[[nodiscard]] ast::QualType integerTypeForSize(ast::AstContext& context, std::uint64_t bytes, bool isSigned);

} // namespace nitwit::sema::detail
