#pragma once

// Internal to the library implementation: the formatting of the printf family (C17 7.21.6.1), byte-identical to
// glibc's.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "interp/library_call.hpp"

namespace nitwit::interp::library {

struct MemoryRange {
    Address address = 0;
    std::uint64_t size = 0;
};

/// The output of printf, and the ranges of interpreted memory it read (the format string and the strings of `%s`),
/// which sprintf and snprintf must not write to (C17 7.21.6.5p2, 7.21.6.6p2).
struct Formatted {
    std::string text;
    std::vector<MemoryRange> reads;
};

/// What printf prints for the format string at `format` and the arguments from `firstArgument` on.
///
/// Each conversion specification is parsed here. Its argument must have the type the conversion expects after the
/// default argument promotions (LibraryCall::argumentTypes); an integer of another width, a pointer for a number or a
/// number for `%s` is undefined behaviour (C17 7.21.6.1p9), a signed/unsigned difference of the same width is not.
/// Strings are read from interpreted memory, and the host's snprintf formats each conversion: the host's glibc
/// formats exactly like the target's. `%n`, positional arguments and wide characters are not supported.
[[nodiscard]] Formatted formatPrintf(const Call& call, Address format, std::size_t firstArgument);

} // namespace nitwit::interp::library
