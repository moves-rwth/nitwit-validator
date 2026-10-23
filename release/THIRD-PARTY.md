# Third-party code in the executables

`bin/nitwit32` and `bin/nitwit64` link the C++ runtime statically and use the system's C library (glibc) as a shared
library. Besides NITWIT itself (New BSD license, `LICENSE`), they contain the following. `licenses/VERSIONS` names the
source revision of NITWIT, the compiler, the packages of the C++ runtime and the oldest glibc the executables run with.

| Component | Version | License | Notices |
|---|---|---|---|
| yaml-cpp | 0.8.0 | MIT | `licenses/yaml-cpp-LICENSE` |
| pugixml | 1.15 | MIT | `licenses/pugixml-LICENSE.md` |
| GNU C++ Library (libstdc++) and libgcc | `licenses/VERSIONS` | GPL-3.0-or-later with the GCC Runtime Library Exception 3.1; parts of libstdc++ under HP and SGI permission notices, MIT (fast_float) and BSL-1.0 (Ryu) | `licenses/libstdc++-notices` |

The GCC Runtime Library Exception allows distributing programs compiled with GCC, including the parts of libstdc++
and libgcc linked into them, under licenses of one's choice. The third-party parts of libstdc++ keep their notices,
see `licenses/libstdc++-notices`.
