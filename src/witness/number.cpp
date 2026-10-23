#include "witness/number.hpp"

#include <charconv>

namespace nitwit::witness {

std::optional<std::uint32_t> parseUnsigned(std::string_view text) {
    std::uint32_t value = 0;
    const char* end = text.data() + text.size();
    // from_chars accepts neither a '+' nor, for unsigned types, a '-'.
    const auto [ptr, error] = std::from_chars(text.data(), end, value);
    if (text.empty() || error != std::errc{} || ptr != end) {
        return std::nullopt;
    }
    return value;
}

} // namespace nitwit::witness
