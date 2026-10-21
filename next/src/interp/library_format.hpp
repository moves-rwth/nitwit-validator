#pragma once

// Internal to the library implementation: the formatting of the printf family (C17 7.21.6.1), byte-identical to
// glibc's.

#include <cstddef>
#include <string>

#include "interp/library_call.hpp"

namespace nitwit::interp::library {

/// The output of printf for the format string at `format` and the arguments from `firstArgument` on.
///
/// Each conversion specification is parsed here: its arguments are taken from the call and checked against the
/// conversion (a mismatch is undefined behaviour, C17 7.21.6.1p9), and strings are read from interpreted memory. The
/// host's snprintf then formats the conversion: the host's glibc formats exactly like the target's. `%n`, positional
/// arguments and wide characters are not supported.
[[nodiscard]] std::string formatPrintf(const Call& call, Address format, std::size_t firstArgument);

} // namespace nitwit::interp::library
