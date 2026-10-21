# NITWIT 4: development

## Building

NITWIT 4 builds on Linux (WSL Ubuntu 24.04 on the development machine) with GCC 14 or Clang 20, CMake ≥ 3.28 and Ninja.
The Ubuntu default compilers (GCC 13, Clang 18) are not used, see "Toolchain" in `docs/plan.md`.

```bash
cd next
cmake --preset gcc-debug          # or: gcc-release, clang-debug, clang-release, asan (Clang with ASan and UBSan)
cmake --build --preset gcc-debug
ctest --preset gcc-debug          # unit and golden tests
```

The presets put build directories in `~/nitwit-next-build/<path of the checkout>/<preset>`, for example
`~/nitwit-next-build/mnt/e/Dev/nitwit-validator/gcc-debug`. Builds in the WSL home directory are much faster than on
`/mnt/e`, and every worktree gets its own build directories.

From Windows, PowerShell with a single-quoted command:

```powershell
wsl -d Ubuntu -- bash -lc 'cd /mnt/e/Dev/nitwit-validator/next && cmake --preset gcc-debug && cmake --build --preset gcc-debug && ctest --preset gcc-debug'
```

From Git Bash, set `MSYS_NO_PATHCONV=1` so that `/mnt/...` paths are not rewritten.

`tools/check.sh` runs what every hand-over requires: it configures, builds and tests the `gcc-debug`, `clang-debug` and
`asan` presets, then checks the formatting with `clang-format-20`.

## Tests

| Command | Runs |
|---|---|
| `ctest --preset <p>` | unit and golden tests |
| `ctest --preset <p> -L semantics` | interpreter vs. GCC/Clang on `tests/semantics/` |
| `ctest --preset <p> -L corpus` | corpus checks; needs `-DNITWIT_DATASET_DIR=...` at configure time |

## Corpus

`NITWIT_DATASET_DIR` points to the SV-COMP 2026 witness dataset (`/mnt/e/workspace/playground/svcomp-2026-witnesses`,
see its `README.md`). The corpus files are untrusted input: read them, do not execute anything from them.
