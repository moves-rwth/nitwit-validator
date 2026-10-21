# Module `lex`

Turns a preprocessed C file into a vector of tokens, and decodes literal spellings.

- `token.hpp`: `TokenKind`, `Token`. A token stores its location and length; the spelling is in the source.
- `lexer.hpp`: `lexFile`. Handles line markers, `#line`, `#pragma`, `#ident`; reports other directives through
  `LexResult::needsPreprocessing`.
- `literals.hpp`: decoding of integer, floating, character and string literals.

## Behaviour to keep

- Every token knows its exact first byte. Columns are byte-based and 1-based (`SourceManager::lineColumn`), as in the
  witness formats.
- Identifiers may contain `$` and bytes ≥ 0x80 (GCC accepts UTF-8 in identifiers).
- A pp-number (C17 6.4.8) is classified as an integer or a floating literal; a pp-number that is neither is lexed as one
  token and diagnosed when it is decoded.
- Keywords include the GNU spellings listed in `token.hpp`. `__builtin_va_list`, `__int128_t`, `__uint128_t` and the
  `__builtin_*` functions are identifiers; sema declares them.

## Tests

- `tests/unit/lex/`: every token kind; digraphs; literals with all prefixes, suffixes and escapes; line markers and
  `#line`; `#pragma`; detection of `#include`/`#define`/`#if`; backslash-newline; columns of tokens after tabs; files
  without a trailing newline; unterminated comments, strings and character constants.
- `tests/corpus/lex/` (label `corpus`): lexes every program in `NITWIT_DATASET_DIR/programs`. It checks
  that only whitespace, comments and handled directives lie between tokens. It reports the files that need the
  preprocessor, and the throughput.
