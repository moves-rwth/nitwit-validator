# NITWIT 4: development

## Toolchain

GCC 14 (`gcc-14`, `g++-14`) and Clang 20 (`clang-20`, `clang++-20`, `clang-format-20`), both with libstdc++ 14, on
Ubuntu 24.04; CMake ≥ 3.28, Ninja and Python 3 (the semantics tests need it). libstdc++ 14 determines the usable
subset of C++23 (`docs/guidelines.md`). The Ubuntu defaults GCC 13 and Clang 18 are not used: Clang 18 cannot use
libstdc++'s `std::expected`, which requires `__cpp_concepts >= 202002L`, and Clang sets that from version 19 on. Some
tests compile C programs with `gcc-14 -m32`/`-m64` where they can (`gcc-14-multilib`); the difftest tools also need
`csmith` and `libcsmith-dev` (`tools/difftest/README.md`).

## Building

NITWIT builds on Linux, from the root of the repository:

```bash
cmake --preset gcc-debug          # or: gcc-release, clang-debug, clang-release, asan (Clang with ASan and UBSan),
                                  # release (nitwit of the release archive only, docs/release.md)
cmake --build --preset gcc-debug
ctest --preset gcc-debug          # all tests of the build, see "Tests"
```

The presets put build directories in `~/nitwit-build/<path of the checkout>/<preset>`, for example
`~/nitwit-build/mnt/e/Dev/nitwit-validator/gcc-debug`, so that every checkout and worktree gets its own.

### WSL

On Windows, NITWIT builds in WSL (Ubuntu 24.04). Builds in the WSL home directory, where the presets put them, are
much faster than on a Windows drive such as `/mnt/e`.

- **PowerShell:** pass the command single-quoted, e.g. to configure, build and test `gcc-debug` and check the
  formatting:

  ```powershell
  wsl -d Ubuntu -- bash -lc 'cd /mnt/e/Dev/nitwit-validator && tools/check.sh gcc-debug'
  ```

- **Git Bash:** set `MSYS_NO_PATHCONV=1` so that `/mnt/...` paths are not rewritten. `wsl.exe` runs the command
  through another shell, which expands `$variables` first: put anything with variables into a script file and run
  `wsl -d Ubuntu -- bash /mnt/.../script.sh`.
- **Shared machine:** build with at most `-j 8` (`CMAKE_BUILD_PARALLEL_LEVEL=8`) and run at most 8 corpus threads.
  Cap the memory of heavy processes: `ulimit -v 10000000` for non-sanitizer programs, and for sanitizer builds
  `ASAN_OPTIONS=hard_rss_limit_mb=6000`, since ASan cannot start under an address-space limit (libFuzzer:
  `-rss_limit_mb`). Do not run corpus jobs at the same time as sanitizer builds.
- **Memory:** running out of memory crashes WSL (`Wsl/Service/E_UNEXPECTED`) and every job in it; `wsl --shutdown`
  recovers it. Jobs started detached (`setsid nohup ...`) survive the end of the `wsl.exe` session that started them.
- **Disk:** WSL's disk image lives on C:, and a full C: stops WSL with I/O errors. Every worktree gets its own build
  directories (about 1 GB per preset): delete those of merged worktrees (`~/nitwit-build/mnt/e/.../worktrees/<name>`).
  Inside WSL, freed space is reused without the image growing.

## Tests

`ctest --preset <p>` runs every test of the build; `-L <label>` selects one tier.

| Tier | What | When | CTest label |
|---|---|---|---|
| unit | GoogleTest per module in `tests/unit/<module>/`, fast and deterministic; GCC's checks of the sema zoo | every build | `unit` |
| golden | input file → expected dump in `tests/golden/` (AST, typed AST); `NITWIT_UPDATE_GOLDEN=1` rewrites the dumps | every build | `golden` |
| semantics | the programs of `tests/semantics/` with `nitwit --replay`, `-32` and `-64`, against the output, outcome and input log of GCC and Clang, also with the terms checked (`--check-terms`) | every build | `semantics` |
| release | `tests/release/check_release.sh` with this build's `nitwit`: `nitwit.sh`, the smoke test, the data model by the executable's name | every build | `release` |
| corpus | lexing, parsing and analysis of the corpus programs, parsing of the corpus witnesses, validation of `testfiles/`, `validation-crafted` and the 250-witness sample (`samples/unreach-call-25-per-producer.tsv` of the dataset) against 3.x; needs `-DNITWIT_DATASET_DIR=...` at configure time | after changes to the front end or the validator, and before a release | `corpus` |
| difftest | `corpus.py` and `csmith.py` run corpus programs with random inputs and Csmith programs under GCC, Clang and the sanitizer build; `difftest.py run --nitwit` compares the interpreter with them, program by program (`tools/difftest/README.md`) | after changes to `value`, `sema` or `interp` | – |

The module READMEs list what each test covers; `README.md` has the last results. The parser (with the lexer) and the
witness parsers were fuzzed with libFuzzer without findings; the fuzz targets are not part of the repository.

`tools/check.sh [preset...]` configures, builds and tests the given presets (default: `gcc-debug`, `clang-debug` and
`asan`) with `ctest -LE 'corpus|difftest'`, then checks the formatting of `src`, `tests` and `tools` with
`clang-format-20`.

### Corpus

`NITWIT_DATASET_DIR` points to the SV-COMP 2026 witness dataset, which the NITWIT developers maintain outside this
repository (for example at `/mnt/e/workspace/playground/svcomp-2026-witnesses`). Its own `README.md`, at the root of
the dataset, describes its contents and scripts. Its witnesses and programs are untrusted input: they are read, never
executed, except as compiled programs in the difftest harness.

## Release archive

`tools/package.sh` builds the executable (preset `release`, with libstdc++ and libgcc linked statically) and the
archive for BenchExec and fm-tools; `docs/release.md` describes the archive and how to release.

## Contributing

- The rules for every change are in `docs/guidelines.md`, design decisions and module boundaries in
  `docs/architecture.md`. A module's public headers and its `README.md` describe its API and behaviour; a change to
  one updates the other.
- Before a change is merged, `tools/check.sh` passes, and the change is checked against the review checklist of
  `docs/guidelines.md`.
- Changes of the validator are checked on the corpus (`ctest --preset <p> -L corpus`): no invalid witness and no
  witness of a correct program may be confirmed.
- Results that change go into `README.md` ("Results") and `docs/evaluation.md`; open work goes into
  `docs/roadmap.md`.
