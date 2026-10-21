#include "lex/lexer.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace nitwit::lex {
namespace {

using ::testing::ElementsAre;
using K = TokenKind;

class LexerTest : public ::testing::Test {
protected:
    // Lexes `text` as the file "test.c".
    LexResult lex(std::string text) {
        file_ = sources_.addFile("test.c", std::move(text));
        return lexFile(sources_, file_, interner_, diagnostics_);
    }

    // The kinds of the tokens, without the final EndOfFile.
    static std::vector<TokenKind> kinds(const LexResult& result) {
        std::vector<TokenKind> kinds;
        for (const Token& token : result.tokens) {
            if (!token.is(K::EndOfFile)) {
                kinds.push_back(token.kind);
            }
        }
        return kinds;
    }

    std::vector<TokenKind> kinds(std::string text) { return kinds(lex(std::move(text))); }

    // The spellings of the tokens, without the final EndOfFile.
    std::vector<std::string> spellings(const LexResult& result) const {
        std::vector<std::string> spellings;
        for (const Token& token : result.tokens) {
            if (!token.is(K::EndOfFile)) {
                spellings.emplace_back(spellingOf(sources_, token));
            }
        }
        return spellings;
    }

    std::vector<std::string> spellings(std::string text) { return spellings(lex(std::move(text))); }

    LineColumn position(const Token& token) const { return sources_.lineColumn(token.location); }

    PresumedLocation presumed(const Token& token) const { return sources_.presumed(token.location); }

    std::vector<std::string> messages() const {
        std::vector<std::string> messages;
        for (const Diagnostic& diagnostic : diagnostics_.diagnostics()) {
            messages.push_back(diagnostics_.format(diagnostic));
        }
        return messages;
    }

