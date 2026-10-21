#pragma once

// Integer constant expressions (C17 6.6p6) as GCC evaluates them, also through GNU folding of some expressions that are
// not strictly constant (e.g. `(int)(char *)0 + 4`). Used for array sizes, enumeration values, case labels, bit-field
// widths, _Static_assert, attribute arguments and __builtin_choose_expr.

#include <cstdint>
#include <optional>

#include "ast/context.hpp"
#include "ast/expr.hpp"
#include "support/int128.hpp"

namespace nitwit::sema {

/// The value of an analyzed expression of integer type if it is an integer constant expression, converted to the
/// expression's type; nullopt otherwise (also on division by zero and other undefined behaviour).
[[nodiscard]] std::optional<Int128> evaluateIntegerConstant(ast::AstContext& context, const ast::Expr* expr);

/// What GCC gives for an analyzed call `__builtin_object_size(p, type)` or `__builtin_dynamic_object_size(p, type)`
/// when `p` is the address of a declared object or of a member or element of one, possibly converted to another
/// pointer type and moved by a constant, which GCC knows even at -O0 (`buf`, `s.a`, `&s.b`, `buf + 4`):
/// the bytes from `p` to the end of the object (types 0 and 2) or of the closest enclosing member (types 1 and 3).
/// nullopt if the object is not known; then the C library answers (size_t)-1 for types 0 and 1, 0 for 2 and 3.
/// Not an integer constant expression, also in GCC.
[[nodiscard]] std::optional<std::uint64_t> knownObjectSize(ast::AstContext& context, const ast::CallExpr* call);

} // namespace nitwit::sema
