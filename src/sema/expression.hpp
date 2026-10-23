#pragma once

// Analysis of expressions created after the translation unit was analyzed: witness constraints.

#include "ast/context.hpp"
#include "ast/expr.hpp"
#include "ast/type.hpp"
#include "support/diagnostics.hpp"

namespace nitwit::sema {

/// Analyzes a constraint like any expression of the program and converts it to an rvalue of scalar type, as a
/// controlling expression would be (C17 6.8.4.1p1). `resultType` is the type of `\result` (the return type of the
/// observed call); null if `\result` is not allowed here. Returns the analyzed root, which may be a new node wrapping
/// `expr`, or null after reporting an error.
[[nodiscard]] ast::Expr* analyzeConstraint(ast::Expr* expr, ast::QualType resultType, ast::AstContext& context,
                                           DiagnosticEngine& diagnostics);

} // namespace nitwit::sema
