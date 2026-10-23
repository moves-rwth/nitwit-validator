#pragma once

// Access to the witness files in tests/unit/witness/data.

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace nitwit::witness {

inline std::string readTestFile(std::string_view name) {
    std::ifstream stream(std::filesystem::path(NITWIT_TEST_DATA_DIR) / name, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

} // namespace nitwit::witness
