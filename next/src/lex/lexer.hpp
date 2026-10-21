#pragma once

// Turns a source file into tokens. The input is preprocessed C: line markers, `#line`, `#pragma` and `#ident` are
// handled here; any other directive means the file has to go through the preprocessor first.
// Not supported: universal character names in identifiers, raw string literals (a GNU extension in C), and
// backslash-newlines inside tokens other than literals.

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
/// - Each `#pragma` directive becomes one Pragma token at its place in the token stream. Its spelling is the raw text
///   of the directive, from '#' to the end of the line, including any comments and backslash-newlines in it. The
///   parser interprets `#pragma pack` and `#pragma weak` and ignores the other pragmas.
/// - `#ident` and the null directive `#` are skipped.
/// - A directive starts with '#' as the first token of a logical line, as in GCC: not after a block comment that
///   started on an earlier line, and not after a backslash-newline.
/// - A backslash-newline is accepted between tokens and inside string and character literals (the decoders remove
///   it). Joining the characters of two tokens with it is an error. Blanks between the backslash and the newline give
///   a warning, as in GCC.
/// - Characters that start no token give an Unknown token and an error diagnostic; lexing continues after them. Null
///   characters outside literals are ignored with a warning.
/// - Lexing stops at the first directive that needs the preprocessor. The diagnostics reported until then are only
///   meaningful if the caller does not run the preprocessor; a caller that re-lexes the preprocessed output discards
///   them.
[[nodiscard]] LexResult lexFile(SourceManager& sources, FileId file, StringInterner& interner,
                                DiagnosticEngine& diagnostics);

/// The source text of a token.
[[nodiscard]] std::string_view spellingOf(const SourceManager& sources, const Token& token);

} // namespace nitwit::lex
