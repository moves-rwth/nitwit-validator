#pragma once

// Questions about types that the interpreter and the validation layer ask, answered for the target of the AstContext.
// All functions take any QualType and canonicalize it themselves.

#include <cstdint>

#include "ast/context.hpp"
#include "ast/decl.hpp"
#include "ast/type.hpp"
#include "value/value.hpp"

namespace nitwit::sema {

[[nodiscard]] bool isVoid(ast::AstContext& context, ast::QualType type);
/// Integer types including _Bool, char and enumerations (C17 6.2.5p17).
[[nodiscard]] bool isInteger(ast::AstContext& context, ast::QualType type);
[[nodiscard]] bool isSignedInteger(ast::AstContext& context, ast::QualType type);
[[nodiscard]] bool isFloating(ast::AstContext& context, ast::QualType type);
[[nodiscard]] bool isArithmetic(ast::AstContext& context, ast::QualType type);
[[nodiscard]] bool isPointer(ast::AstContext& context, ast::QualType type);
/// Arithmetic and pointer types (C17 6.2.5p21).
[[nodiscard]] bool isScalar(ast::AstContext& context, ast::QualType type);
[[nodiscard]] bool isRecord(ast::AstContext& context, ast::QualType type);
/// The pointee of a pointer type; precondition: isPointer.
[[nodiscard]] ast::QualType pointee(ast::AstContext& context, ast::QualType type);

/// The representation of an integer, enumeration or pointer type (pointers are unsigned of the pointer width).
[[nodiscard]] value::IntegerFormat integerFormat(ast::AstContext& context, ast::QualType type);
/// The representation of a real floating type.
[[nodiscard]] value::FloatFormat floatFormat(ast::AstContext& context, ast::QualType type);

/// sizeof of a complete object type other than a variable length array; 1 for void and function types, as GCC
/// computes it for pointer arithmetic.
[[nodiscard]] std::uint64_t sizeOf(ast::AstContext& context, ast::QualType type);
/// _Alignof (ABI alignment).
[[nodiscard]] std::uint64_t alignOf(ast::AstContext& context, ast::QualType type);
/// The layout of a complete record; computed by sema during analysis.
[[nodiscard]] const ast::RecordLayout& layoutOf(const ast::RecordDecl* record);

} // namespace nitwit::sema
