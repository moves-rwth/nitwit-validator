# NITWIT 4

NITWIT validates violation witnesses of C programs, as in SV-COMP: it executes the program with an interpreter,
follows the witness, and searches for input values where the witness needs them. A witness is confirmed only by one
complete run in which every waypoint had its outcome. NITWIT 4 is written in C++23: a C front end produces a typed
AST, an interpreter executes it with a precise memory model, and the validation layer observes the execution. It
replaces NITWIT 3.x (branch `old`) and keeps its command line and exit codes, with the differences listed in
`docs/release.md` and `docs/exit-codes.md`.

## Quick start

On Linux with GCC 14, CMake and Ninja (`docs/development.md`), from the root of the repository:

```bash
cmake --preset gcc-release && cmake --build --preset gcc-release
~/nitwit-build$PWD/gcc-release/src/driver/nitwit -64 -w witness.yml -p unreach-call.prp program.c
ctest --preset gcc-release            # the tests
tools/package.sh                      # the release archive for BenchExec and fm-tools (docs/release.md)
```

The last line of the output is the result (`VALIDATED:`, `FAILED:`, `UNKNOWN:` or `ERROR:`), and the exit code says
the same to BenchExec (`docs/exit-codes.md`).

## What NITWIT supports

- **Property:** `unreach-call`. Any other property gives UNKNOWN (exit 4).
- **Witnesses:** violation witnesses in format 2.x (YAML; SV-COMP 2026 uses 2.0 and 2.1). Witnesses with `cycle`
  waypoints or a `thread_id` other than 0 give UNKNOWN, and so do witnesses in format 1.0 (GraphML).
- **Data models:** ILP32 (`-32`, the default) and LP64 (`-64`), with the types, layouts and arithmetic of GCC on i386
  and x86-64.
- **C:** C17 with the GNU extensions that sv-benchmarks uses (`src/parse/README.md`), pre-C99 code (implicit `int`,
  K&R definitions, implicit declarations), and the C library functions of sv-benchmarks with glibc's behaviour
  (`src/interp/README.md`). Files with preprocessor directives other than line markers, `#line`, `#ident`, `#pragma`
  and the null directive go through the system `cpp`.
- **Not supported** (UNKNOWN), the main items: threads, `setjmp`/`longjmp`, inline assembly other than an empty
  template, files, signals, `atexit`, the `scanf` family, computed `goto`, nested functions, arithmetic on `_Complex`
  values. `__float128` is computed as `long double`. The module READMEs list the rest (`src/lex/README.md`,
  `src/parse/README.md`, `src/sema/README.md`, `src/interp/README.md`, `src/validate/README.md`).

## Results

<!-- The headline numbers of docs/evaluation.md; update both together. -->
On 1000 unreach-call witnesses of SV-COMP 2026 (100 per producer), NITWIT 4.0 confirms 587 of the 851 witnesses of
buggy programs, NITWIT 3.x 312. Neither confirms a witness of a correct program or an invalid crafted witness. On the
903 witnesses both decide, NITWIT 4.0 takes 27 s of CPU time, 3.x 348 s. Details: `docs/evaluation.md`.

The test suites (`docs/development.md`, "Tests"), with their last results:

| Suite | Result |
|---|---|
| unit tests | 1,936 tests of the 10 modules, all passing |
| golden dumps | 22 ASTs; 26 typed ASTs, 4 of them for ILP32 |
| semantics | 50 programs with the output, outcome and inputs of GCC and Clang, with `-m32` and `-m64`, also with the terms checked: 200 tests, all passing |
| difftest | per program with `difftest.py run --nitwit`: 0 disagreements with GCC and Clang on 841 Csmith programs; 2,468 of 2,481 corpus runs with random inputs agree (the others use `setjmp` or `fesetround`) |
| corpus: lexer | all 19,752 corpus and test files, 628 M tokens, 0 errors |
| corpus: parser | all 19,714 corpus and test files without directives, and the 34 that need `cpp` after `gcc -E`; 4.9 M fuzz runs without findings |
| corpus: sema | 19,703 of 19,714 programs (the 11 others use `__int128` on i386, which GCC rejects too); 589,000 layout and constant probes against GCC, 0 mismatches except glibc's `max_align_t` with `__float128` on i386 |
| corpus: witnesses | 58,093 of 58,119 corpus witnesses parse (the 26 others have `line: 0`), and all test witnesses |
| corpus: validation | `testfiles/`: all 45 format 2.x witnesses; `validation-crafted`: 96 of 100 right, no invalid witness confirmed (3.x: 77 right); the 250-witness sample: 139 of 191 buggy programs confirmed, none wrong (3.x: 65) |
| release | `nitwit.sh` and the smoke test of the archive pass; so does fm-weck's smoke test in fm-tools' CI (Ubuntu 26.04, container mode, the SV-COMP 2027 image) |

## Documentation

- [`docs/architecture.md`](docs/architecture.md): goals, modules and key design decisions
- [`docs/guidelines.md`](docs/guidelines.md): coding guidelines and review checklist
- [`docs/development.md`](docs/development.md): toolchain, building, tests, contributing
- [`docs/exit-codes.md`](docs/exit-codes.md): exit codes (compatible with NITWIT 3.x)
- [`docs/release.md`](docs/release.md): the release archive for BenchExec and fm-tools, and how to release
- [`docs/evaluation.md`](docs/evaluation.md): NITWIT 4.0 against 3.x on 1000 SV-COMP 2026 witnesses
- [`docs/roadmap.md`](docs/roadmap.md): open work
- the module READMEs in `src/<module>/README.md`: semantics, rules and known gaps of each module
- [`tools/difftest/README.md`](tools/difftest/README.md): differential testing against GCC and Clang
- [`release/README.md`](release/README.md) and [`release/THIRD-PARTY.md`](release/THIRD-PARTY.md): the README and
  the third-party notices of the release archive
- [`LICENSE`](LICENSE): NITWIT 4 is licensed under the New BSD license (BSD-3-Clause)
