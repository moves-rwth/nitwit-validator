#pragma once

// SHA-256 (FIPS 180-4), for the hashes of the program files that a written witness names (witness format 2.x,
// "input_file_hashes").

#include <string>
#include <string_view>

namespace nitwit {

/// The SHA-256 digest of `data` as 64 lowercase hexadecimal digits.
[[nodiscard]] std::string sha256Hex(std::string_view data);

} // namespace nitwit
