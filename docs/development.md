# NITWIT 4: development

## Toolchain

GCC 14 (`gcc-14`, `g++-14`) and Clang 20 (`clang-20`, `clang-format-20`), both with libstdc++ 14, on Ubuntu 24.04;
CMake ≥ 3.28 and Ninja. libstdc++ 14 determines the usable subset of C++23 (`docs/guidelines.md`). The Ubuntu defaults
GCC 13 and Clang 18 are not used: Clang 18 cannot use libstdc++'s `std::expected`, which requires
`__cpp_concepts >= 202002L`, and Clang sets that from version 19 on. Some tests compile C programs with
`gcc-14 -m32`/`-m64` where they can (`gcc-14-multilib`); the difftest tools also need `clang-20`, `csmith` and
`libcsmith-dev` (`tools/difftest/README.md`).

## Building

NITWIT builds on Linux (WSL Ubuntu 24.04 on the development machine), from the root of the repository:

```bash
cmake --preset gcc-debug          # or: gcc-release, clang-debug, clang-release, asan (Clang with ASan and UBSan),
                                  # release (nitwit of the release archive only, docs/release.md)
cmake --build --preset gcc-debug
ctest --preset gcc-debug          # all tests of the build, see "Tests"
```

The presets put build directories in `~/nitwit-build/<path of the checkout>/<preset>`, for example
`~/nitwit-build/mnt/e/Dev/nitwit-validator/gcc-debug`. Builds in the WSL home directory are much faster than on
`/mnt/e`, and every worktree gets its own build directories.

From Windows, PowerShell with a single-quoted command:

```powershell
wsl -d Ubuntu -- bash -lc 'cd /mnt/e/Dev/nitwit-validator && cmake --preset gcc-debug && cmake --build --preset gcc-debug && ctest --preset gcc-debug'
```

From Git Bash, set `MSYS_NO_PATHCONV=1` so that `/mnt/...` paths are not rewritten. `wsl.exe` runs the command
through another shell, which expands `$variables` first: put anything with variables into a script file and run
`wsl -d Ubuntu -- bash /mnt/.../script.sh`.

When several jobs share the machine:
- build with at most `-j 8` (`CMAKE_BUILD_PARALLEL_LEVEL=8`) and run at most 8 corpus threads;
- cap the memory of heavy processes: `ulimit -v 10000000` for non-sanitizer programs, and for sanitizer builds
  `ASAN_OPTIONS=hard_rss_limit_mb=6000`, since ASan cannot start under an address-space limit (libFuzzer:
  `-rss_limit_mb`);
- do not run corpus jobs at the same time as sanitizer builds.

WSL's disk image lives on C:, and every worktree gets its own build directories (about 1 GB per preset). Delete the
build directories of merged worktrees (`~/nitwit-build/mnt/e/.../worktrees/<name>`): a full C: has stopped WSL with
I/O errors once. Inside WSL, freed space is reused without the image growing.

Running out of memory has crashed WSL several times (`Wsl/Service/E_UNEXPECTED`), killing every job in it.
`wsl --shutdown` recovers it. Jobs started detached (`setsid nohup ...`) survive the end of the `wsl.exe` session that
started them.

## Tests

`ctest --preset <p>` runs every test of the build; `-L <label>` selects one tier.

| Tier | What | CTest label |
|---|---|---|
| unit | GoogleTest per module in `tests/unit/<module>/`, fast and deterministic; GCC's checks of the sema zoo | `unit` |
| golden | input file → expected dump in `tests/golden/` (AST, typed AST); `NITWIT_UPDATE_GOLDEN=1` rewrites the dumps | `golden` |
| semantics | the programs of `tests/semantics/` with `nitwit --replay`, `-32` and `-64`, against the output, outcome and input log of GCC and Clang, also with the terms checked (`--check-terms`) | `semantics` |
| release | `tests/release/check_release.sh` with this build's `nitwit`: `nitwit.sh`, the smoke test, the data model by the executable's name | `release` |
| corpus | lexing, parsing and analysis of the corpus programs, parsing of the corpus witnesses, validation of `testfiles/`, `validation-crafted` and the 250-witness sample (`samples/unreach-call-25-per-producer.tsv` of the dataset) against 3.x; needs `-DNITWIT_DATASET_DIR=...` at configure time | `corpus` |
| difftest | Csmith programs and corpus programs with random inputs, GCC vs. Clang and native vs. interpreter (`tools/difftest/`, `difftest.py run --nitwit`) | – |

The module READMEs list what each test covers; `README.md` has the last results. The parser (with the lexer) and the
witness parsers were fuzzed with libFuzzer without findings; the fuzz targets are not part of the repository.

`tools/check.sh` runs what every hand-over requires: it configures, builds and tests the `gcc-debug`, `clang-debug` and
`asan` presets, then checks the formatting with `clang-format-20`.

### Corpus

`NITWIT_DATASET_DIR` points to the SV-COMP 2026 witness dataset `svcomp-2026-witnesses`
(`/mnt/e/workspace/playground/svcomp-2026-witnesses` in WSL, see its `README.md`). Its witnesses and programs are
untrusted input: they are read, never executed, except as compiled programs in the difftest harness.

### Differential testing against GCC and Clang

`tools/difftest/README.md` describes the tools. In short:

- **Harness:** `tools/difftest/harness.c` implements `__VERIFIER_nondet_*` from an input file and logs each value it
  hands out. It also implements `reach_error` and `__VERIFIER_assume`, each of which prints a marker and exits with a
  fixed code; `abort` is the C library's (`SIGABRT`, no marker).
- **Native side:** the harness is linked to the program, compiled with `gcc`/`clang`, `-m32`/`-m64`, `-O0`, and once
  more with `-fsanitize=undefined,address`, so that runs with undefined behaviour can be discarded.
- **Interpreter side:** `nitwit --replay <inputs>` uses the same input file.
- **Comparison:** stdout, exit code, the error marker and the sequence of inputs used. Csmith programs print a checksum
  of all globals, which makes them a strong check on integer promotions, layouts and pointer arithmetic.

## Release archive

`tools/package.sh` builds the executable (preset `release`, with libstdc++ and libgcc linked statically) and the
archive for BenchExec and fm-tools, see `docs/release.md`.

## Contributing

- The rules for every change are in `docs/guidelines.md`, design decisions and module boundaries in
  `docs/architecture.md`. A module's public headers and its `README.md` describe its API and behaviour; a change to
  one updates the other.
- Before handing a change over, run `tools/check.sh` and go through the review checklist of `docs/guidelines.md`. A
  reviewer who knows only the module's API, the guidelines and the diff uses the same checklist.
- Changes of the validator are checked on the corpus (`ctest --preset <p> -L corpus`): no invalid witness and no
  witness of a correct program may be confirmed.
- Results that change go into `README.md` ("Results") and `docs/evaluation.md`; open work goes into
  `docs/roadmap.md`.
