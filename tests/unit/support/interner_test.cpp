#include "support/interner.hpp"

#include <string>
#include <unordered_map>

#include <gtest/gtest.h>

namespace nitwit {
namespace {

TEST(InternerTest, EqualTextsGiveEqualSymbols) {
    StringInterner interner;
    std::string text = "main";
    const Symbol first = interner.intern(text);
    text = "other";
    const Symbol second = interner.intern("main");
    EXPECT_EQ(first, second);
    EXPECT_EQ(first.text(), "main");
    EXPECT_NE(first, interner.intern("mainx"));
}

TEST(InternerTest, EmptyStringIsTheEmptySymbol) {
    StringInterner interner;
    EXPECT_TRUE(interner.intern("").empty());
    EXPECT_EQ(interner.intern(""), Symbol{});
    EXPECT_EQ(Symbol{}.text(), "");
}

TEST(InternerTest, SymbolsStayValidWhileTheInternerGrows) {
    StringInterner interner;
    const Symbol first = interner.intern("x0");
    for (int i = 1; i < 10000; ++i) {
        interner.intern("x" + std::to_string(i));
    }
    EXPECT_EQ(first.text(), "x0");
    EXPECT_EQ(first, interner.intern("x0"));
}

TEST(InternerTest, SymbolsWorkAsHashKeys) {
    StringInterner interner;
    std::unordered_map<Symbol, int> values;
    values[interner.intern("a")] = 1;
    values[interner.intern("b")] = 2;
    EXPECT_EQ(values.at(interner.intern("a")), 1);
    EXPECT_EQ(values.at(interner.intern("b")), 2);
}

} // namespace
} // namespace nitwit
