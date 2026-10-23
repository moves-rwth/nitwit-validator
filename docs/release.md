# NITWIT 4: release archive

The release archive is what BenchExec and fm-tools (SV-COMP) run: `nitwit-<version>.zip` with the single top-level
directory `nitwit/`.

## Building it

```bash
tools/package.sh [output-dir]       # from the root of the repository; default output directory: the build directory
```

`tools/package.sh`:

1. configures and builds the preset `release` in `~/nitwit-build/<path of the checkout>/release`: GCC 14,
   Release, `NITWIT_STATIC_RUNTIME=ON` (`nitwit` is linked with `-static-libstdc++ -static-libgcc`), no tests;
2. runs `tests/release/check_release.sh --glibc-only` on the executable;
3. assembles `nitwit/` in a temporary directory, writes `licenses/VERSIONS` and runs the smoke test;
4. writes `nitwit-<version>.zip`. The version is the project version in `CMakeLists.txt`, which `nitwit --version`
   prints.

It needs what the other presets need, plus the static C++ runtime (`libstdc++-14-dev`, `libgcc-14-dev`; both come with
GCC 14 on Ubuntu 24.04) and `objdump`. git is optional: without it, the source revision in `licenses/VERSIONS` is
`unknown`. `cmake -E tar` writes the zip file and keeps the Unix permissions.

**glibc is linked dynamically,** libstdc++ and libgcc statically. The executable needs only `libc.so.6`, `libm.so.6`
and the dynamic loader. Built on Ubuntu 24.04 (glibc 2.39), it runs with **glibc 2.38** or later: the newest symbol
versions it uses are those of `fmod` and `fmodf` (new in 2.38) and `__isoc23_strtoul` (glibc 2.38 redirects `strtoul`
to its C23 version). Ubuntu 22.04 (glibc 2.35) is too old. `check_release.sh --glibc-only` checks both: the shared
libraries, and that no symbol needs a glibc newer than 2.38 (`maxGlibc` there, the only place that states it in
code). `licenses/VERSIONS` records the minimum of each build. The interpreter evaluates the math functions
of programs with the host's `libm` (`src/interp/README.md`), so their results follow the glibc of the machine NITWIT
runs on.

With `NITWIT_STATIC_RUNTIME`, everything is compiled with `-ffile-prefix-map`, so that the executable contains no
paths of the build machine. Other builds keep absolute paths, which debuggers need to find the sources.
`NITWIT_STATIC_RUNTIME` together with `NITWIT_SANITIZE` is a configuration error.

## Layout

```
nitwit/
├── nitwit.sh           wrapper for BenchExec (release/nitwit.sh)
├── bin/nitwit32        the nitwit executable
├── bin/nitwit64        the same file again
├── smoketest.sh        smoke test for fm-weck (release/smoketest.sh)
├── smoketest/          smoketest.c, smoketest.yml, unreach-call.prp
├── README.md           for users of the archive (release/README.md)
├── THIRD-PARTY.md      third-party code in the executables (release/THIRD-PARTY.md)
├── LICENSE             NITWIT's license (LICENSE)
└── licenses/
    ├── VERSIONS            source revision, compiler, C++ runtime packages, minimum glibc
    ├── yaml-cpp-LICENSE, pugixml-LICENSE.md
    └── libstdc++-notices   HP/SGI STL, fast_float, Ryu (release/licenses/)
```

`bin/nitwit32` and `bin/nitwit64` are plain copies, not symlinks, because zip archives and BenchExec handle plain files
best. Without `-32` or `-64`, the executable's name selects the data model: `bin/nitwit64` called directly uses LP64
(`src/driver/README.md`).

## BenchExec

The tool-info module `benchexec/tools/nitwit.py`:

- requires `bin/nitwit32` and `bin/nitwit64` next to `nitwit.sh`;
- takes the version from `nitwit.sh --version`, which prints only the version, e.g. `4.0.0`;
- runs `nitwit.sh <options> -p <property> -w <witness> <program>`; the maintainer's fork adds `-32` or `-64` from the
  task's data model before `-w`. `nitwit.sh` passes all arguments unchanged to `bin/nitwit32`, which reads `-32` and
  `-64` itself, from any working directory. `bin/nitwit64` is only there for `REQUIRED_PATHS` and direct calls;
- maps the exit codes of `docs/exit-codes.md`, which are those of 3.x: 0 and 245 are FALSE, 251 OUT OF MEMORY, 4, 5,
  241, 242, 243 and 250 UNKNOWN, all others ERROR. 252 is UNKNOWN in the fork, but ERROR in BenchExec 3.35, so NITWIT
  4 does not use it (`docs/exit-codes.md`).

Differences from 3.x's `nitwit.sh`:

| | 3.x | NITWIT 4 |
|---|---|---|
| `-v` | prints the version | prints the validation log; the version is `--version` |
| unsupported property | 255 (ERROR) | 4 (UNKNOWN) |
| program or witness file missing | 255 (ERROR) | 3 (ERROR) |
| no `-w` | 255 (ERROR) | 240 (ERROR) |
| `-32`/`-64` twice, or both | the last one counts | wrong command line: 3 (ERROR) |
| the error function is called, but the witness was not followed to its target (format 2.x) | 252 (UNKNOWN in the fork, ERROR in BenchExec 3.35) | 241 (UNKNOWN), with its own result line |
| missing executable | 255 (ERROR) | 255 (ERROR) |
| `-h`, `--help` | usage, 0 | unknown option: usage, 3 |
| working directory | must be the archive's directory | any |

## fm-tools

- The archive has one top-level directory and runs on Ubuntu 24.04 and 26.04, the environment of SV-COMP 2027 (and any
  x86-64 Linux with glibc ≥ 2.38). fm-weck's smoke test passes in fm-tools' CI job (`ubuntu:26.04`), in fm-weck's
  container mode and in the competition image `competition-scripts/user:2027`.
- fm-weck's smoke test is `smoketest.sh`. It calls `nitwit.sh --version`, then validates `smoketest/smoketest.yml` with
  `-32` and with `-64`, with the arguments in the fork's order. It fails unless both runs print `VALIDATED:` and exit
  with 0.
- Required Ubuntu packages: none for the executables, which need only glibc. Programs with preprocessor directives
  other than line markers and `#pragma` (38 of the 19,752 corpus and test files) need `cpp`; with `#include` of system
  headers in both data models, `gcc-multilib`, which brings `cpp`, `libc6-dev`, `libc6-dev-i386` and GCC's own
  headers.

`tests/release/check_release.sh` checks `nitwit.sh` (also through a symlink and in a path with a blank), the version it
prints, the smoke test, and the data model chosen by the executable's name; with `--glibc-only` also the shared
libraries. The CTest test `release_archive` (label `release`) runs it with the `nitwit` of every build with tests
(with `--glibc-only` if `NITWIT_STATIC_RUNTIME` is on), and `tools/package.sh` with the executable it ships.

## Licensing

NITWIT 4 is under the New BSD license (BSD-3-Clause, `LICENSE`), and the archive ships that file. The executables
contain yaml-cpp and pugixml (MIT) and libstdc++/libgcc (GPL-3.0 with the GCC Runtime Library Exception, with
third-party parts under the HP and SGI STL notices, MIT and BSL-1.0).
`release/THIRD-PARTY.md` lists them, and `tools/package.sh` copies their notices into `licenses/`. glibc is the
system's shared library, not part of the archive. Keep `release/THIRD-PARTY.md` in step with
`cmake/Dependencies.cmake`, and `release/licenses/libstdc++-notices` with the compiler.
