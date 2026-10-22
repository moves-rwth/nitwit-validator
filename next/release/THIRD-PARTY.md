# Third-party code in the executables

`bin/nitwit32` and `bin/nitwit64` are statically linked. Besides NITWIT itself (New BSD license, `LICENSE`), they
contain the following. `licenses/VERSIONS` names the source revision of NITWIT, the compiler and the exact packages
of the C and C++ libraries the executables were built with.

| Component | Version | License | Notices |
|---|---|---|---|
| yaml-cpp | 0.8.0 | MIT | `licenses/yaml-cpp-LICENSE` |
| pugixml | 1.15 | MIT | `licenses/pugixml-LICENSE.md` |
| GNU C Library (glibc) | `licenses/VERSIONS` | LGPL-2.1-or-later; some parts under other free licenses | `licenses/glibc-copyright`, `licenses/LGPL-2.1` |
| GNU C++ Library (libstdc++) and libgcc | `licenses/VERSIONS` | GPL-3.0-or-later with the GCC Runtime Library Exception 3.1; parts of libstdc++ under HP and SGI permission notices, MIT (fast_float) and BSL-1.0 (Ryu) | `licenses/libstdc++-notices` |

## glibc

The LGPL lets you modify glibc and use the modified library with NITWIT. Since NITWIT is linked statically, that
means linking NITWIT again: the complete source code of NITWIT is public under the New BSD license at
<https://github.com/moves-rwth/nitwit-validator>, and its script `tools/package.sh` builds these executables from it
with the glibc of the build host. The source code of the glibc used here is Ubuntu's package `glibc`
(<https://launchpad.net/ubuntu/+source/glibc>).

## libstdc++ and libgcc

The GCC Runtime Library Exception allows distributing programs compiled with GCC, including the parts of these
libraries linked into them, under licenses of one's choice. The third-party parts of libstdc++ keep their notices,
see `licenses/libstdc++-notices`.