    SourceManager sources_;
    StringInterner interner_;
    DiagnosticEngine diagnostics_{sources_};
    FileId file_ = FileId::None;
};

TEST_F(LexerTest, EmptyFileHasOnlyEndOfFile) {
    const LexResult result = lex("");
    ASSERT_EQ(result.tokens.size(), 1u);
    EXPECT_TRUE(result.tokens[0].is(K::EndOfFile));
    EXPECT_EQ(result.tokens[0].location, (SourceLocation{file_, 0}));
    EXPECT_TRUE(result.tokens[0].atStartOfLine);
    EXPECT_FALSE(result.needsPreprocessing);
    EXPECT_FALSE(diagnostics_.hasErrors());
}

TEST_F(LexerTest, EveryPunctuatorLexesToItsKind) {
    for (auto value = std::to_underlying(K::LeftSquare); value <= std::to_underlying(K::Comma); ++value) {
        const auto kind = static_cast<TokenKind>(value);
        const LexResult result = lex(std::string(spelling(kind)));
        ASSERT_EQ(result.tokens.size(), 2u) << spelling(kind);
        EXPECT_EQ(result.tokens[0].kind, kind) << spelling(kind);
        EXPECT_EQ(result.tokens[0].length, spelling(kind).size());
    }
    EXPECT_FALSE(diagnostics_.hasErrors());
}

TEST_F(LexerTest, EveryKeywordLexesToItsKind) {
    for (auto value = std::to_underlying(K::KwAuto); value <= std::to_underlying(K::KwBuiltinChooseExpr); ++value) {
        const auto kind = static_cast<TokenKind>(value);
        const LexResult result = lex(std::string(spelling(kind)));
        ASSERT_EQ(result.tokens.size(), 2u) << spelling(kind);
        EXPECT_EQ(result.tokens[0].kind, kind) << spelling(kind);
        EXPECT_TRUE(result.tokens[0].identifier.empty()) << spelling(kind);
    }
}

TEST_F(LexerTest, GnuKeywordSpellingsMapToTheirKinds) {
    const std::pair<std::string_view, TokenKind> cases[] = {
        {"__const", K::KwConst},        {"__const__", K::KwConst},
        {"__inline", K::KwInline},      {"__inline__", K::KwInline},
        {"__restrict", K::KwRestrict},  {"__restrict__", K::KwRestrict},
        {"__signed", K::KwSigned},      {"__signed__", K::KwSigned},
        {"__volatile", K::KwVolatile},  {"__volatile__", K::KwVolatile},
        {"__alignof", K::KwAlignof},    {"__alignof__", K::KwAlignof},
        {"__complex", K::KwComplex},    {"__complex__", K::KwComplex},
        {"__thread", K::KwThreadLocal}, {"__asm", K::KwAsm},
        {"__asm__", K::KwAsm},          {"__attribute", K::KwAttribute},
        {"__typeof", K::KwTypeof},      {"__typeof__", K::KwTypeof},
        {"__float128", K::KwFloat128},  {"__real", K::KwReal},
        {"__imag", K::KwImag},
    };
    for (const auto& [text, kind] : cases) {
        EXPECT_THAT(kinds(std::string(text)), ElementsAre(kind)) << text;
    }
}

TEST_F(LexerTest, BuiltinTypeNamesAreIdentifiers) {
    const LexResult result = lex("__builtin_va_list __int128_t __uint128_t __builtin_expect Int inline_ _ __inline_");
    ASSERT_EQ(result.tokens.size(), 9u);
    for (std::size_t i = 0; i + 1 < result.tokens.size(); ++i) {
        const Token& token = result.tokens[i];
        EXPECT_TRUE(token.is(K::Identifier)) << spellingOf(sources_, token);
        EXPECT_EQ(token.identifier.text(), spellingOf(sources_, token));
    }
}

TEST_F(LexerTest, IdentifiersMayContainDollarSignsAndUtf8) {
    const LexResult result = lex("a$b $x h\xC3\xA9llo");
    EXPECT_THAT(spellings(result), ElementsAre("a$b", "$x", "h\xC3\xA9llo"));
    for (std::size_t i = 0; i < 3; ++i) {
        EXPECT_TRUE(result.tokens[i].is(K::Identifier));
    }
    EXPECT_FALSE(diagnostics_.hasErrors());
}

TEST_F(LexerTest, EqualIdentifiersShareASymbol) {
    const LexResult result = lex("abc abc abd");
    EXPECT_EQ(result.tokens[0].identifier, result.tokens[1].identifier);
    EXPECT_NE(result.tokens[0].identifier, result.tokens[2].identifier);
}

TEST_F(LexerTest, DigraphsMapToBrackets) {
    EXPECT_THAT(kinds("<: :> <% %>"), ElementsAre(K::LeftSquare, K::RightSquare, K::LeftBrace, K::RightBrace));
}

TEST_F(LexerTest, PunctuatorsAreLongestMatches) {
    EXPECT_THAT(kinds("a+++++b"), ElementsAre(K::Identifier, K::PlusPlus, K::PlusPlus, K::Plus, K::Identifier));
    EXPECT_THAT(kinds("x->y--z"), ElementsAre(K::Identifier, K::Arrow, K::Identifier, K::MinusMinus, K::Identifier));
    EXPECT_THAT(kinds("<<= >>= <<< .. ..."), ElementsAre(K::LessLessEqual, K::GreaterGreaterEqual, K::LessLess, K::Less,
                                                         K::Period, K::Period, K::Ellipsis));
    EXPECT_THAT(kinds("a/b/=c"), ElementsAre(K::Identifier, K::Slash, K::Identifier, K::SlashEqual, K::Identifier));
}

TEST_F(LexerTest, NumbersAreClassifiedAsIntegerOrFloating) {
    const std::pair<std::string_view, TokenKind> cases[] = {
        {"42", K::IntegerLiteral},     {"42ul", K::IntegerLiteral},   {"0x2A", K::IntegerLiteral},
        {"017", K::IntegerLiteral},    {"0b101", K::IntegerLiteral},  {"0xe", K::IntegerLiteral},
        {"0x1e+1", K::IntegerLiteral}, {"123abc", K::IntegerLiteral}, {"1.0", K::FloatingLiteral},
        {"1.", K::FloatingLiteral},    {".5", K::FloatingLiteral},    {"1e10", K::FloatingLiteral},
        {"1E-3f", K::FloatingLiteral}, {"0x1p4", K::FloatingLiteral}, {"0X1P+4L", K::FloatingLiteral},
        {"0x1.8", K::FloatingLiteral}, {"1.2.3", K::FloatingLiteral}, {"1e", K::FloatingLiteral},
    };
    for (const auto& [text, kind] : cases) {
        const LexResult result = lex(std::string(text));
        ASSERT_EQ(result.tokens.size(), 2u) << text;
        EXPECT_EQ(result.tokens[0].kind, kind) << text;
        EXPECT_EQ(result.tokens[0].length, text.size()) << text;
    }
}

TEST_F(LexerTest, SignsBelongToNumbersOnlyAfterExponentLetters) {
    EXPECT_THAT(spellings("1+2"), ElementsAre("1", "+", "2"));
    EXPECT_THAT(spellings("1e+2-3"), ElementsAre("1e+2", "-", "3"));
    EXPECT_THAT(spellings("0x1p-2+1"), ElementsAre("0x1p-2", "+", "1"));
}

TEST_F(LexerTest, LiteralsKeepTheirPrefixes) {
    const LexResult result = lex(R"("a" L"b" u"c" U"d" u8"e" 'x' L'x' u'x' U'x' u8'x')");
    EXPECT_THAT(spellings(result), ElementsAre(R"("a")", R"(L"b")", R"(u"c")", R"(U"d")", R"(u8"e")", "'x'", "L'x'",
                                               "u'x'", "U'x'", "u8'x'"));
    for (std::size_t i = 0; i < 5; ++i) {
        EXPECT_TRUE(result.tokens[i].is(K::StringLiteral));
        EXPECT_TRUE(result.tokens[i + 5].is(K::CharacterLiteral));
    }
}

TEST_F(LexerTest, PrefixLettersWithoutQuoteAreIdentifiers) {
    EXPECT_THAT(kinds("L u U u8 Lx u8x"),
                ElementsAre(K::Identifier, K::Identifier, K::Identifier, K::Identifier, K::Identifier, K::Identifier));
    EXPECT_THAT(kinds("u8 \"x\""), ElementsAre(K::Identifier, K::StringLiteral));
}

TEST_F(LexerTest, EscapedQuotesDoNotEndLiterals) {
    EXPECT_THAT(spellings(R"("a\"b" '\'' "\\" 'x' "'" '"')"),
                ElementsAre(R"("a\"b")", R"('\'')", R"("\\")", "'x'", R"("'")", R"('"')"));
}

TEST_F(LexerTest, TokensRecordLineStartsAndLeadingSpace) {
    const LexResult result = lex("a b\n c\nd/**/e");
    ASSERT_EQ(result.tokens.size(), 6u);
    const auto flags = [&](std::size_t i) {
        return std::pair{result.tokens[i].atStartOfLine, result.tokens[i].hasLeadingSpace};
    };
    EXPECT_EQ(flags(0), std::pair(true, false));
    EXPECT_EQ(flags(1), std::pair(false, true));
    EXPECT_EQ(flags(2), std::pair(true, true));
    EXPECT_EQ(flags(3), std::pair(true, false));
    EXPECT_EQ(flags(4), std::pair(false, true));
}

TEST_F(LexerTest, BlockCommentSpanningLinesStartsALine) {
    const LexResult result = lex("a /* x\n y */ b /* z */ c");
    EXPECT_TRUE(result.tokens[1].atStartOfLine);
    EXPECT_TRUE(result.tokens[1].hasLeadingSpace);
    EXPECT_FALSE(result.tokens[2].atStartOfLine);
}

TEST_F(LexerTest, CommentsAreSkipped) {
    EXPECT_THAT(spellings("a // b\nc /* d */ e /* f\ng */ h /*/ i */ j//"), ElementsAre("a", "c", "e", "h", "j"));
    EXPECT_FALSE(diagnostics_.hasErrors());
}

TEST_F(LexerTest, BackslashNewlineContinuesALineComment) {
    EXPECT_THAT(spellings("a // b \\\n c\nd // e \\\r\n f\r\ng // h \\  \n i\nj"), ElementsAre("a", "d", "g", "j"));
    EXPECT_TRUE(diagnostics_.diagnostics().empty()); // GCC does not warn about blanks after the backslash in comments
}

TEST_F(LexerTest, BackslashNewlineMaySplitTheEndOfABlockComment) {
    EXPECT_THAT(spellings("a /* x *\\\n/ b /* y *\\\r\n\\\n/ c /* *\\ */ d"), ElementsAre("a", "b", "c", "d"));
    EXPECT_FALSE(diagnostics_.hasErrors());
}

TEST_F(LexerTest, ColumnsCountTabsAsOneColumn) {
    const LexResult result = lex("\tint\tx;\n  \t y");
    EXPECT_EQ(position(result.tokens[0]), (LineColumn{1, 2}));
    EXPECT_EQ(position(result.tokens[1]), (LineColumn{1, 6}));
    EXPECT_EQ(position(result.tokens[2]), (LineColumn{1, 7}));
    EXPECT_EQ(position(result.tokens[3]), (LineColumn{2, 5}));
}

TEST_F(LexerTest, TokensKnowTheirSpellingAndRange) {
    const LexResult result = lex("int  value = 42;");
    EXPECT_EQ(spellingOf(sources_, result.tokens[1]), "value");
    EXPECT_EQ(result.tokens[1].range(), (SourceRange{{file_, 5}, {file_, 10}}));
    EXPECT_EQ(spellingOf(sources_, result.tokens[3]), "42");
}

TEST_F(LexerTest, FileWithoutTrailingNewlineEndsAfterTheLastToken) {
    const LexResult result = lex("int x");
    ASSERT_EQ(result.tokens.size(), 3u);
    const Token& end = result.tokens[2];
    EXPECT_TRUE(end.is(K::EndOfFile));
    EXPECT_EQ(end.location.offset, 5u);
    EXPECT_EQ(end.length, 0u);
    EXPECT_FALSE(end.atStartOfLine);
    EXPECT_FALSE(diagnostics_.hasErrors());
}

TEST_F(LexerTest, TokenVectorOfASparseFileIsShrunk) {
    const LexResult result = lex("x" + std::string(1000, ' ') + "y");
    EXPECT_EQ(result.tokens.size(), 3u);
    EXPECT_LE(result.tokens.capacity(), 2 * result.tokens.size());
}

TEST_F(LexerTest, EndOfFileAfterNewlineStartsALine) {
    const LexResult result = lex("x;\n");
    EXPECT_TRUE(result.tokens.back().atStartOfLine);
    EXPECT_EQ(result.tokens.back().location.offset, 3u);
}

TEST_F(LexerTest, CarriageReturnLineEndingsAreLineBreaks) {
    const LexResult result = lex("a\r\n b\r\n#pragma x\r\nc\r\n");
    EXPECT_THAT(spellings(result), ElementsAre("a", "b", "#pragma x", "c"));
    EXPECT_TRUE(result.tokens[1].atStartOfLine);
    EXPECT_EQ(position(result.tokens[1]), (LineColumn{2, 2}));
    EXPECT_EQ(position(result.tokens[3]), (LineColumn{4, 1}));
    EXPECT_FALSE(diagnostics_.hasErrors());
}

TEST_F(LexerTest, ByteOrderMarkIsIgnored) {
    const LexResult result = lex("\xEF\xBB\xBFint x;");
    EXPECT_THAT(spellings(result), ElementsAre("int", "x", ";"));
    EXPECT_TRUE(result.tokens[0].atStartOfLine);
    EXPECT_FALSE(result.tokens[0].hasLeadingSpace);
}

TEST_F(LexerTest, LineMarkerSetsThePresumedLineOfTheNextLine) {
    const LexResult result = lex("# 10 \"a.c\"\nint x;\n\ny;\n");
    const PresumedLocation x = presumed(result.tokens[1]);
    EXPECT_EQ(x.fileName, "a.c");
    EXPECT_EQ(x.line, 10u);
    EXPECT_EQ(x.column, 5u);
    EXPECT_EQ(presumed(result.tokens[3]).line, 12u);
    EXPECT_FALSE(result.needsPreprocessing);
    EXPECT_FALSE(diagnostics_.hasErrors());
}

TEST_F(LexerTest, LineMarkersAcceptFlags) {
    const LexResult result = lex("# 0 \"a.c\"\n# 0 \"<built-in>\"\n# 1 \"/usr/include/stdio.h\" 1 3 4\nint x;\n"
                                 "# 5 \"a.c\" 2\ny\n");
    EXPECT_EQ(presumed(result.tokens[0]).fileName, "/usr/include/stdio.h");
    EXPECT_EQ(presumed(result.tokens[0]).line, 1u);
    EXPECT_EQ(presumed(result.tokens[3]).fileName, "a.c");
    EXPECT_EQ(presumed(result.tokens[3]).line, 5u);
    EXPECT_FALSE(diagnostics_.hasErrors());
}

TEST_F(LexerTest, LineDirectiveSetsThePresumedLine) {
    const LexResult result = lex("#line 20 \"b.c\"\nx\n");
    EXPECT_EQ(presumed(result.tokens[0]).fileName, "b.c");
    EXPECT_EQ(presumed(result.tokens[0]).line, 20u);
}

TEST_F(LexerTest, LineMarkerWithoutFileNameKeepsThePresumedFileName) {
    const LexResult result = lex("# 5 \"a.c\"\nx\n#line 20\ny\n");
    EXPECT_EQ(presumed(result.tokens[1]).fileName, "a.c");
    EXPECT_EQ(presumed(result.tokens[1]).line, 20u);
}

TEST_F(LexerTest, LineDirectiveBeforeAnyMarkerKeepsTheFileName) {
    const LexResult result = lex("#line 7\nx");
    EXPECT_EQ(presumed(result.tokens[0]).fileName, "test.c");
    EXPECT_EQ(presumed(result.tokens[0]).line, 7u);
}

TEST_F(LexerTest, LineMarkerFileNameIsDecoded) {
    const LexResult result = lex(R"(# 1 "C:\\dir\\a.c")"
                                 "\nx");
    EXPECT_EQ(presumed(result.tokens[0]).fileName, R"(C:\dir\a.c)");
}

TEST_F(LexerTest, DirectivesMayContainBlanksAndComments) {
    const LexResult result = lex("  #  line 3 /* c */ \"a.c\" // d\nx");
    EXPECT_EQ(presumed(result.tokens[0]).fileName, "a.c");
    EXPECT_EQ(presumed(result.tokens[0]).line, 3u);
    EXPECT_FALSE(diagnostics_.hasErrors());
}

TEST_F(LexerTest, BackslashNewlineContinuesADirective) {
    const LexResult result = lex("#line 5 \\\n\"a.c\"\nx");
    EXPECT_EQ(position(result.tokens[0]).line, 3u);
    EXPECT_EQ(presumed(result.tokens[0]).fileName, "a.c");
    EXPECT_EQ(presumed(result.tokens[0]).line, 5u);
}

TEST_F(LexerTest, DigraphOfHashStartsADirective) {
    const LexResult result = lex("%:line 9\nx\n%:pragma once\ny");
    EXPECT_THAT(spellings(result), ElementsAre("x", "%:pragma once", "y"));
    EXPECT_TRUE(result.tokens[1].is(K::Pragma));
    EXPECT_EQ(presumed(result.tokens[0]).line, 9u);
    EXPECT_FALSE(diagnostics_.hasErrors());
}

class InvalidLineMarkerTest : public LexerTest, public ::testing::WithParamInterface<std::string_view> {};

TEST_P(InvalidLineMarkerTest, IsAnErrorAndIgnored) {
    const LexResult result = lex(std::string(GetParam()) + "\nx");
    EXPECT_TRUE(diagnostics_.hasErrors());
    EXPECT_THAT(spellings(result), ElementsAre("x"));
    EXPECT_EQ(presumed(result.tokens[0]).fileName, "test.c");
    EXPECT_EQ(presumed(result.tokens[0]).line, 2u);
}

INSTANTIATE_TEST_SUITE_P(LexerTest, InvalidLineMarkerTest,
                         ::testing::Values("# 12 foo", "#line", "#line abc", "#line 12x", "#line 2147483648",
                                           "# 99999999999", "#line 3 \"a.c\" junk", "#line 3 \"a.c\" 1", "# 3 \"a.c",
                                           "# 3 L\"a.c\"", "# 3 \"\\x\""));

TEST_F(LexerTest, EachPragmaBecomesOneToken) {
    const LexResult result = lex("int a;\n#pragma pack(push, 1)\nstruct s;\n  # pragma omp parallel \\\n for\n"
                                 "#pragma pack(pop)");
    EXPECT_THAT(spellings(result), ElementsAre("int", "a", ";", "#pragma pack(push, 1)", "struct", "s", ";",
                                               "# pragma omp parallel \\\n for", "#pragma pack(pop)"));
    EXPECT_THAT(kinds(result), ElementsAre(K::KwInt, K::Identifier, K::Semicolon, K::Pragma, K::KwStruct, K::Identifier,
                                           K::Semicolon, K::Pragma, K::Pragma));
    const Token& indented = result.tokens[7];
    EXPECT_TRUE(indented.atStartOfLine);
    EXPECT_TRUE(indented.hasLeadingSpace);
    EXPECT_EQ(position(indented), (LineColumn{4, 3}));
    EXPECT_TRUE(result.tokens[8].atStartOfLine);
    EXPECT_FALSE(result.tokens[8].hasLeadingSpace);
    EXPECT_FALSE(diagnostics_.hasErrors());
}

TEST_F(LexerTest, PragmaTokenIncludesQuotedTextAndComments) {
    const LexResult result = lex("#pragma message(\"a // b\") /* c\nd */ e\n#pragma it's // f\nx");
    EXPECT_THAT(spellings(result), ElementsAre("#pragma message(\"a // b\") /* c\nd */ e", "#pragma it's // f", "x"));
    EXPECT_TRUE(result.tokens[0].is(K::Pragma));
    EXPECT_TRUE(result.tokens[0].atStartOfLine);
    EXPECT_FALSE(result.tokens[0].hasLeadingSpace);
    EXPECT_FALSE(diagnostics_.hasErrors());
}

TEST_F(LexerTest, IdentLinesAreSkipped) {
    EXPECT_THAT(spellings("#ident \"v1\"\n# ident \"v2\" \\\n \"v3\"\nx"), ElementsAre("x"));
    EXPECT_FALSE(diagnostics_.hasErrors());
}

TEST_F(LexerTest, NullDirectiveIsSkipped) {
    EXPECT_THAT(spellings("#\n  # \nx"), ElementsAre("x"));
    EXPECT_FALSE(diagnostics_.hasErrors());
}

class PreprocessorDirectiveTest : public LexerTest, public ::testing::WithParamInterface<std::string_view> {};

TEST_P(PreprocessorDirectiveTest, StopsLexing) {
    const LexResult result = lex("int a;\n" + std::string(GetParam()) + "\nint b;\n");
    EXPECT_TRUE(result.needsPreprocessing);
    EXPECT_THAT(spellings(result), ElementsAre("int", "a", ";"));
    ASSERT_EQ(result.tokens.size(), 4u);
    EXPECT_TRUE(result.tokens.back().is(K::EndOfFile));
    EXPECT_EQ(result.tokens.back().location.offset, 7u); // at the directive
    EXPECT_FALSE(diagnostics_.hasErrors());
}

INSTANTIATE_TEST_SUITE_P(LexerTest, PreprocessorDirectiveTest,
                         ::testing::Values("#include <stdio.h>", "# include \"a.h\"", "#define X 1", "%:define X",
                                           "#undef X", "#if 0", "#ifdef X", "#ifndef X", "#elif 1", "#else", "#endif",
                                           "#error stop", "#warning careful", "#pragmatic", "#!", "# \"a.c\""));

TEST_F(LexerTest, HashInsideALineIsNotADirective) {
    const LexResult result = lex("a # define\n");
    EXPECT_THAT(spellings(result), ElementsAre("a", "#", "define"));
    EXPECT_TRUE(result.tokens[1].is(K::Unknown));
    EXPECT_FALSE(result.needsPreprocessing);
    EXPECT_THAT(messages(), ElementsAre("test.c:1:3: error: stray '#' in program"));
}

TEST_F(LexerTest, DoubleHashIsNotADirective) {
    const LexResult result = lex("## x %:%: y\n");
    EXPECT_THAT(spellings(result), ElementsAre("##", "x", "%:%:", "y"));
    EXPECT_TRUE(result.tokens[0].is(K::Unknown));
    EXPECT_TRUE(result.tokens[2].is(K::Unknown));
    EXPECT_FALSE(result.needsPreprocessing);
}

TEST_F(LexerTest, HashAfterACommentAtLineStartStartsADirective) {
    const LexResult result = lex("/* c */ #pragma x\nint y;\n/* d\n e */ #ident \"z\"\n");
    EXPECT_THAT(spellings(result), ElementsAre("#pragma x", "int", "y", ";"));
    EXPECT_TRUE(result.tokens[0].hasLeadingSpace);
    EXPECT_FALSE(diagnostics_.hasErrors());
}

TEST_F(LexerTest, DirectivesStartOnlyLogicalLines) {
    // After a block comment that started on the line of a token, or after a backslash-newline, '#' is in the middle
    // of a logical line, as in GCC.
    const LexResult result = lex("a /* b\n */ #pragma c\nd \\\n# define e\n");
    EXPECT_THAT(spellings(result), ElementsAre("a", "#", "pragma", "c", "d", "#", "define", "e"));
    EXPECT_FALSE(result.needsPreprocessing);
    EXPECT_THAT(messages(),
                ElementsAre("test.c:2:5: error: stray '#' in program", "test.c:4:1: error: stray '#' in program"));
}

TEST_F(LexerTest, StrayCharactersAreUnknownTokens) {
    const LexResult result = lex("a @ b ` \\ c");
    EXPECT_THAT(kinds(result),
                ElementsAre(K::Identifier, K::Unknown, K::Identifier, K::Unknown, K::Unknown, K::Identifier));
    EXPECT_THAT(messages(),
                ElementsAre("test.c:1:3: error: stray '@' in program", "test.c:1:7: error: stray '`' in program",
                            "test.c:1:9: error: stray '\\' in program"));
}

TEST_F(LexerTest, NullCharactersAreIgnoredWithAWarning) {
    const LexResult result = lex(std::string("a\0\0b", 4));
    EXPECT_THAT(spellings(result), ElementsAre("a", "b"));
    EXPECT_TRUE(result.tokens[1].hasLeadingSpace);
    EXPECT_FALSE(diagnostics_.hasErrors());
    EXPECT_THAT(messages(), ElementsAre("test.c:1:2: warning: null character(s) ignored"));
}

TEST_F(LexerTest, RawStringLiteralsAreNotSupported) {
    const LexResult result = lex("a R\"x(b\")c\n)x\" d LR\"(e)\" u8R\"(\nf");
    EXPECT_THAT(spellings(result), ElementsAre("a", "R\"x(b\")c\n)x\"", "d", "LR\"(e)\"", "u8R\"(", "f"));
    EXPECT_THAT(kinds(result),
                ElementsAre(K::Identifier, K::Unknown, K::Identifier, K::Unknown, K::Unknown, K::Identifier));
    EXPECT_THAT(messages(), ElementsAre("test.c:1:3: error: raw string literals are not supported",
                                        "test.c:2:7: error: raw string literals are not supported",
                                        "test.c:2:15: error: raw string literals are not supported"));
}

TEST_F(LexerTest, BackslashNewlineBetweenTokensIsALineBreak) {
    const LexResult result = lex("a \\\nb \\\r\n c");
    EXPECT_THAT(spellings(result), ElementsAre("a", "b", "c"));
    EXPECT_TRUE(result.tokens[1].atStartOfLine);
    EXPECT_FALSE(result.tokens[1].hasLeadingSpace);
    EXPECT_TRUE(result.tokens[2].hasLeadingSpace);
    EXPECT_FALSE(diagnostics_.hasErrors());
}

TEST_F(LexerTest, BackslashNewlinesBeforeWhitespaceDoNotJoinTokens) {
    EXPECT_THAT(spellings("a\\\n\\\n b\\\n"), ElementsAre("a", "b"));
    EXPECT_FALSE(diagnostics_.hasErrors());
}

TEST_F(LexerTest, BackslashNewlineBetweenTokensThatDoNotJoinIsAccepted) {
    const LexResult result = lex("return x\\\n; f(a,\\\nb) \"a\"\\\n\"b\" 1\\\n+2 a\\\n.b x-\\\n1");
    EXPECT_THAT(spellings(result), ElementsAre("return", "x", ";", "f", "(", "a", ",", "b", ")", "\"a\"", "\"b\"", "1",
                                               "+", "2", "a", ".", "b", "x", "-", "1"));
    EXPECT_FALSE(diagnostics_.hasErrors());
}

class JoiningBackslashNewlineTest : public LexerTest, public ::testing::WithParamInterface<std::string_view> {};

TEST_P(JoiningBackslashNewlineTest, IsAnError) {
    lex(std::string(GetParam()));
    std::vector<std::string> errors;
    for (const Diagnostic& diagnostic : diagnostics_.diagnostics()) {
        if (diagnostic.severity == Severity::Error) {
            errors.push_back(diagnostic.message);
        }
    }
    EXPECT_THAT(errors, ElementsAre("backslash-newline inside a token is not supported"));
}

INSTANTIATE_TEST_SUITE_P(LexerTest, JoiningBackslashNewlineTest,
                         ::testing::Values("ab\\\ncd", "x 12\\\n3", "1\\\n.5", "1e\\\n+5", ".\\\n5", "a -\\\n> b",
                                           "a +\\\n= b", "a <<\\\n= b", "a .\\\n..", "x <\\\n: y", "a /\\\n/ b",
                                           "a /\\\n* b */", "L\\\n\"x\"", "u8\\\n'x'", "a\\ \nb"));

TEST_F(LexerTest, BlanksBetweenBackslashAndNewlineGiveAWarning) {
    const LexResult result = lex("a \\  \nb \"c\\ \t\r\nd\" #e");
    EXPECT_THAT(spellings(result), ElementsAre("a", "b", "\"c\\ \t\r\nd\"", "#", "e"));
    EXPECT_THAT(messages(), ElementsAre("test.c:1:3: warning: backslash and newline separated by space",
                                        "test.c:2:5: warning: backslash and newline separated by space",
                                        "test.c:3:4: error: stray '#' in program"));
}

TEST_F(LexerTest, BackslashNewlineInsideLiteralsIsPartOfTheToken) {
    const LexResult result = lex("\"ab\\\ncd\" 'x\\\r\n'");
    EXPECT_THAT(spellings(result), ElementsAre("\"ab\\\ncd\"", "'x\\\r\n'"));
    EXPECT_TRUE(result.tokens[0].is(K::StringLiteral));
    EXPECT_TRUE(result.tokens[1].is(K::CharacterLiteral));
    EXPECT_FALSE(diagnostics_.hasErrors());
}

TEST_F(LexerTest, BackslashNewlineIsRemovedBeforeEscapes) {
    // The second backslash and the newline go first, so the first backslash escapes the quote (C17 5.1.1.2p1).
    const LexResult result = lex("\"\\\\\n\"x\" y");
    EXPECT_THAT(spellings(result), ElementsAre("\"\\\\\n\"x\"", "y"));
}

TEST_F(LexerTest, UnterminatedBlockCommentIsAnError) {
    const LexResult result = lex("a /* b\nc");
    EXPECT_THAT(spellings(result), ElementsAre("a"));
    EXPECT_THAT(messages(), ElementsAre("test.c:1:3: error: unterminated comment"));
}

TEST_F(LexerTest, UnterminatedStringIsAnError) {
    const LexResult result = lex("\"abc\r\nx");
    EXPECT_THAT(spellings(result), ElementsAre("\"abc", "x"));
    EXPECT_TRUE(result.tokens[0].is(K::Unknown));
    EXPECT_TRUE(result.tokens[1].atStartOfLine);
    EXPECT_THAT(messages(), ElementsAre("test.c:1:1: error: missing terminating '\"' character"));
}

TEST_F(LexerTest, UnterminatedCharacterConstantIsAnError) {
    const LexResult result = lex("x = 'a;\ny");
    EXPECT_THAT(spellings(result), ElementsAre("x", "=", "'a;", "y"));
    EXPECT_TRUE(result.tokens[2].is(K::Unknown));
    EXPECT_THAT(messages(), ElementsAre("test.c:1:5: error: missing terminating ' character"));
}

TEST_F(LexerTest, UnterminatedLiteralAtEndOfFileIsAnError) {
    const LexResult result = lex("L\"abc\\");
    EXPECT_THAT(spellings(result), ElementsAre("L\"abc\\"));
    EXPECT_TRUE(result.tokens[0].is(K::Unknown));
    EXPECT_TRUE(diagnostics_.hasErrors());
}

} // namespace
} // namespace nitwit::lex
