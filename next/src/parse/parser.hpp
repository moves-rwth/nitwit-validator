#pragma once

// Recursive-descent parser for C17 with the GNU extensions found in sv-benchmarks, producing the syntactic AST with all
// names bound to their declarations.

#include <span>

#include "ast/context.hpp"
#include "ast/decl.hpp"
#include "lex/token.hpp"
#include "support/diagnostics.hpp"
#include "support/source_manager.hpp"

namespace nitwit::parse {

/// Parses a translation unit from the tokens of lex::lexFile. Stops at the first error, reports it to `diagnostics`
/// and returns null. Warnings (e.g. for implicit function declarations) do not stop parsing.
[[nodiscard]] ast::TranslationUnit* parseTranslationUnit(std::span<const lex::Token> tokens, ast::AstContext& context,
                                                         const SourceManager& sources, DiagnosticEngine& diagnostics);

} // namespace nitwit::parse
