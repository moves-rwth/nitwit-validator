#include "lex/token.hpp"

#include <utility>

#include <gtest/gtest.h>

namespace nitwit::lex {
namespace {

TEST(TokenTest, KeywordsSpellTheirStandardName) {
    EXPECT_EQ(spelling(TokenKind::KwInt), "int");
    EXPECT_EQ(spelling(TokenKind::KwInline), "inline");
    EXPECT_EQ(spelling(TokenKind::KwAlignof), "_Alignof");
    EXPECT_EQ(spelling(TokenKind::KwThreadLocal), "_Thread_local");
    EXPECT_EQ(spelling(TokenKind::KwAttribute), "__attribute__");
    EXPECT_EQ(spelling(TokenKind::KwBuiltinTypesCompatibleP), "__builtin_types_compatible_p");
}

TEST(TokenTest, PunctuatorsSpellTheirText) {
    EXPECT_EQ(spelling(TokenKind::LeftSquare), "[");
    EXPECT_EQ(spelling(TokenKind::Arrow), "->");
    EXPECT_EQ(spelling(TokenKind::Ellipsis), "...");
    EXPECT_EQ(spelling(TokenKind::GreaterGreaterEqual), ">>=");
    EXPECT_EQ(spelling(TokenKind::Comma), ",");
}

TEST(TokenTest, OtherKindsAreDescribed) {
    EXPECT_EQ(spelling(TokenKind::EndOfFile), "end of file");
    EXPECT_EQ(spelling(TokenKind::Pragma), "#pragma");
    EXPECT_EQ(spelling(TokenKind::Identifier), "identifier");
    EXPECT_EQ(spelling(TokenKind::IntegerLiteral), "integer literal");
    EXPECT_EQ(spelling(TokenKind::StringLiteral), "string literal");
}

TEST(TokenTest, EveryKindHasASpelling) {
    for (auto value = std::to_underlying(TokenKind::EndOfFile); value <= std::to_underlying(TokenKind::Comma);
         ++value) {
        EXPECT_FALSE(spelling(static_cast<TokenKind>(value)).empty()) << int{value};
    }
}

TEST(TokenTest, IsKeywordHoldsExactlyForKeywords) {
    EXPECT_TRUE(isKeyword(TokenKind::KwAuto));
    EXPECT_TRUE(isKeyword(TokenKind::KwWhile));
    EXPECT_TRUE(isKeyword(TokenKind::KwBuiltinChooseExpr));
    EXPECT_FALSE(isKeyword(TokenKind::Identifier));
    EXPECT_FALSE(isKeyword(TokenKind::StringLiteral));
    EXPECT_FALSE(isKeyword(TokenKind::LeftSquare));
    EXPECT_FALSE(isKeyword(TokenKind::EndOfFile));
    EXPECT_FALSE(isKeyword(TokenKind::Pragma));
}

TEST(TokenTest, IsPunctuatorHoldsExactlyForPunctuators) {
    EXPECT_TRUE(isPunctuator(TokenKind::LeftSquare));
    EXPECT_TRUE(isPunctuator(TokenKind::Ellipsis));
    EXPECT_TRUE(isPunctuator(TokenKind::Comma));
    EXPECT_FALSE(isPunctuator(TokenKind::KwBuiltinChooseExpr));
    EXPECT_FALSE(isPunctuator(TokenKind::Pragma));
    EXPECT_FALSE(isPunctuator(TokenKind::Unknown));
    EXPECT_FALSE(isPunctuator(TokenKind::StringLiteral));
}

TEST(TokenTest, RangeCoversTheSpelling) {
    Token token;
    token.kind = TokenKind::Identifier;
    token.length = 3;
    token.location = {static_cast<FileId>(1), 10};
    EXPECT_EQ(token.range().begin.offset, 10u);
    EXPECT_EQ(token.range().end.offset, 13u);
    EXPECT_EQ(token.range().end.file, token.location.file);
    EXPECT_TRUE(token.is(TokenKind::Identifier));
}

} // namespace
} // namespace nitwit::lex
