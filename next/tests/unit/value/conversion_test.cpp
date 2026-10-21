#include <bit>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "formats.hpp"
#include "value/arithmetic.hpp"

namespace nitwit::value {
namespace {

using namespace test;

// ---- Integer to integer ----

TEST(ConvertIntegerTest, WideningKeepsTheValue) {
    EXPECT_EQ(convertInteger(integer(-1, s32), s64).signedBits(), -1);
    EXPECT_EQ(convertInteger(Value::integer(0xFFFF'FFFF, u32), s64).bits(), 0xFFFF'FFFFu);
    EXPECT_EQ(convertInteger(integer(-128, s8), s128).signedBits(), -128);
}

TEST(ConvertIntegerTest, ToUnsignedIsModular) {
    // C17 6.3.1.3p2.
    EXPECT_EQ(convertInteger(integer(-1, s32), u32).bits(), 0xFFFF'FFFFu);
    EXPECT_EQ(convertInteger(integer(-1, s32), u64).bits(), 0xFFFF'FFFF'FFFF'FFFFu);
    EXPECT_EQ(convertInteger(integer(-1, s64), u128).bits(), ~UInt128{0});
    EXPECT_EQ(convertInteger(Value::integer(0x1'2345'6789, u64), u16).bits(), 0x6789u);
}

TEST(ConvertIntegerTest, ToSignedTruncatesLikeGcc) {
    // C17 6.3.1.3p3 leaves this implementation-defined; GCC reduces modulo 2^N.
    EXPECT_EQ(convertInteger(Value::integer(0x8000, u16), s16).signedBits(), -32768);
    EXPECT_EQ(convertInteger(Value::integer(0x1'2345'6789, s64), s32).bits(), 0x2345'6789u);
    EXPECT_EQ(convertInteger(Value::integer(0xFF, s64), s8).signedBits(), -1);
    EXPECT_EQ(convertInteger(Value::integer(~UInt128{0}, u128), s32).signedBits(), -1);
}

// ---- To _Bool ----

TEST(ToBoolTest, IntegersCompareWithZero) {
    EXPECT_EQ(toBool(Value::integer(0, s32)).bits(), 0u);
    EXPECT_EQ(toBool(Value::integer(256, s32)).bits(), 1u); // not a truncation to 8 bits
    EXPECT_EQ(toBool(integer(-1, s128)).bits(), 1u);
}

TEST(ToBoolTest, FloatingValuesCompareWithZero) {
    EXPECT_EQ(toBool(Value::ofFloat(-0.0F)).bits(), 0u);
    EXPECT_EQ(toBool(Value::ofDouble(0.5)).bits(), 1u);
    EXPECT_EQ(toBool(Value::ofLongDouble(LDBL_TRUE_MIN)).bits(), 1u);
    EXPECT_EQ(toBool(Value::ofDouble(std::numeric_limits<double>::quiet_NaN())).bits(), 1u);
    EXPECT_EQ(toBool(Value::ofFloat(-std::numeric_limits<float>::infinity())).bits(), 1u);
}

// ---- Integer to floating ----
//
// Every conversion rounds once, to nearest; the expected values come from gcc-14 -m64 and agree with gcc-13 -m32.

float toFloat(Int128 value, IntegerFormat from) {
    return integerToFloat(integer(value, from), from, FloatFormat::Float).asFloat();
}

double toDouble(Int128 value, IntegerFormat from) {
    return integerToFloat(integer(value, from), from, FloatFormat::Double).asDouble();
}

long double toLongDouble(Int128 value, IntegerFormat from) {
    return integerToFloat(integer(value, from), from, FloatFormat::LongDouble).asLongDouble();
}

TEST(IntegerToFloatTest, RoundsToNearestEven) {
    EXPECT_EQ(toFloat(16777217, s32), 0x1p24F);
    EXPECT_EQ(toFloat(16777219, s32), 0x1.000004p24F);
    EXPECT_EQ(toFloat(-16777217, s64), -0x1p24F);
    EXPECT_EQ(toDouble((Int128{1} << 53) + 1, s64), 0x1p53);
    EXPECT_EQ(toDouble((Int128{1} << 53) + 3, s64), 0x1.0000000000002p53);
}

TEST(IntegerToFloatTest, RoundsOnlyOnce) {
    // Rounding to double first would give a tie and round to 2^62.
    EXPECT_EQ(toFloat((Int128{1} << 62) + (Int128{1} << 38) + 1, s64), 0x1.000002p62F);
    EXPECT_EQ(toFloat((Int128{1} << 63) + (Int128{1} << 39) + 1, u64), 0x1.000002p63F);
    EXPECT_EQ(toDouble((Int128{1} << 63) - 1, s64), 0x1p63);
    EXPECT_EQ(toDouble((Int128{1} << 63) - 1024, s64), 0x1.fffffffffffffp62);
}

TEST(IntegerToFloatTest, UnsignedValuesUseTheFullRange) {
    EXPECT_EQ(toFloat(0xFFFF'FFFF, u32), 0x1p32F);
    EXPECT_EQ(toDouble(0xFFFF'FFFF, u32), 0x1.fffffffep31);
    EXPECT_EQ(toDouble(0xFFFF'FFFF'FFFF'FFFF, u64), 0x1p64);
    EXPECT_EQ(toLongDouble(0xFFFF'FFFF'FFFF'FFFF, u64), 0xf.fffffffffffffffp60L);
}

TEST(IntegerToFloatTest, SixtyFourBitValuesAreExactInLongDouble) {
    EXPECT_EQ(toLongDouble(std::numeric_limits<std::int64_t>::max(), s64), 0xf.ffffffffffffffep59L);
    EXPECT_EQ(toLongDouble(std::numeric_limits<std::int64_t>::min(), s64), -0x8p60L);
    EXPECT_EQ(toLongDouble(-5, s8), -5.0L);
}

TEST(IntegerToFloatTest, Int128Extremes) {
    const Int128 max = static_cast<Int128>(~UInt128{0} >> 1);
    EXPECT_EQ(toFloat(max, s128), 0x1p127F);
    EXPECT_EQ(toDouble(-max - 1, s128), -0x1p127);
    EXPECT_EQ(toLongDouble(max, s128), 0x8p124L);
    // 2^128 - 1 rounds to 2^128, beyond FLT_MAX.
    EXPECT_EQ(toFloat(-1, u128), std::numeric_limits<float>::infinity());
    EXPECT_EQ(toDouble(-1, u128), 0x1p128);
    EXPECT_EQ(toDouble((Int128{1} << 64) + 1, u128), 0x1p64);
}

// ---- Floating to integer ----

IntegerFormat parseFormat(const std::string& name) {
    static const std::map<std::string, IntegerFormat> formats{{"s8", s8},     {"u8", u8},    {"s16", s16}, {"u16", u16},
                                                              {"s32", s32},   {"u32", u32},  {"s64", s64}, {"u64", u64},
                                                              {"s128", s128}, {"u128", u128}};
    return formats.at(name);
}

UInt128 parseHex(const std::string& text) {
    UInt128 bits = 0;
    for (const char digit : text.substr(2)) {
        const auto nibble = static_cast<unsigned>(std::isdigit(static_cast<unsigned char>(digit)) != 0
                                                      ? digit - '0'
                                                      : std::tolower(static_cast<unsigned char>(digit)) - 'a' + 10);
        bits = (bits << 4) | nibble;
    }
    return bits;
}

Value floatingValue(const std::string& format, long double value) {
    if (format == "float") {
        return Value::ofFloat(static_cast<float>(value));
    }
    if (format == "double") {
        return Value::ofDouble(static_cast<double>(value));
    }
    return Value::ofLongDouble(value);
}

FloatFormat parseFloatFormat(const std::string& format) {
    if (format == "float") {
        return FloatFormat::Float;
    }
    return format == "double" ? FloatFormat::Double : FloatFormat::LongDouble;
}

/// Whether the integral part of `value` is representable in `format` (C17 6.3.1.4p1).
bool isRepresentable(long double value, IntegerFormat format) {
    const long double truncated = std::trunc(value);
    const long double lowest = format.isSigned ? -std::ldexp(1.0L, format.bits - 1) : 0.0L;
    const long double limit = std::ldexp(1.0L, format.isSigned ? format.bits - 1 : format.bits);
    return truncated >= lowest && truncated < limit;
}

TEST(FloatToIntegerTest, MatchesGccOnX86_64) {
    std::ifstream data(NITWIT_TEST_DATA_DIR "/float_to_integer.txt");
    ASSERT_TRUE(data) << "cannot open the test data";
    std::string line;
    int rows = 0;
    while (std::getline(data, line)) {
        if (line.empty() || line.front() == '#') {
            continue;
        }
        std::istringstream fields(line);
        std::string source;
        std::string valueText;
        std::string target;
        std::string resultText;
        fields >> source >> valueText >> target >> resultText;
        ASSERT_FALSE(fields.fail()) << line;
        const long double value = std::strtold(valueText.c_str(), nullptr);
        const IntegerFormat to = parseFormat(target);
        const Result result = floatToInteger(floatingValue(source, value), parseFloatFormat(source), to);
        EXPECT_EQ(result.value.bits(), Value::integer(parseHex(resultText), to).bits()) << line;
        EXPECT_EQ(result.undefinedBehavior,
                  isRepresentable(value, to) ? UndefinedBehavior::None : UndefinedBehavior::FloatToIntegerOverflow)
            << line;
        ++rows;
    }
    EXPECT_GT(rows, 1000);
}

TEST(FloatToIntegerTest, TruncatesTowardZero) {
    const Result negative = floatToInteger(Value::ofDouble(-2.9), FloatFormat::Double, s32);
    EXPECT_EQ(negative.value.signedBits(), -2);
    EXPECT_EQ(negative.undefinedBehavior, UndefinedBehavior::None);
    EXPECT_EQ(floatToInteger(Value::ofFloat(2.9F), FloatFormat::Float, u8).value.bits(), 2u);
}

TEST(FloatToIntegerTest, NegativeFractionsFitUnsignedTypes) {
    // The integral part of -0.9 is 0, which is representable.
    const Result result = floatToInteger(Value::ofDouble(-0.9), FloatFormat::Double, u32);
    EXPECT_EQ(result.value.bits(), 0u);
    EXPECT_EQ(result.undefinedBehavior, UndefinedBehavior::None);
}

TEST(FloatToIntegerTest, ValuesJustOutsideTheRange) {
    EXPECT_EQ(floatToInteger(Value::ofDouble(127.9), FloatFormat::Double, s8).undefinedBehavior,
              UndefinedBehavior::None);
    EXPECT_EQ(floatToInteger(Value::ofDouble(128.0), FloatFormat::Double, s8).undefinedBehavior,
              UndefinedBehavior::FloatToIntegerOverflow);
    EXPECT_EQ(floatToInteger(Value::ofDouble(-128.9), FloatFormat::Double, s8).undefinedBehavior,
              UndefinedBehavior::None);
    EXPECT_EQ(floatToInteger(Value::ofDouble(-129.0), FloatFormat::Double, s8).undefinedBehavior,
              UndefinedBehavior::FloatToIntegerOverflow);
    EXPECT_EQ(floatToInteger(Value::ofDouble(-1.0), FloatFormat::Double, u64).undefinedBehavior,
              UndefinedBehavior::FloatToIntegerOverflow);
    EXPECT_EQ(floatToInteger(Value::ofLongDouble(0x1.fffffffffffffffep63L), FloatFormat::LongDouble, u64).value.bits(),
              0xFFFF'FFFF'FFFF'FFFFu);
}

TEST(FloatToIntegerTest, NanAndInfinitiesAreUndefined) {
    for (const FloatFormat from : {FloatFormat::Float, FloatFormat::Double, FloatFormat::LongDouble}) {
        for (const long double value : {std::numeric_limits<long double>::quiet_NaN(), HUGE_VALL, -HUGE_VALL}) {
            const Value operand = from == FloatFormat::Float    ? Value::ofFloat(static_cast<float>(value))
                                  : from == FloatFormat::Double ? Value::ofDouble(static_cast<double>(value))
                                                                : Value::ofLongDouble(value);
            EXPECT_EQ(floatToInteger(operand, from, s32).undefinedBehavior, UndefinedBehavior::FloatToIntegerOverflow);
            // cvttsd2si and fistp give the "integer indefinite" value.
            EXPECT_EQ(floatToInteger(operand, from, s32).value.bits(), minBits(s32));
        }
    }
}

// ---- Floating to floating ----

TEST(ConvertFloatTest, WideningIsExact) {
    EXPECT_EQ(convertFloat(Value::ofFloat(0x1.fffffep127F), FloatFormat::Float, FloatFormat::Double).asDouble(),
              0x1.fffffep127);
    EXPECT_EQ(convertFloat(Value::ofDouble(0x1.fffffffffffffp-1), FloatFormat::Double, FloatFormat::LongDouble)
                  .asLongDouble(),
              0x1.fffffffffffffp-1L);
}

float doubleToFloat(double value) {
    return convertFloat(Value::ofDouble(value), FloatFormat::Double, FloatFormat::Float).asFloat();
}

TEST(ConvertFloatTest, NarrowingRoundsToNearestEven) {
    // gcc-14 -m64; gcc-13 -m32 -msse2 -mfpmath=sse agrees.
    EXPECT_EQ(doubleToFloat(0x1.000001p0), 1.0F);
    EXPECT_EQ(doubleToFloat(0x1.000003p0), 0x1.000004p0F);
    EXPECT_EQ(doubleToFloat(0x1p-149), 0x1p-149F);
    EXPECT_EQ(doubleToFloat(0x1p-150), 0.0F);
    EXPECT_EQ(doubleToFloat(0x1.8p-150), 0x1p-149F);
    EXPECT_EQ(convertFloat(Value::ofLongDouble(0x1.00000000000008p0L), FloatFormat::LongDouble, FloatFormat::Double)
                  .asDouble(),
              1.0);
    EXPECT_EQ(convertFloat(Value::ofLongDouble(0x1.00000000000018p0L), FloatFormat::LongDouble, FloatFormat::Double)
                  .asDouble(),
              0x1.0000000000002p0);
}

TEST(ConvertFloatTest, NarrowingOverflowsToInfinity) {
    EXPECT_EQ(doubleToFloat(1e40), std::numeric_limits<float>::infinity());
    EXPECT_EQ(doubleToFloat(-1e40), -std::numeric_limits<float>::infinity());
    // Half way between FLT_MAX and 2^128: the tie rounds to the even 2^128, which overflows; below it, FLT_MAX.
    EXPECT_EQ(doubleToFloat(0x1.ffffffp127), std::numeric_limits<float>::infinity());
    EXPECT_EQ(doubleToFloat(0x1.fffffefffffffp127), FLT_MAX);
    EXPECT_EQ(convertFloat(Value::ofLongDouble(LDBL_MAX), FloatFormat::LongDouble, FloatFormat::Double).asDouble(),
              std::numeric_limits<double>::infinity());
}

TEST(ConvertFloatTest, NarrowingUnderflowKeepsTheSign) {
    EXPECT_EQ(std::bit_cast<std::uint32_t>(doubleToFloat(-1e-50)), 0x8000'0000u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(doubleToFloat(-0.0)), 0x8000'0000u);
}

TEST(ConvertFloatTest, NanStaysQuietNan) {
    EXPECT_EQ(std::bit_cast<std::uint32_t>(doubleToFloat(std::numeric_limits<double>::quiet_NaN())), 0x7FC0'0000u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(doubleToFloat(-std::numeric_limits<double>::quiet_NaN())), 0xFFC0'0000u);
}

} // namespace
} // namespace nitwit::value
