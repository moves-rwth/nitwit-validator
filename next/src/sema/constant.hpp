#pragma once

// Integer constant expressions (C17 6.6p6) as GCC evaluates them, also through GNU folding of some expressions that are
// not strictly constant (e.g. `(int)(char *)0 + 4`). Used for array sizes, enumeration values, case labels, bit-field
// widths, _Static_assert, attribute arguments and __builtin_choose_expr.

#include <optional>

#include "ast/context.hpp"
#include "ast/expr.hpp"
#include "support/int128.hpp"

namespace nitwit::sema {

/// The value of an analyzed expression of integer type if it is an integer constant expression, converted to the
/// expression's type; nullopt otherwise (also on division by zero and other undefined behaviour).
[[nodiscard]] std::optional<Int128> evaluateIntegerConstant(ast::AstContext& context, const ast::Expr* expr);

} // namespace nitwit::sema
