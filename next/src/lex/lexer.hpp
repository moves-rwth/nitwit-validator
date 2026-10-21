#pragma once

// Turns a source file into tokens. The input is preprocessed C: line markers, `#line`, `#pragma` and `#ident` are
// handled here; any other directive means the file has to go through the preprocessor first.

#include <string_view>
#include <vector>

#include "lex/token.hpp"
#include "support/diagnostics.hpp"
#include "support/interner.hpp"
#include "support/source_manager.hpp"

namespace nitwit::lex {

struct LexResult {
    /// All tokens of the file, ending with one EndOfFile token. Incomplete if needsPreprocessing is set.
    std::vector<Token> tokens;
    /// Set when the file contains a directive other than a line marker, `#line`, `#pragma`, `#ident` or `#`.
    bool needsPreprocessing = false;
};

/// Lexes a whole file.
/// - Line markers (`# 12 "a.c" 1 3`) and `#line` directives are registered with the source manager
///   (SourceManager::addLineMarker).
/// - `#pragma` and `#ident` lines are skipped.
/// - A backslash-newline is accepted between tokens and inside string and character literals (the decoders remove
///   it). Anywhere else it is an error.
/// - Characters that start no token give an Unknown token and an error diagnostic; lexing continues after them.
/// - Lexing stops at the first directive that needs the preprocessor.
[[nodiscard]] LexResult lexFile(SourceManager& sources, FileId file, StringInterner& interner,
                                DiagnosticEngine& diagnostics);

/// The source text of a token.
[[nodiscard]] std::string_view spellingOf(const SourceManager& sources, const Token& token);

} // namespace nitwit::lex
