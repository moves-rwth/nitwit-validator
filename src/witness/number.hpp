#pragma once

// Numbers in witness files, shared by the YAML and GraphML parsers. Not part of the module's API.

#include <cstdint>
#include <optional>
#include <string_view>

namespace nitwit::witness {

/// The value of `text` if it is a decimal number without sign or spaces that fits into 32 bits.
[[nodiscard]] std::optional<std::uint32_t> parseUnsigned(std::string_view text);

} // namespace nitwit::witness
