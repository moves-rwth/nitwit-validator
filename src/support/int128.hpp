#pragma once

// 128-bit integers for C's __int128 and for intermediate results. GCC and Clang provide them as an extension.

namespace nitwit {

__extension__ typedef __int128 Int128;
__extension__ typedef unsigned __int128 UInt128;

} // namespace nitwit
