#include "support/large_stack.hpp"

#include <cstddef>

#include <gtest/gtest.h>

namespace nitwit {
namespace {

// Recursion that needs far more than the usual 8 MiB of stack. `volatile` keeps the frames from being optimized away.
std::size_t recurse(std::size_t depth) {
    volatile unsigned char frame[1024] = {};
    frame[depth % sizeof(frame)] = 1;
    if (depth == 0) {
        return 0;
    }
    return recurse(depth - 1) + static_cast<std::size_t>(frame[depth % sizeof(frame)]);
}

TEST(LargeStackTest, RunsDeepRecursion) {
    std::size_t result = 0;
    runWithLargeStack(std::size_t{1} << 30, [&] { result = recurse(100000); });
    EXPECT_EQ(result, 100000u);
}

} // namespace
} // namespace nitwit
