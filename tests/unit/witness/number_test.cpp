#include "witness/number.hpp"

#include <gtest/gtest.h>

namespace nitwit::witness {
namespace {

TEST(NumberTest, DecimalNumbersParse) {
    EXPECT_EQ(parseUnsigned("0"), 0u);
    EXPECT_EQ(parseUnsigned("42"), 42u);
    EXPECT_EQ(parseUnsigned("007"), 7u);
    EXPECT_EQ(parseUnsigned("4294967295"), 4294967295u);
}

TEST(NumberTest, OverflowIsRejected) {
    EXPECT_EQ(parseUnsigned("4294967296"), std::nullopt);
    EXPECT_EQ(parseUnsigned("99999999999999999999"), std::nullopt);
}

TEST(NumberTest, SignsSpacesAndOtherNotationsAreRejected) {
    for (const char* text : {"", "-1", "+1", " 1", "1 ", "1.0", "1e3", "0x10", "12abc", "true"}) {
        EXPECT_EQ(parseUnsigned(text), std::nullopt) << text;
    }
}

} // namespace
} // namespace nitwit::witness
