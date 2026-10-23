# NITWIT 4: plan for the C++23 re-implementation

## Goals

1. **The same job as NITWIT 3.x.** Validate violation witnesses for `unreach-call` by executing the program concretely.
   Accept witness format 2.x (YAML) first and 1.0 (GraphML) later. Keep the command line and the exit codes, so that
   BenchExec and fm-tools keep working.
2. **Clean and modular.** A real C front end (lexer, parser, semantic analysis) produces a typed AST. A separate
   interpreter executes it, with a precise memory model. The witness layer only observes the execution and supplies
   nondeterministic values. Each module has a small public API and its own tests.
3. **Correct by construction and by comparison.** C semantics are checked against GCC and Clang (differential testing).
   Witness validation is checked against the SV-COMP 2026 witness corpus and NITWIT 3.x.
4. **Designed for what 3.x could not do.**
   - Deterministic replay with restarts, so that nondeterministic values can be searched instead of guessed (P3).
   - Value provenance, i.e. which nondeterministic input a value is an unmodified copy of.
   - Exact source ranges for every token.
   - Undefined-behaviour checks, which the properties `no-overflow` and `valid-memsafety` will need later.

Understandable, maintainable code comes first. Performance comes second, but it matters: a typed-AST interpreter
should be far faster than 3.x, which re-parses the source text on every execution of a statement.

### Non-goals (for now)

- A full C preprocessor. Only 31 of the 19,693 programs in the SV-COMP 2026 corpus contain `#include`, and only 3
  contain `#define`. Line markers and `#pragma` are handled by the lexer; other files go through the system `cpp`.
- Threads (`pthread_create`), `setjmp`/`longjmp`, inline assembly. Programs that use them get UNKNOWN.
- Witness format 2.x `cycle` waypoints (termination) and the properties other than `unreach-call`, until stage 9.

## Where the code lives

NITWIT 4 was built in `next/` on the branch `next`, next to NITWIT 3.x, so that it could use the test files in
`testfiles/` and 3.x could be built for comparisons. Since it replaced 3.x, it is the root of the branch `main`; 3.x is
on the branch `old` (formerly `master`).

## Stages

Each stage ends with an exit check. The numbers it produces are recorded in `docs/status.md`. A stage is done when
its exit criteria hold with GCC and Clang and in the sanitizer build.

| Stage | Content | Depends on | Exit criteria |
|---|---|---|---|
| 0 | **Foundation:** repository layout, CMake presets, warnings, sanitizers, GoogleTest, clang-format, CI, docs; module `support` (source manager, diagnostics, target data models, string interning) | – | builds warning-free with GCC 14 and Clang 20; unit tests pass under ASan and UBSan |
| 1 | **Lexer:** tokens with exact ranges, literals, line markers, `#pragma`, detection of files that need the preprocessor | 0 | lexes all 19,693 corpus programs; re-spelling the tokens reproduces the source except for whitespace and comments |
| 2 | **AST and parser:** C17 plus the GNU extensions used in sv-benchmarks; AST dump | 1 | parses ≥ 99.5 % of the corpus programs that `gcc -fsyntax-only` accepts; golden AST dumps; no crash on fuzzed input |
| 3 | **Semantic analysis:** declarations and scopes, types, ILP32/LP64 layouts, constant evaluation, implicit conversions as explicit AST nodes | 2 | `sizeof`/`_Alignof`/`offsetof` of every record and constant expressions agree with GCC (`-m32`, `-m64`) on generated probes; ≥ 99 % of the corpus passes |
| 4 | **Interpreter:** memory model, values and arithmetic, lowering of statements, expression evaluation, libc builtins, nondeterministic inputs through an oracle; replay mode | 3 (memory and arithmetic only need 0) | semantic test suite identical to GCC and Clang; 1,000 Csmith programs and corpus programs with random inputs identical to the native run (except where the native run is flagged by UBSan) |
| 5 | **Witness models and parsers:** format 2.x (YAML) and 1.0 (GraphML) | 0 | all corpus witnesses parse or are rejected with a clear message; no crash on fuzzed input |
| 6 | **Validation of format 2.x and the driver:** constraint compilation, segment monitor, evaluation points, input oracle with restarts, verdicts, exit codes | 4, 5 | `yaml-witnesses` and the 2.x tests in `sv-comp-2019` pass; on `validation-crafted` at least 3.x (77/100) with 0 invalid witnesses confirmed; on the 250-witness sample at least the confirmations of 3.x with 0 wrong confirmations |
| 7 | **Value search:** terms of input-derived values; solving waypoint requirements for inputs, one run per prediction | 6 | measurable gain on the Ultimate witnesses (Automizer, Taipan, Kojak) and on `validation-crafted`; still 0 wrong confirmations |
| 8 | **Format 1.0 (GraphML)** | 6 | the 84 GraphML test witnesses give at least the 3.x results |
| 9 | **Release:** `nitwit.sh`, archive, smoke test, the C++ runtime linked statically, performance comparison with 3.x, complete docs | 6 (8 for GraphML) | fm-tools archive checks and the fm-weck smoke test pass; CPU time on the witnesses both tools decide (no timeout, no limit of either tool) ≤ 3.x; a run ends early only where the witness can no longer be followed |
| later | own preprocessor; `no-overflow` and `valid-memsafety`; concolic search with an SMT solver; threads | | |

