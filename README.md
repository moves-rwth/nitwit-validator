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
NITWIT 4.0.0 (the release archive, `main` at ad6f5b502146) on the 27,767 unreach-call witnesses of SV-COMP 2026 in
format 2.x, at SV-COMP's 90 s: it confirms 9,239 of the 14,841 witnesses of buggy programs (62 %) and none of the
12,792 witnesses of correct programs. On the 11,738 witnesses of buggy programs that the SV-COMP 2026 validators ran
on, it confirms 6,820, more than any of them alone (the best, Witch, 5,392), and unlike CPAchecker and MetaVal it
confirms no witness of a correct program. On 1000 of the witnesses (100 per producer), it confirms 587 of the 851
witnesses of buggy programs, NITWIT 3.x 312; on the witnesses both decide, it takes 20 s of CPU time, 3.x 348 s.
Details, also on memory: `docs/evaluation.md`.

The test suites (`docs/development.md`, "Tests") with this source:

| Suite | Result |
|---|---|
| unit tests | 1,936 tests of the 10 modules, all passing |
| golden dumps | 48: 22 ASTs; 26 typed ASTs, 4 of them for ILP32; all passing |
| semantics | 50 programs with the output, outcome and inputs of GCC and Clang, with `-m32` and `-m64`, also with the terms checked: 200 tests, all passing |
| validation of all SV-COMP 2026 unreach-call witnesses (format 2.x) | the front end accepts every program; all witnesses but one (`line: 0`) parse; 9,239 of 14,841 witnesses of buggy programs confirmed, none of 12,792 of correct programs |
| `validation-crafted` | 96 of 100 right, no invalid witness confirmed |
| `testfiles/` | all 45 format 2.x witnesses right |
| release | `nitwit.sh` and the smoke test of the archive pass; so does fm-weck's smoke test in fm-tools' CI (Ubuntu 26.04, container mode, the SV-COMP 2027 image) |

## Documentation

- [`docs/architecture.md`](docs/architecture.md): goals, modules and key design decisions
- [`docs/guidelines.md`](docs/guidelines.md): coding guidelines and review checklist
- [`docs/development.md`](docs/development.md): toolchain, building, tests, contributing
- [`docs/exit-codes.md`](docs/exit-codes.md): exit codes (compatible with NITWIT 3.x)
- [`docs/release.md`](docs/release.md): the release archive for BenchExec and fm-tools, and how to release
- [`docs/evaluation.md`](docs/evaluation.md): NITWIT 4.0 on all SV-COMP 2026 unreach-call witnesses, against 3.x and
  the other SV-COMP validators
- [`docs/roadmap.md`](docs/roadmap.md): open work
- the module READMEs in `src/<module>/README.md`: semantics, rules and known gaps of each module
- [`tools/difftest/README.md`](tools/difftest/README.md): differential testing against GCC and Clang
- [`release/README.md`](release/README.md) and [`release/THIRD-PARTY.md`](release/THIRD-PARTY.md): the README and
  the third-party notices of the release archive
- [`LICENSE`](LICENSE): NITWIT 4 is licensed under the New BSD license (BSD-3-Clause)
