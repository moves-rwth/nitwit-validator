# Module `support`

What every other module needs, without knowledge of C or of witnesses.

- `source_manager.hpp`: `SourceManager`, the source files and positions in them. A `SourceLocation` is a file and a
  byte offset; the source manager maps it to a physical line and column (1-based, counting bytes, so a tab is one
  column, as in the witness formats) and, through line markers (`# 12 "a.c"`), to a presumed location.
- `diagnostics.hpp`: `DiagnosticEngine`, the diagnostics about the input (program or witness) of one validation, and
  their format `file:line:column: severity: message`.
- `error.hpp`: `Error`, the error type of `std::expected` results: a failure caused by the input, not by a bug.
- `assert.hpp`: `NITWIT_ASSERT` and `NITWIT_UNREACHABLE` for invariants of NITWIT itself, active in all build types
  (`docs/architecture.md`, "Errors"); the only macros of the code base.
- `target.hpp`: `TargetInfo`, the data models ILP32 (i386) and LP64 (x86-64) with the sizes and alignments of the
  scalar types under the System V ABI.
- `interner.hpp`: interned strings for identifiers (`Symbol`): equal texts of one interner are equal symbols.
- `int128.hpp`: 128-bit integers, for C's `__int128` and intermediate results; the only place that names the
  compiler's `__int128`.
- `large_stack.hpp`: `runWithLargeStack`, which runs work on a thread with a large stack, since the parser, sema and
  the interpreter recurse on the host stack.

## Tests

`tests/unit/support/`: the line and column mapping, line markers and presumed locations (`source_manager_test.cpp`);
the format of diagnostics; interning; the scalar types of both data models; a deep recursion on the large stack.