### Which stages run in parallel

```
wave 1:  [0 foundation] ──► API headers for support, lex, witness, memory
wave 2:  lexer (1)   │ witness parsers (5) │ memory + values + arithmetic (part of 4) │ difftest tooling
wave 3:  parser (2)                              (AST headers written while wave 2 runs)
wave 4:  semantic analysis (3)
wave 5:  lowering + evaluator + builtins (rest of 4)
wave 6:  validation of 2.x + driver (6)
wave 7:  value search (7) │ GraphML monitor (8)
wave 8:  release (9)
```

## Way of working

The integrator is the main session. It owns the public API headers, `docs/` and the branch `main`.

1. **API first.** For each module, the integrator writes the public header(s), a short module `README.md` and the test
   plan, and commits them. API changes after that go through the integrator.
2. **Implementation by a sub-agent.** The sub-agent works in its own git worktree and build directory. It implements
   the module and its tests, builds with GCC and Clang, runs the tests under the sanitizers, and reports what it did
   and what it could not do.
3. **Review by a fresh agent.** The reviewer knows nothing but the module's API, the guidelines and the diff. It checks
   correctness, simplicity, comments, tests and adherence to the API. Its findings go back to the implementer (or to a
   new agent).
4. **Integration.** The integrator builds and tests again, merges into `next` (one commit per module or step) and
   updates `docs/status.md`.
5. **Stage exit.** The integrator runs the corpus checks of the stage and records the numbers.

Rules for every change: `docs/guidelines.md`. Design decisions and module boundaries: `docs/architecture.md`.

## Test tiers

| Tier | What | When | CTest label |
|---|---|---|---|
| unit | GoogleTest per module, fast and deterministic | every build | `unit` |
| golden | input file → expected dump (tokens, AST, types, interpreter output); `--update` regenerates | every build | `golden` |
| semantics | small C programs in `tests/semantics/`, run by the interpreter and compiled natively with GCC and Clang, `-m32` and `-m64`; the outputs must agree | every build (needs GCC and Clang) | `semantics` |
| difftest | Csmith programs and corpus programs with random inputs; native vs. interpreter | before a merge into `next`, nightly | `difftest` |
| corpus | lexing/parsing/sema of the corpus; witness validation of `validation-crafted`, the sample and `testfiles/` | stage exits | `corpus` |
| fuzz | libFuzzer on lexer, parser and witness parsers (Clang only) | before stage exits | – |

The corpus lives outside the repository: the CMake cache variable `NITWIT_DATASET_DIR` points to
`svcomp-2026-witnesses` (`/mnt/e/workspace/playground/svcomp-2026-witnesses` in WSL). The witnesses and programs in
it are untrusted data: they are read, never executed, except as compiled programs in the difftest harness.

### Differential testing against GCC and Clang

- **Harness:** `tools/difftest/harness.c` implements `__VERIFIER_nondet_*` from an input file and logs each value it
  hands out. It also implements `reach_error` and `__VERIFIER_assume`, each of which prints a marker and exits with a
  fixed code; `abort` is the C library's (`SIGABRT`, no marker).
- **Native side:** the harness is linked to the program, compiled with `gcc`/`clang`, `-m32`/`-m64`, `-O0`, and once
  more with `-fsanitize=undefined,address`, so that runs with undefined behaviour can be discarded.
- **Interpreter side:** `nitwit --replay <inputs>` uses the same input file.
- **Comparison:** stdout, exit code, the error marker and the sequence of inputs used. Csmith programs print a checksum
  of all globals, which makes them a strong check on integer promotions, layouts and pointer arithmetic.

## Risks

| Risk | Mitigation |
|---|---|
| C is large | the corpus decides what to implement first; differential testing finds the semantic gaps |
| Results differ from 3.x in ways that are hard to explain | per-witness comparison against 3.x; every difference is classified in `docs/status.md` |
| Not ready for SV-COMP 2028 | 3.x stays the release line until NITWIT 4 is at least as good on the corpus |
| Sub-agents drift in style or design | APIs fixed by the integrator, `docs/guidelines.md`, clang-format, a fresh reviewer for every module |
| Builds on `/mnt/e` are slow in WSL | build directories in the WSL home directory, see `docs/development.md` |

## Toolchain

GCC 14 (`gcc-14`, `g++-14`) and Clang 20 (`clang-20`, `clang-tidy-20`, `clang-format-20`), both with libstdc++ 14, on
Ubuntu 24.04; CMake ≥ 3.28, Ninja. libstdc++ 14 determines the usable subset of C++23, see `docs/guidelines.md`. The
Ubuntu defaults GCC 13 and Clang 18 are not used: Clang 18 cannot use libstdc++'s `std::expected` (it requires
`__cpp_concepts >= 202002L`, which Clang sets from version 19 on).

Still to install for stage 4: `sudo apt install csmith`.
