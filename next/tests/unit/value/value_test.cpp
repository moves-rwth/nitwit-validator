#include "value/value.hpp"

#include <array>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <limits>

#include <gtest/gtest.h>

#include "value_formats.hpp"

namespace nitwit::value {
namespace {

using namespace test;

template <std::size_t N>
std::array<std::byte, N> bytes(const std::array<unsigned, N>& values) {
    std::array<std::byte, N> result{};
    for (std::size_t i = 0; i < N; ++i) {
        result[i] = static_cast<std::byte>(values[i]);
    }
    return result;
}

TEST(ValueTest, DefaultValueIsVoid) {
    EXPECT_EQ(Value().kind(), Value::Kind::None);
}

TEST(ValueTest, KindsMatchTheConstructors) {
    EXPECT_EQ(Value::integer(1, s32).kind(), Value::Kind::Integer);
    EXPECT_EQ(Value::ofFloat(1.0F).kind(), Value::Kind::Float);
    EXPECT_EQ(Value::ofDouble(1.0).kind(), Value::Kind::Double);
    EXPECT_EQ(Value::ofLongDouble(1.0L).kind(), Value::Kind::LongDouble);
    EXPECT_EQ(Value::ofBytes({std::byte{1}, std::byte{2}}).kind(), Value::Kind::Bytes);
}

TEST(ValueTest, AccessorsReturnThePayload) {
    EXPECT_EQ(Value::ofFloat(1.5F).asFloat(), 1.5F);
    EXPECT_EQ(Value::ofDouble(-2.5).asDouble(), -2.5);
    EXPECT_EQ(Value::ofLongDouble(0x1.0000000000000002p0L).asLongDouble(), 0x1.0000000000000002p0L);
    const Value blob = Value::ofBytes({std::byte{7}, std::byte{8}, std::byte{9}});
    ASSERT_EQ(blob.asBytes().size(), 3u);
    EXPECT_EQ(blob.asBytes()[2], std::byte{9});
}

TEST(ValueTest, IntegerIsTruncatedToItsWidth) {
    EXPECT_EQ(Value::integer(0x1234, u8).bits(), 0x34u);
    EXPECT_EQ(Value::integer(0x1'2345'6789, u32).bits(), 0x2345'6789u);
    EXPECT_EQ(Value::integer(~UInt128{0}, u64).bits(), UInt128{0xFFFF'FFFF'FFFF'FFFF});
}

TEST(ValueTest, SignedIntegerIsSignExtended) {
    EXPECT_EQ(Value::integer(0xFF, s8).signedBits(), -1);
    EXPECT_EQ(Value::integer(0x80, s8).signedBits(), -128);
    EXPECT_EQ(Value::integer(0x7F, s8).signedBits(), 127);
    EXPECT_EQ(Value::integer(0x1'8000'0000, s32).signedBits(), Int128{-2147483648LL});
    EXPECT_EQ(Value::integer(0xFFFF'FFFF'FFFF'FFFF, s64).bits(), ~UInt128{0});
}

TEST(ValueTest, CanonicalFormForEveryFormat) {
    for (const IntegerFormat format : allFormats) {
        SCOPED_TRACE(formatName(format));
        const UInt128 topBit = UInt128{1} << (format.bits - 1);
        // The top bit of the format is the sign bit for signed formats and an ordinary bit for unsigned ones.
        EXPECT_EQ(Value::integer(topBit, format).bits(), format.isSigned ? minBits(format) : topBit);
        EXPECT_EQ(Value::integer(~UInt128{0}, format).bits(), format.isSigned ? ~UInt128{0} : maxBits(format));
        EXPECT_EQ(Value::integer(topBit - 1, format).bits(), topBit - 1);
        EXPECT_EQ(Value::integer(0, format).bits(), 0u);
    }
}

TEST(ValueTest, IntegersHaveNoOriginByDefault) {
    EXPECT_FALSE(Value::integer(5, s32).origin().has_value());
}

TEST(ValueTest, OriginCanBeSetAndCleared) {
    Value value = Value::integer(5, s32);
    value.setOrigin(InputId{3});
    EXPECT_EQ(value.origin(), InputId{3});
    const Value copy = value;
    EXPECT_EQ(copy.origin(), InputId{3});
    value.setOrigin(std::nullopt);
    EXPECT_FALSE(value.origin().has_value());
}

TEST(ValueTest, TermCanBeSetAndCleared) {
    Value value = Value::ofDouble(1.0);
    EXPECT_FALSE(value.term().has_value());
    value.setTerm(TermId{4});
    EXPECT_EQ(value.term(), TermId{4});
    const Value copy = value;
    EXPECT_EQ(copy.term(), TermId{4});
    value.setTerm(std::nullopt);
    EXPECT_FALSE(value.term().has_value());
}

TEST(ValueDeathTest, AccessorOfAnotherKindAsserts) {
    EXPECT_DEATH((void)Value::integer(1, s32).asDouble(), "another kind");
    EXPECT_DEATH((void)Value::ofFloat(1.0F).bits(), "another kind");
}

TEST(ValueDeathTest, UnsupportedWidthAsserts) {
    EXPECT_DEATH((void)Value::integer(1, IntegerFormat{.bits = 24, .isSigned = true}), "integer width");
}

// ---- Encoding ----

TEST(EncodingTest, IntegerIsLittleEndian) {
    std::array<std::byte, 8> out{};
    encodeInteger(Value::integer(0x0102'0304'0506'0708, u64), u64, out);
    EXPECT_EQ(out, bytes<8>({8, 7, 6, 5, 4, 3, 2, 1}));
}

TEST(EncodingTest, NegativeIntegerIsTwosComplement) {
    std::array<std::byte, 2> out{};
    encodeInteger(integer(-2, s16), s16, out);
    EXPECT_EQ(out, bytes<2>({0xFE, 0xFF}));
}

TEST(EncodingTest, DecodeExtendsBySignedness) {
    const auto encoded = bytes<2>({0x00, 0x80});
    EXPECT_EQ(decodeInteger(encoded, s16).signedBits(), -32768);
    EXPECT_EQ(decodeInteger(encoded, u16).bits(), 0x8000u);
}

TEST(EncodingTest, IntegerRoundTripsInEveryFormat) {
    for (const IntegerFormat format : allFormats) {
        SCOPED_TRACE(formatName(format));
        std::array<std::byte, 16> buffer{};
        const std::span<std::byte> out = std::span(buffer).first(format.bits / 8u);
        for (const UInt128 bits : {UInt128{0}, UInt128{1}, minBits(format), maxBits(format),
                                   static_cast<UInt128>(0x0123'4567'89AB'CDEF) * 0x1'0000'0001}) {
            const Value value = Value::integer(bits, format);
            encodeInteger(value, format, out);
            EXPECT_EQ(decodeInteger(out, format).bits(), value.bits());
        }
    }
}

TEST(EncodingTest, FloatAndDoubleUseTheIeeeRepresentation) {
    std::array<std::byte, 4> f{};
    encodeFloat(Value::ofFloat(1.0F), FloatFormat::Float, f);
    EXPECT_EQ(f, bytes<4>({0x00, 0x00, 0x80, 0x3F}));
    std::array<std::byte, 8> d{};
    encodeFloat(Value::ofDouble(-2.0), FloatFormat::Double, d);
    EXPECT_EQ(d, bytes<8>({0, 0, 0, 0, 0, 0, 0, 0xC0}));
    EXPECT_EQ(decodeFloat(d, FloatFormat::Double).asDouble(), -2.0);
}

TEST(EncodingTest, NanPayloadSurvivesTheRoundTrip) {
    const auto signalingNan = bytes<4>({0x01, 0x00, 0x80, 0xFF}); // negative signaling NaN with payload 1
    const Value value = decodeFloat(signalingNan, FloatFormat::Float);
    EXPECT_TRUE(std::isnan(value.asFloat()));
    std::array<std::byte, 4> out{};
    encodeFloat(value, FloatFormat::Float, out);
    EXPECT_EQ(out, signalingNan);
}

TEST(EncodingTest, NegativeZeroKeepsItsSign) {
    std::array<std::byte, 8> out{};
    encodeFloat(Value::ofDouble(-0.0), FloatFormat::Double, out);
    EXPECT_EQ(out, bytes<8>({0, 0, 0, 0, 0, 0, 0, 0x80}));
}

TEST(EncodingTest, LongDoubleTakesTenBytesAndZeroPaddingOnX86_64) {
    std::array<std::byte, 16> out{};
    out.fill(std::byte{0xAA});
    encodeFloat(Value::ofLongDouble(1.0L), FloatFormat::LongDouble, out);
    // Significand with explicit integer bit, then sign and exponent 0x3FFF; 6 bytes of padding.
    EXPECT_EQ(out, bytes<16>({0, 0, 0, 0, 0, 0, 0, 0x80, 0xFF, 0x3F, 0, 0, 0, 0, 0, 0}));
}

TEST(EncodingTest, LongDoubleTakesTwelveBytesOnI386) {
    std::array<std::byte, 12> out{};
    out.fill(std::byte{0xAA});
    encodeFloat(Value::ofLongDouble(-3.0L), FloatFormat::LongDouble, out);
    EXPECT_EQ(out, bytes<12>({0, 0, 0, 0, 0, 0, 0, 0xC0, 0x00, 0xC0, 0, 0}));
}

TEST(EncodingTest, LongDoubleDecodeIgnoresThePadding) {
    auto encoded = bytes<16>({0, 0, 0, 0, 0, 0, 0, 0x80, 0xFF, 0x3F, 1, 2, 3, 4, 5, 6});
    EXPECT_EQ(decodeFloat(encoded, FloatFormat::LongDouble).asLongDouble(), 1.0L);
    EXPECT_EQ(decodeFloat(std::span(encoded).first(12), FloatFormat::LongDouble).asLongDouble(), 1.0L);
}

TEST(EncodingTest, LongDoubleRoundTripsAtTheExtremes) {
    for (const long double value : {LDBL_MAX, -LDBL_MAX, LDBL_MIN, LDBL_TRUE_MIN, -0.0L,
                                    std::numeric_limits<long double>::infinity(), 0x1.0000000000000002p0L}) {
        std::array<std::byte, 16> out{};
        encodeFloat(Value::ofLongDouble(value), FloatFormat::LongDouble, out);
        const long double decoded = decodeFloat(out, FloatFormat::LongDouble).asLongDouble();
        EXPECT_EQ(std::memcmp(&decoded, &value, 10), 0) << static_cast<double>(value);
    }
}

TEST(EncodingDeathTest, WrongSizeAsserts) {
    std::array<std::byte, 4> out{};
    EXPECT_DEATH(encodeInteger(Value::integer(1, s64), s64, out), "wrong size");
    EXPECT_DEATH(encodeFloat(Value::ofLongDouble(1.0L), FloatFormat::LongDouble, out), "too small");
}

} // namespace
} // namespace nitwit::value
