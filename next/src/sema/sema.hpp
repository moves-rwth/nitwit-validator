#pragma once

// Semantic analysis: completes the syntactic AST of the parser into the typed AST described in ast/README.md.

#include <string>
#include <vector>

#include "ast/context.hpp"
#include "ast/decl.hpp"
#include "support/diagnostics.hpp"

namespace nitwit::sema {

/// Analyzes a whole translation unit in place. Returns false after reporting the first error; then the AST is only
/// partly analyzed and must not be used further. Accepts what GCC accepts with warnings (e.g. integer/pointer
/// conversions without a cast) and reports those as warnings.
[[nodiscard]] bool analyze(ast::TranslationUnit& unit, ast::AstContext& context, DiagnosticEngine& diagnostics);

/// The names of the functions sema's builtin table types: `__builtin_x` for each entry, and also `x` for the C library
/// functions GCC knows, with the f and l variants of the math functions. The interpreter's library tests that it
/// implements or rejects each of them.
[[nodiscard]] std::vector<std::string> builtinFunctionNames();

} // namespace nitwit::sema
