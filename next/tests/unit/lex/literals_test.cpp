#include "lex/literals.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <string_view>
#include <tuple>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace nitwit::lex {
namespace {

using ::testing::ElementsAre;
using ::testing::HasSubstr;

// ---- Integer constants

TEST(IntegerLiteralTest, DecodesAllBases) {
    const std::pair<std::string_view, std::uint64_t> cases[] = {
        {"0", 0},         {"42", 42},       {"052", 42}, {"0x2a", 42}, {"0X2A", 42},
        {"0b101010", 42}, {"0B101010", 42}, {"00", 0},   {"0x0", 0},   {"1234567890", 1234567890},
    };
    for (const auto& [spelling, value] : cases) {
        const auto literal = decodeIntegerLiteral(spelling);
        ASSERT_TRUE(literal.has_value()) << spelling << ": " << literal.error().message;
        EXPECT_EQ(literal->value, value) << spelling;
    }
}

TEST(IntegerLiteralTest, OnlyDecimalConstantsAreDecimal) {
    EXPECT_TRUE(decodeIntegerLiteral("10")->isDecimal);
    EXPECT_FALSE(decodeIntegerLiteral("010")->isDecimal);
    EXPECT_FALSE(decodeIntegerLiteral("0x10")->isDecimal);
    EXPECT_FALSE(decodeIntegerLiteral("0b10")->isDecimal);
    EXPECT_FALSE(decodeIntegerLiteral("0")->isDecimal); // 0 is an octal constant (C17 6.4.4.1p1)
}

TEST(IntegerLiteralTest, AcceptsTheLargestValue) {
    EXPECT_EQ(decodeIntegerLiteral("18446744073709551615")->value, std::numeric_limits<std::uint64_t>::max());
    EXPECT_EQ(decodeIntegerLiteral("0xffffffffffffffffULL")->value, std::numeric_limits<std::uint64_t>::max());
    EXPECT_EQ(decodeIntegerLiteral("01777777777777777777777")->value, std::numeric_limits<std::uint64_t>::max());
}

TEST(IntegerLiteralTest, RejectsValuesAboveTheLargest) {
    EXPECT_FALSE(decodeIntegerLiteral("18446744073709551616").has_value());
    EXPECT_FALSE(decodeIntegerLiteral("0x10000000000000000").has_value());
    EXPECT_FALSE(decodeIntegerLiteral("99999999999999999999999999u").has_value());
}

TEST(IntegerLiteralTest, DecodesSuffixes) {
    const std::tuple<std::string_view, bool, int> cases[] = {
        {"1", false, 0},   {"1u", true, 0},   {"1U", true, 0},   {"1l", false, 1},   {"1L", false, 1},
        {"1ll", false, 2}, {"1LL", false, 2}, {"1ul", true, 1},  {"1lu", true, 1},   {"1uLL", true, 2},
        {"1LLu", true, 2}, {"1Ull", true, 2}, {"1llU", true, 2}, {"0x1Lu", true, 1}, {"07ull", true, 2},
    };
    for (const auto& [spelling, isUnsigned, longCount] : cases) {
        const auto literal = decodeIntegerLiteral(spelling);
        ASSERT_TRUE(literal.has_value()) << spelling;
        EXPECT_EQ(literal->value, spelling.starts_with("07") ? 7u : 1u) << spelling;
        EXPECT_EQ(literal->hasUnsignedSuffix, isUnsigned) << spelling;
        EXPECT_EQ(literal->longCount, longCount) << spelling;
    }
}

TEST(IntegerLiteralTest, RejectsInvalidSuffixes) {
    for (const std::string_view spelling : {"1lL", "1Ll", "1uu", "1lul", "1lll", "1x", "1f", "1i", "0x1g", "1.0"}) {
        const auto literal = decodeIntegerLiteral(spelling);
        ASSERT_FALSE(literal.has_value()) << spelling;
        EXPECT_THAT(literal.error().message, HasSubstr("invalid suffix")) << spelling;
    }
}

TEST(IntegerLiteralTest, RejectsInvalidDigits) {
    EXPECT_EQ(decodeIntegerLiteral("08").error().message, "invalid digit '8' in octal constant");
    EXPECT_EQ(decodeIntegerLiteral("0b102").error().message, "invalid digit '2' in binary constant");
}

TEST(IntegerLiteralTest, RejectsPrefixesWithoutDigits) {
    EXPECT_FALSE(decodeIntegerLiteral("0x").has_value());
    EXPECT_FALSE(decodeIntegerLiteral("0b").has_value());
    EXPECT_FALSE(decodeIntegerLiteral("0xu").has_value());
    EXPECT_FALSE(decodeIntegerLiteral("").has_value());
    EXPECT_FALSE(decodeIntegerLiteral("x1").has_value());
}

// ---- Floating constants

TEST(FloatingLiteralTest, DecodesDecimalConstantsAsDouble) {
    const std::pair<std::string_view, double> cases[] = {
        {"1.0", 1.0},       {"0.1", 0.1},    {"1.", 1.0},
        {".5", 0.5},        {"1e10", 1e10},  {"1E+2", 1e2},
        {"1.5e-3", 1.5e-3}, {"00.25", 0.25}, {"123.456e7", 123.456e7},
    };
    for (const auto& [spelling, value] : cases) {
        const auto literal = decodeFloatingLiteral(spelling);
        ASSERT_TRUE(literal.has_value()) << spelling << ": " << literal.error().message;
        EXPECT_EQ(literal->suffix, FloatingSuffix::None);
        EXPECT_EQ(literal->doubleValue, value) << spelling;
    }
}

TEST(FloatingLiteralTest, DecodesHexadecimalConstants) {
    const std::pair<std::string_view, double> cases[] = {
        {"0x1p4", 16.0}, {"0x1.8p1", 3.0}, {"0X.8P0", 0.5}, {"0x1.p-1", 0.5}, {"0xAp+0", 10.0}, {"0x1.fp3", 15.5},
    };
    for (const auto& [spelling, value] : cases) {
        const auto literal = decodeFloatingLiteral(spelling);
        ASSERT_TRUE(literal.has_value()) << spelling << ": " << literal.error().message;
        EXPECT_EQ(literal->doubleValue, value) << spelling;
    }
}

TEST(FloatingLiteralTest, SuffixSelectsTheType) {
    const auto f = decodeFloatingLiteral("0.1f");
    EXPECT_EQ(f->suffix, FloatingSuffix::F);
    EXPECT_EQ(f->floatValue, 0.1f);

    const auto l = decodeFloatingLiteral("0.1L");
    EXPECT_EQ(l->suffix, FloatingSuffix::L);
    EXPECT_EQ(l->longDoubleValue, 0.1L);

    for (const std::string_view spelling : {"0.1q", "0.1Q", "0.1f128", "0.1F128"}) {
        const auto q = decodeFloatingLiteral(spelling);
        ASSERT_TRUE(q.has_value()) << spelling;
        EXPECT_EQ(q->suffix, FloatingSuffix::F128) << spelling;
        EXPECT_EQ(q->longDoubleValue, 0.1L) << spelling;
    }
    EXPECT_EQ(decodeFloatingLiteral("0x1p3f")->floatValue, 8.0f);
}

TEST(FloatingLiteralTest, RoundsOnceToTheTargetType) {
    // Just above the midpoint between 1 and the next float, but within half an ulp of the midpoint in double:
    // rounding to double first and then to float would give 1.
    const auto literal = decodeFloatingLiteral("1.00000005960464478539f");
    ASSERT_TRUE(literal.has_value());
    EXPECT_EQ(literal->floatValue, std::nextafter(1.0f, 2.0f));
}

TEST(FloatingLiteralTest, KeepsSubnormalValues) {
    EXPECT_EQ(decodeFloatingLiteral("4.9e-324")->doubleValue, std::numeric_limits<double>::denorm_min());
    EXPECT_EQ(decodeFloatingLiteral("1e-45f")->floatValue, std::numeric_limits<float>::denorm_min());
}

TEST(FloatingLiteralTest, OverflowGivesInfinity) {
    EXPECT_EQ(decodeFloatingLiteral("1e400")->doubleValue, std::numeric_limits<double>::infinity());
    EXPECT_EQ(decodeFloatingLiteral("1e39f")->floatValue, std::numeric_limits<float>::infinity());
    EXPECT_EQ(decodeFloatingLiteral("1e5000L")->longDoubleValue, std::numeric_limits<long double>::infinity());
    EXPECT_EQ(decodeFloatingLiteral("0x1p99999")->doubleValue, std::numeric_limits<double>::infinity());
    EXPECT_EQ(decodeFloatingLiteral("123456789.0e99999999999999999999")->doubleValue,
              std::numeric_limits<double>::infinity());
}

TEST(FloatingLiteralTest, UnderflowGivesZero) {
    EXPECT_EQ(decodeFloatingLiteral("1e-400")->doubleValue, 0.0);
    EXPECT_EQ(decodeFloatingLiteral("2.4e-324")->doubleValue, 0.0);
    EXPECT_EQ(decodeFloatingLiteral("1e-50f")->floatValue, 0.0f);
    EXPECT_EQ(decodeFloatingLiteral("0.000001e-99999")->doubleValue, 0.0);
    EXPECT_EQ(decodeFloatingLiteral("0x0.01p-99999L")->longDoubleValue, 0.0L);
}

TEST(FloatingLiteralTest, RejectsMalformedConstants) {
    for (const std::string_view spelling : {"0x1.8", "1e", "1e+", "0x1p", ".", "0x.p1", "1..2", "1.0x", "1.0fl",
                                            "1.0ff", "1.0lf", "1.0d", "1.0f16", "1.0e5e5", "0x1p1.5"}) {
        EXPECT_FALSE(decodeFloatingLiteral(spelling).has_value()) << spelling;
    }
    EXPECT_THAT(decodeFloatingLiteral("0x1.8").error().message, HasSubstr("no exponent"));
    EXPECT_THAT(decodeFloatingLiteral("1.0fl").error().message, HasSubstr("invalid suffix 'fl'"));
}

// ---- Character constants

TEST(CharacterLiteralTest, DecodesPlainCharacters) {
    const auto literal = decodeCharacterLiteral("'a'");
    ASSERT_TRUE(literal.has_value());
    EXPECT_EQ(literal->encoding, CharEncoding::Plain);
    EXPECT_EQ(literal->value, 97);
    EXPECT_EQ(decodeCharacterLiteral("'\"'")->value, '"');
}

TEST(CharacterLiteralTest, DecodesSimpleEscapes) {
    const std::pair<std::string_view, std::int64_t> cases[] = {
        {R"('\'')", 0x27}, {R"('\"')", 0x22}, {R"('\?')", 0x3F}, {R"('\\')", 0x5C}, {R"('\a')", 0x07},
        {R"('\b')", 0x08}, {R"('\f')", 0x0C}, {R"('\n')", 0x0A}, {R"('\r')", 0x0D}, {R"('\t')", 0x09},
        {R"('\v')", 0x0B}, {R"('\e')", 0x1B}, {R"('\E')", 0x1B},
    };
    for (const auto& [spelling, value] : cases) {
        const auto literal = decodeCharacterLiteral(spelling);
        ASSERT_TRUE(literal.has_value()) << spelling;
        EXPECT_EQ(literal->value, value) << spelling;
    }
}

TEST(CharacterLiteralTest, DecodesNumericEscapes) {
    EXPECT_EQ(decodeCharacterLiteral(R"('\0')")->value, 0);
    EXPECT_EQ(decodeCharacterLiteral(R"('\101')")->value, 65);
    EXPECT_EQ(decodeCharacterLiteral(R"('\x41')")->value, 65);
    EXPECT_EQ(decodeCharacterLiteral(R"('\x7f')")->value, 127);
    EXPECT_EQ(decodeCharacterLiteral(R"('\x000041')")->value, 65);
}

TEST(CharacterLiteralTest, PlainCharIsSigned) {
    EXPECT_EQ(decodeCharacterLiteral(R"('\xff')")->value, -1);
    EXPECT_EQ(decodeCharacterLiteral(R"('\377')")->value, -1);
    EXPECT_EQ(decodeCharacterLiteral(R"('\x80')")->value, -128);
}

TEST(CharacterLiteralTest, OctalEscapesHaveAtMostThreeDigits) {
    EXPECT_EQ(decodeCharacterLiteral(R"('\1234')")->value, 0x5334); // '\123' and '4'
}

TEST(CharacterLiteralTest, MultiCharacterConstantsCombineBigEndian) {
    EXPECT_EQ(decodeCharacterLiteral("'ab'")->value, 0x6162);
    EXPECT_EQ(decodeCharacterLiteral("'abcd'")->value, 0x61626364);
    EXPECT_EQ(decodeCharacterLiteral(R"('\xff\xff')")->value, 0xFFFF);
    EXPECT_EQ(decodeCharacterLiteral(R"('\xff\xff\xff\xff')")->value, -1);
    EXPECT_EQ(decodeCharacterLiteral("'abcde'")->value, 0x62636465); // GCC keeps the last four
}

TEST(CharacterLiteralTest, Utf8InPlainConstantIsMultiCharacter) {
    EXPECT_EQ(decodeCharacterLiteral("'\xC3\xA9'")->value, 0xC3A9);
    EXPECT_EQ(decodeCharacterLiteral("'\\u00e9'")->value, 0xC3A9);
}

TEST(CharacterLiteralTest, DecodesPrefixedConstants) {
    const std::tuple<std::string_view, CharEncoding, std::int64_t> cases[] = {
        {"L'a'", CharEncoding::Wide, 97},
        {"L'\xC3\xA9'", CharEncoding::Wide, 0xE9},
        {R"(L'\U0001F600')", CharEncoding::Wide, 0x1F600},
        {R"(L'\xffffffff')", CharEncoding::Wide, -1}, // wchar_t is int
        {"u'\xC3\xA9'", CharEncoding::Utf16, 0xE9},
        {R"(u'\xffff')", CharEncoding::Utf16, 0xFFFF},
        {"U'\xF0\x9F\x98\x80'", CharEncoding::Utf32, 0x1F600},
        {R"(U'\xffffffff')", CharEncoding::Utf32, 0xFFFFFFFF},
        {"u8'a'", CharEncoding::Utf8, 97},
        {R"(u8'\xff')", CharEncoding::Utf8, 0xFF},
    };
    for (const auto& [spelling, encoding, value] : cases) {
        const auto literal = decodeCharacterLiteral(spelling);
        ASSERT_TRUE(literal.has_value()) << spelling << ": " << literal.error().message;
        EXPECT_EQ(literal->encoding, encoding) << spelling;
        EXPECT_EQ(literal->value, value) << spelling;
    }
}

TEST(CharacterLiteralTest, WideConstantWithSeveralCodeUnitsUsesTheLast) {
    EXPECT_EQ(decodeCharacterLiteral("L'ab'")->value, 'b');
    EXPECT_EQ(decodeCharacterLiteral(R"(u'\U0001F600')")->value, 0xDE00); // the low surrogate
}

TEST(CharacterLiteralTest, RejectsU8ConstantsBeyondOneCodeUnit) {
    EXPECT_FALSE(decodeCharacterLiteral("u8'\xC3\xA9'").has_value());
    EXPECT_FALSE(decodeCharacterLiteral("u8'ab'").has_value());
}

TEST(CharacterLiteralTest, RejectsEscapesOutOfRange) {
    EXPECT_THAT(decodeCharacterLiteral(R"('\400')").error().message, HasSubstr("octal escape sequence out of range"));
    EXPECT_THAT(decodeCharacterLiteral(R"('\x100')").error().message, HasSubstr("out of range"));
    EXPECT_FALSE(decodeCharacterLiteral(R"(u'\x10000')").has_value());
    EXPECT_FALSE(decodeCharacterLiteral(R"(L'\x100000000')").has_value());
    EXPECT_FALSE(decodeCharacterLiteral(R"('\x1234567890123456789012345')").has_value());
}

TEST(CharacterLiteralTest, RejectsInvalidUniversalCharacterNames) {
    EXPECT_FALSE(decodeCharacterLiteral("'\\u0041'").has_value()); // C17 6.4.3p2
    EXPECT_FALSE(decodeCharacterLiteral("U'\\uD800'").has_value());
    EXPECT_FALSE(decodeCharacterLiteral("U'\\U00110000'").has_value());
    EXPECT_FALSE(decodeCharacterLiteral("U'\\u12'").has_value());
    EXPECT_EQ(decodeCharacterLiteral("'\\u0024'")->value, '$');
    EXPECT_EQ(decodeCharacterLiteral("U'\\u00A0'")->value, 0xA0);
    EXPECT_EQ(decodeCharacterLiteral("U'\xC2\xA0'")->value, 0xA0);
}

TEST(CharacterLiteralTest, RejectsMalformedConstants) {
    EXPECT_EQ(decodeCharacterLiteral("''").error().message, "empty character constant");
    EXPECT_FALSE(decodeCharacterLiteral(R"('\x')").has_value());
    EXPECT_FALSE(decodeCharacterLiteral(R"('\')").has_value());
    EXPECT_FALSE(decodeCharacterLiteral("'a").has_value());
    EXPECT_FALSE(decodeCharacterLiteral("a'").has_value());
    EXPECT_FALSE(decodeCharacterLiteral("\"a\"").has_value());
    EXPECT_FALSE(decodeCharacterLiteral("'a'b'").has_value());
    EXPECT_FALSE(decodeCharacterLiteral("L'\xC3'").has_value()); // incomplete UTF-8
}

TEST(CharacterLiteralTest, UnknownEscapeIsTheCharacterItself) {
    EXPECT_EQ(decodeCharacterLiteral(R"('\%')")->value, '%'); // GCC warns and accepts it
    EXPECT_EQ(decodeCharacterLiteral("L'\\\xC3\xA9'")->value, 0xE9);
}

TEST(CharacterLiteralTest, BackslashNewlinesAreRemoved) {
    EXPECT_EQ(decodeCharacterLiteral("'a\\\n'")->value, 'a');
    EXPECT_EQ(decodeCharacterLiteral("'\\\\\nn'")->value, '\n');
    EXPECT_EQ(decodeCharacterLiteral("'\\\r\na'")->value, 'a');
}

// ---- String literals

std::vector<std::uint32_t> units(std::string_view text) {
    return std::vector<std::uint32_t>(text.begin(), text.end());
}

TEST(StringLiteralTest, DecodesPlainStrings) {
    const auto literal = decodeStringLiteral(R"("abc")");
    ASSERT_TRUE(literal.has_value());
    EXPECT_EQ(literal->encoding, CharEncoding::Plain);
    EXPECT_THAT(literal->codeUnits, ElementsAre('a', 'b', 'c'));
    EXPECT_TRUE(decodeStringLiteral(R"("")")->codeUnits.empty());
    EXPECT_THAT(decodeStringLiteral(R"("it's")")->codeUnits, ElementsAre('i', 't', '\'', 's'));
}

TEST(StringLiteralTest, DecodesEscapes) {
    EXPECT_THAT(decodeStringLiteral(R"("a\0b")")->codeUnits, ElementsAre('a', 0, 'b'));
    EXPECT_THAT(decodeStringLiteral(R"("\xff\377")")->codeUnits, ElementsAre(0xFF, 0xFF));
    EXPECT_THAT(decodeStringLiteral(R"("\1234")")->codeUnits, ElementsAre(0x53, '4'));
    EXPECT_THAT(decodeStringLiteral(R"("\x41g")")->codeUnits, ElementsAre(0x41, 'g'));
    EXPECT_THAT(decodeStringLiteral(R"("\"\\\n\e")")->codeUnits, ElementsAre('"', '\\', '\n', 0x1B));
    EXPECT_THAT(decodeStringLiteral(R"("100\%")")->codeUnits, ElementsAre('1', '0', '0', '%'));
}

TEST(StringLiteralTest, UniversalCharactersInNarrowStringsBecomeUtf8) {
    EXPECT_THAT(decodeStringLiteral("\"\\u00e9\"")->codeUnits, ElementsAre(0xC3, 0xA9));
    EXPECT_THAT(decodeStringLiteral("\"\\u20AC\"")->codeUnits, ElementsAre(0xE2, 0x82, 0xAC));
    EXPECT_THAT(decodeStringLiteral("\"\\U0001F600\"")->codeUnits, ElementsAre(0xF0, 0x9F, 0x98, 0x80));
    EXPECT_THAT(decodeStringLiteral("u8\"\\u00e9\"")->codeUnits, ElementsAre(0xC3, 0xA9));
}

TEST(StringLiteralTest, NarrowStringsKeepUtf8Bytes) {
    EXPECT_THAT(decodeStringLiteral("\"\xC3\xA9\"")->codeUnits, ElementsAre(0xC3, 0xA9));
    const auto literal = decodeStringLiteral("u8\"\xC3\xA9\"");
    EXPECT_EQ(literal->encoding, CharEncoding::Utf8);
    EXPECT_THAT(literal->codeUnits, ElementsAre(0xC3, 0xA9));
}

TEST(StringLiteralTest, WideStringsHoldCodePoints) {
    const auto wide = decodeStringLiteral("L\"\xC3\xA9\\U0001F600\"");
    EXPECT_EQ(wide->encoding, CharEncoding::Wide);
    EXPECT_THAT(wide->codeUnits, ElementsAre(0xE9, 0x1F600));

    const auto utf32 = decodeStringLiteral("U\"\xF0\x9F\x98\x80\"");
    EXPECT_EQ(utf32->encoding, CharEncoding::Utf32);
    EXPECT_THAT(utf32->codeUnits, ElementsAre(0x1F600));

    EXPECT_THAT(decodeStringLiteral(R"(L"\xffffffff")")->codeUnits, ElementsAre(0xFFFFFFFF));
}

TEST(StringLiteralTest, Utf16StringsUseSurrogatePairs) {
    const auto literal = decodeStringLiteral("u\"a\xF0\x9F\x98\x80\\U0001F600\"");
    EXPECT_EQ(literal->encoding, CharEncoding::Utf16);
    EXPECT_THAT(literal->codeUnits, ElementsAre('a', 0xD83D, 0xDE00, 0xD83D, 0xDE00));
    EXPECT_THAT(decodeStringLiteral(R"(u"\xffff")")->codeUnits, ElementsAre(0xFFFF));
}

TEST(StringLiteralTest, RejectsEscapesOutOfRange) {
    EXPECT_FALSE(decodeStringLiteral(R"("\x100")").has_value());
    EXPECT_FALSE(decodeStringLiteral(R"("\400")").has_value());
    EXPECT_FALSE(decodeStringLiteral(R"(u8"\x100")").has_value());
    EXPECT_FALSE(decodeStringLiteral(R"(u"\x10000")").has_value());
    EXPECT_FALSE(decodeStringLiteral(R"(U"\x100000000")").has_value());
}

TEST(StringLiteralTest, RejectsMalformedLiterals) {
    EXPECT_FALSE(decodeStringLiteral(R"("a"b")").has_value());
    EXPECT_FALSE(decodeStringLiteral(R"("abc)").has_value());
    EXPECT_FALSE(decodeStringLiteral(R"("abc\")").has_value());
    EXPECT_FALSE(decodeStringLiteral("'a'").has_value());
    EXPECT_FALSE(decodeStringLiteral("\"a\nb\"").has_value());
    EXPECT_FALSE(decodeStringLiteral("L\"\xFF\"").has_value());         // not UTF-8
    EXPECT_FALSE(decodeStringLiteral("U\"\xC0\x80\"").has_value());     // overlong
    EXPECT_FALSE(decodeStringLiteral("u\"\xED\xA0\x80\"").has_value()); // a surrogate
}

TEST(StringLiteralTest, BackslashNewlinesAreRemoved) {
    EXPECT_EQ(decodeStringLiteral("\"ab\\\ncd\"")->codeUnits, units("abcd"));
    EXPECT_EQ(decodeStringLiteral("\"ab\\\r\ncd\\\n\"")->codeUnits, units("abcd"));
    EXPECT_EQ(decodeStringLiteral("\"\\\\\n\"\"")->codeUnits, units("\""));
}

// ---- Concatenation

StringLiteral decoded(std::string_view spelling) {
    return decodeStringLiteral(spelling).value();
}

TEST(ConcatenateStringsTest, JoinsPlainStrings) {
    const auto result = concatenateStrings({decoded(R"("ab")"), decoded(R"("")"), decoded(R"("c")")});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->encoding, CharEncoding::Plain);
    EXPECT_EQ(result->codeUnits, units("abc"));
}

TEST(ConcatenateStringsTest, PrefixedPartDeterminesTheEncoding) {
    const auto wide = concatenateStrings({decoded(R"("a")"), decoded(R"(L"b")"), decoded("\"\xC3\xA9\"")});
    ASSERT_TRUE(wide.has_value());
    EXPECT_EQ(wide->encoding, CharEncoding::Wide);
    EXPECT_THAT(wide->codeUnits, ElementsAre('a', 'b', 0xE9));

    const auto utf16 = concatenateStrings({decoded("\"\xF0\x9F\x98\x80\""), decoded(R"(u"")")});
    EXPECT_THAT(utf16->codeUnits, ElementsAre(0xD83D, 0xDE00));

    const auto utf8 = concatenateStrings({decoded("\"\xC3\xA9\""), decoded(R"(u8"x")")});
    EXPECT_EQ(utf8->encoding, CharEncoding::Utf8);
    EXPECT_THAT(utf8->codeUnits, ElementsAre(0xC3, 0xA9, 'x'));
}

TEST(ConcatenateStringsTest, RejectsDifferentPrefixes) {
    EXPECT_FALSE(concatenateStrings({decoded(R"(L"a")"), decoded(R"(u"b")")}).has_value());
    EXPECT_FALSE(concatenateStrings({decoded(R"(u8"a")"), decoded(R"(U"b")")}).has_value());
    EXPECT_TRUE(concatenateStrings({decoded(R"(U"a")"), decoded(R"(U"b")")}).has_value());
}

TEST(ConcatenateStringsTest, RejectsPlainPartsThatAreNotUtf8InWideStrings) {
    EXPECT_FALSE(concatenateStrings({decoded(R"(L"a")"), decoded(R"("\xff")")}).has_value());
}

TEST(ConcatenateStringsTest, NoPartsGiveAnEmptyPlainString) {
    const auto result = concatenateStrings({});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->encoding, CharEncoding::Plain);
    EXPECT_TRUE(result->codeUnits.empty());
}

} // namespace
} // namespace nitwit::lex
