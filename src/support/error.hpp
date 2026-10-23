#pragma once

// The error type of std::expected results: a failure caused by the input, not by a bug in NITWIT.

#include <string>

namespace nitwit {

struct Error {
    std::string message;
};

} // namespace nitwit
