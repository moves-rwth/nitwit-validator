#pragma once

// Assertions for invariants of NITWIT itself. They stay active in release builds: aborting is better than a verdict
// computed from a broken state.

#include <source_location>
#include <string_view>

namespace nitwit {

[[noreturn]] void assertionFailed(std::string_view condition, std::string_view message, std::source_location where);

} // namespace nitwit

#define NITWIT_ASSERT(condition, message)                                                                              \
    ((condition) ? static_cast<void>(0)                                                                                \
                 : ::nitwit::assertionFailed(#condition, message, std::source_location::current()))

#define NITWIT_UNREACHABLE(message) ::nitwit::assertionFailed("unreachable", message, std::source_location::current())
