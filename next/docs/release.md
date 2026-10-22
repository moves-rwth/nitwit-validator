# NITWIT 4: release archive

The release archive is what BenchExec and fm-tools (SV-COMP) run: `nitwit-<version>.zip` with the single top-level
directory `nitwit/`.

## Building it

```bash
cd next
tools/package.sh [output-dir]       # default output directory: the build directory
```

`tools/package.sh`:

1. configures and builds the preset `release-static` in `~/nitwit-next-build/<path of the checkout>/release-static`:
   GCC 14, Release, `NITWIT_STATIC=ON` (`nitwit` is linked with `-static`), no tests;
2. runs `tests/release/check_release.sh` on the static executable;
3. assembles `nitwit/` in a temporary directory, writes `licenses/VERSIONS` and runs the smoke test;
4. writes `nitwit-<version>.zip`. The version is the project version in `CMakeLists.txt`, which `nitwit --version`
   prints.

It needs what the other presets need, plus the static C and C++ libraries (`libc6-dev`, `libstdc++-14-dev`; both
come with GCC 14 on Ubuntu 24.04). git is optional: without it, the source revision in `licenses/VERSIONS` is
`unknown`. `cmake -E tar` writes the zip file and keeps the Unix permissions.

With `NITWIT_STATIC`, everything is compiled with `-ffile-prefix-map`, so that the executable contains no paths of the
build machine. Other builds keep absolute paths, which debuggers need to find the sources. `NITWIT_STATIC` together
with `NITWIT_SANITIZE` is a configuration error.

Linking statically gives no warnings: NITWIT calls none of the glibc functions that need shared libraries at run time
(`dlopen`, the NSS functions such as `getpwnam`), and it never changes the locale. `posix_spawnp`, which runs `cpp`,
works in the static executable.

## Layout

```
nitwit/
├── nitwit.sh           wrapper for BenchExec (release/nitwit.sh)
├── bin/nitwit32        the static nitwit executable
├── bin/nitwit64        the same file again
├── smoketest.sh        smoke test for fm-weck (release/smoketest.sh)
├── smoketest/          smoketest.c, smoketest.yml, unreach-call.prp
├── README.md           for users of the archive (release/README.md)
├── THIRD-PARTY.md      third-party code in the executables (release/THIRD-PARTY.md)
├── LICENSE             the repository's license
└── licenses/
    ├── VERSIONS                    source revision, compiler and C/C++ library packages of the build
    ├── yaml-cpp-LICENSE, pugixml-LICENSE.md
    ├── glibc-copyright, LGPL-2.1   Ubuntu's copyright file of glibc and the LGPL
    └── libstdc++-notices           HP/SGI STL, fast_float, Ryu (release/licenses/)
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
  4 does not use it for now.

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

- The archive has one top-level directory and runs on Ubuntu 24.04, as on any x86-64 Linux.
- fm-weck's smoke test is `smoketest.sh`. It calls `nitwit.sh --version`, then validates `smoketest/smoketest.yml` with
  `-32` and with `-64`, with the arguments in the fork's order. It fails unless both runs print `VALIDATED:` and exit
  with 0.
- Required Ubuntu packages: none for the executables. Programs with preprocessor directives other than line markers
  and `#pragma` (38 of the 19,752 corpus and test files) need `cpp`; with `#include` of system headers in both data
  models, `gcc-multilib`, which brings `cpp`, `libc6-dev`, `libc6-dev-i386` and GCC's own headers.

`tests/release/check_release.sh` checks `nitwit.sh` (also through a symlink and in a path with a blank), the version it
prints, the smoke test, and the data model chosen by the executable's name. The CTest test `release_archive` (label
`release`) runs it with the `nitwit` of every build with tests, and `tools/package.sh` with the static executable it
ships.

## Licensing

NITWIT is under the New BSD license (`LICENSE` of the repository). The static executables contain yaml-cpp and
pugixml (MIT), glibc (LGPL-2.1-or-later) and libstdc++/libgcc (GPL-3.0 with the GCC Runtime Library Exception, with
third-party parts under the HP and SGI STL notices, MIT and BSL-1.0). `release/THIRD-PARTY.md` lists them, and
`tools/package.sh` copies their notices into `licenses/`. The LGPL requires that users can relink NITWIT with a
modified glibc; the complete source of NITWIT is public, so they can rebuild it. Keep `release/THIRD-PARTY.md` in step
with `cmake/Dependencies.cmake`, and `release/licenses/libstdc++-notices` with the compiler.
