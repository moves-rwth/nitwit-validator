#include "support/assert.hpp"

#include <cstdio>
#include <cstdlib>

namespace nitwit {

void assertionFailed(std::string_view condition, std::string_view message, std::source_location where) {
    std::fprintf(stderr, "%s:%u: internal error in %s: %.*s (%.*s)\n", where.file_name(),
                 static_cast<unsigned>(where.line()), where.function_name(), static_cast<int>(message.size()),
                 message.data(), static_cast<int>(condition.size()), condition.data());
    std::abort();
}

} // namespace nitwit
