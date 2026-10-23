# NITWIT 4: development

## Building

NITWIT 4 builds on Linux (WSL Ubuntu 24.04 on the development machine) with GCC 14 or Clang 20, CMake ≥ 3.28 and Ninja.
The Ubuntu default compilers (GCC 13, Clang 18) are not used, see "Toolchain" in `docs/plan.md`.

```bash
cd next
cmake --preset gcc-debug          # or: gcc-release, clang-debug, clang-release, asan (Clang with ASan and UBSan),
                                  # release (nitwit of the release archive only, docs/release.md)
cmake --build --preset gcc-debug
ctest --preset gcc-debug          # unit and golden tests
```

The presets put build directories in `~/nitwit-build/<path of the checkout>/<preset>`, for example
`~/nitwit-build/mnt/e/Dev/nitwit-validator/gcc-debug`. Builds in the WSL home directory are much faster than on
`/mnt/e`, and every worktree gets its own build directories.

From Windows, PowerShell with a single-quoted command:

```powershell
wsl -d Ubuntu -- bash -lc 'cd /mnt/e/Dev/nitwit-validator/next && cmake --preset gcc-debug && cmake --build --preset gcc-debug && ctest --preset gcc-debug'
```

From Git Bash, set `MSYS_NO_PATHCONV=1` so that `/mnt/...` paths are not rewritten. `wsl.exe` runs the command
through another shell, which expands `$variables` first: put anything with variables into a script file and run
`wsl -d Ubuntu -- bash /mnt/.../script.sh`.

When several agents share the machine:
- build with at most `-j 8` (`CMAKE_BUILD_PARALLEL_LEVEL=8`) and run at most 8 corpus threads;
- cap the memory of heavy processes: `ulimit -v 10000000` for non-sanitizer programs, and for sanitizer builds
  `ASAN_OPTIONS=hard_rss_limit_mb=6000`, since ASan cannot start under an address-space limit (libFuzzer: `-rss_limit_mb`);
- do not run corpus jobs at the same time as sanitizer builds.

WSL's disk image lives on C:, and every worktree gets its own build directories (about 1 GB per preset). Delete the
build directories of merged worktrees (`~/nitwit-build/mnt/e/.../worktrees/<agent>`) after merging: a full C: has
stopped WSL with I/O errors once. Inside WSL, freed space is reused without the image growing.

Running out of memory has crashed WSL several times (`Wsl/Service/E_UNEXPECTED`), killing every job in it.
`wsl --shutdown` recovers it. Jobs started detached (`setsid nohup ...`) survive the end of the `wsl.exe` session that
started them.

`tools/check.sh` runs what every hand-over requires: it configures, builds and tests the `gcc-debug`, `clang-debug` and
`asan` presets, then checks the formatting with `clang-format-20`.

## Tests

| Command | Runs |
|---|---|
| `ctest --preset <p>` | unit and golden tests |
| `ctest --preset <p> -L semantics` | interpreter vs. GCC/Clang on `tests/semantics/` |
| `ctest --preset <p> -L corpus` | corpus checks; needs `-DNITWIT_DATASET_DIR=...` at configure time |
| `ctest --preset <p> -L release` | `nitwit.sh` and the smoke test of the release archive with this build's `nitwit` |

## Release archive

`tools/package.sh` builds the executable (preset `release`, with libstdc++ and libgcc linked statically) and the
archive for BenchExec and fm-tools, see `docs/release.md`.

## Corpus

`NITWIT_DATASET_DIR` points to the SV-COMP 2026 witness dataset (`/mnt/e/workspace/playground/svcomp-2026-witnesses`,
see its `README.md`). The corpus files are untrusted input: read them, do not execute anything from them.
