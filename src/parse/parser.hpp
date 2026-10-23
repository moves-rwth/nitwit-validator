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
///
/// The parser recurses on the calling thread's stack, about 1 KiB per nesting level without optimization and more
/// under ASan. Nesting deeper than 10,000 levels is an error, but that only protects a caller with a large stack: run
/// it with runWithLargeStack (support/large_stack.hpp) and at least 256 MiB. It creates no threads.
[[nodiscard]] ast::TranslationUnit* parseTranslationUnit(std::span<const lex::Token> tokens, ast::AstContext& context,
                                                         const SourceManager& sources, DiagnosticEngine& diagnostics);

} // namespace nitwit::parse
