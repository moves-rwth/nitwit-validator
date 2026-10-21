#include <gtest/gtest.h>

#include "value/arithmetic.hpp"
#include "value_formats.hpp"

namespace nitwit::value {
namespace {

using namespace test;

// Expected values follow C17 6.5 for defined results. Where a signed operation overflows, the expected value is the
// wrapped two's-complement result, which GCC-compiled code gives on x86 (confirmed with gcc-14 -m64 and gcc-13 -m32,
// also with -fwrapv).

constexpr auto none = UndefinedBehavior::None;
constexpr auto overflow = UndefinedBehavior::SignedOverflow;

Result binaryBits(IntegerOperator op, UInt128 lhs, UInt128 rhs, IntegerFormat format) {
    return integerBinary(op, Value::integer(lhs, format), Value::integer(rhs, format), format);
}

Result binary(IntegerOperator op, Int128 lhs, Int128 rhs, IntegerFormat format) {
    return integerBinary(op, integer(lhs, format), integer(rhs, format), format);
}

/// Expects the result's value (in canonical form for `format`) and undefined behaviour.
void expectResult(const Result& result, UInt128 bits, IntegerFormat format, UndefinedBehavior undefinedBehavior) {
    EXPECT_EQ(result.value.bits(), Value::integer(bits, format).bits());
    EXPECT_EQ(result.undefinedBehavior, undefinedBehavior);
}

std::string testName(const testing::TestParamInfo<IntegerFormat>& format) {
    return formatName(format.param);
}

class IntegerBoundaryTest : public testing::TestWithParam<IntegerFormat> {};

INSTANTIATE_TEST_SUITE_P(AllFormats, IntegerBoundaryTest, testing::ValuesIn(allFormats), testName);

TEST_P(IntegerBoundaryTest, AddWrapsAtTheMaximum) {
    const IntegerFormat f = GetParam();
    expectResult(binaryBits(IntegerOperator::Add, maxBits(f), UInt128{1}, f), minBits(f), f,
                 f.isSigned ? overflow : none);
    expectResult(binaryBits(IntegerOperator::Add, maxBits(f), UInt128{0}, f), maxBits(f), f, none);
    expectResult(binaryBits(IntegerOperator::Add, UInt128{0}, UInt128{0}, f), 0, f, none);
}

TEST_P(IntegerBoundaryTest, AddOfMinusOneAndOne) {
    const IntegerFormat f = GetParam();
    // For unsigned formats -1 is the maximum, and the sum wraps without undefined behaviour (C17 6.2.5p9).
    expectResult(binary(IntegerOperator::Add, -1, 1, f), 0, f, none);
}

TEST_P(IntegerBoundaryTest, AddBelowTheMinimum) {
    const IntegerFormat f = GetParam();
    if (f.isSigned) {
        expectResult(binaryBits(IntegerOperator::Add, minBits(f), ~UInt128{0}, f), maxBits(f), f, overflow);
        expectResult(binaryBits(IntegerOperator::Add, minBits(f), maxBits(f), f), ~UInt128{0}, f, none);
    } else {
        expectResult(binaryBits(IntegerOperator::Add, maxBits(f), maxBits(f), f), maxBits(f) - 1, f, none);
    }
}

TEST_P(IntegerBoundaryTest, SubWrapsAtTheMinimum) {
    const IntegerFormat f = GetParam();
    expectResult(binaryBits(IntegerOperator::Sub, minBits(f), UInt128{1}, f), maxBits(f), f,
                 f.isSigned ? overflow : none);
    expectResult(binaryBits(IntegerOperator::Sub, maxBits(f), maxBits(f), f), 0, f, none);
}

TEST_P(IntegerBoundaryTest, SubOfTheMinimumFromZero) {
    const IntegerFormat f = GetParam();
    if (f.isSigned) {
        expectResult(binaryBits(IntegerOperator::Sub, UInt128{0}, minBits(f), f), minBits(f), f, overflow);
        expectResult(binaryBits(IntegerOperator::Sub, ~UInt128{0}, maxBits(f), f), minBits(f), f, none);
        expectResult(binaryBits(IntegerOperator::Sub, maxBits(f), ~UInt128{0}, f), minBits(f), f, overflow);
    } else {
        expectResult(binaryBits(IntegerOperator::Sub, UInt128{0}, UInt128{1}, f), maxBits(f), f, none);
    }
}

TEST_P(IntegerBoundaryTest, MulOfTheMaximumByTwo) {
    const IntegerFormat f = GetParam();
    expectResult(binaryBits(IntegerOperator::Mul, maxBits(f), UInt128{2}, f), maxBits(f) * 2, f,
                 f.isSigned ? overflow : none);
    expectResult(binaryBits(IntegerOperator::Mul, maxBits(f), UInt128{1}, f), maxBits(f), f, none);
    expectResult(binaryBits(IntegerOperator::Mul, maxBits(f), UInt128{0}, f), 0, f, none);
}

class SignedBoundaryTest : public testing::TestWithParam<IntegerFormat> {};

INSTANTIATE_TEST_SUITE_P(SignedFormats, SignedBoundaryTest, testing::Values(s8, s16, s32, s64, s128), testName);

TEST_P(SignedBoundaryTest, MulReachesTheMinimumButNotItsNegation) {
    const IntegerFormat f = GetParam();
    const Int128 quarter = Int128{1} << (f.bits - 2);
    // -2^(N-2) * 2 is the minimum and fits; 2^(N-2) * 2 does not.
    expectResult(binary(IntegerOperator::Mul, -quarter, 2, f), minBits(f), f, none);
    expectResult(binary(IntegerOperator::Mul, quarter, -2, f), minBits(f), f, none);
    expectResult(binary(IntegerOperator::Mul, quarter, 2, f), minBits(f), f, overflow);
    expectResult(binary(IntegerOperator::Mul, -quarter, -2, f), minBits(f), f, overflow);
    expectResult(binaryBits(IntegerOperator::Mul, minBits(f), ~UInt128{0}, f), minBits(f), f, overflow);
    expectResult(binaryBits(IntegerOperator::Mul, minBits(f), UInt128{1}, f), minBits(f), f, none);
    expectResult(binary(IntegerOperator::Mul, -1, -1, f), 1, f, none);
    expectResult(binaryBits(IntegerOperator::Mul, maxBits(f), ~UInt128{0}, f), minBits(f) + 1, f, none);
}

TEST_P(IntegerBoundaryTest, DivisionByZeroGivesZero) {
    const IntegerFormat f = GetParam();
    // The value is NITWIT's choice: x86's div and idiv trap.
    expectResult(binaryBits(IntegerOperator::Div, maxBits(f), UInt128{0}, f), 0, f, UndefinedBehavior::DivisionByZero);
    expectResult(binaryBits(IntegerOperator::Rem, maxBits(f), UInt128{0}, f), 0, f, UndefinedBehavior::DivisionByZero);
    expectResult(binaryBits(IntegerOperator::Div, UInt128{0}, UInt128{0}, f), 0, f, UndefinedBehavior::DivisionByZero);
}

TEST_P(IntegerBoundaryTest, DivisionOfTheMinimumByMinusOne) {
    const IntegerFormat f = GetParam();
    if (f.isSigned) {
        // idiv traps (SIGFPE, also with -fwrapv); the values are those of wrapping arithmetic.
        expectResult(binaryBits(IntegerOperator::Div, minBits(f), ~UInt128{0}, f), minBits(f), f, overflow);
        expectResult(binaryBits(IntegerOperator::Rem, minBits(f), ~UInt128{0}, f), 0, f, overflow);
    } else {
        expectResult(binaryBits(IntegerOperator::Div, minBits(f), ~UInt128{0}, f), 0, f, none);
        expectResult(binaryBits(IntegerOperator::Div, maxBits(f), maxBits(f), f), 1, f, none);
    }
}

TEST_P(IntegerBoundaryTest, DivisionAtTheMaximum) {
    const IntegerFormat f = GetParam();
    expectResult(binaryBits(IntegerOperator::Div, maxBits(f), UInt128{1}, f), maxBits(f), f, none);
    expectResult(binaryBits(IntegerOperator::Div, maxBits(f), UInt128{2}, f), maxBits(f) >> 1, f, none);
    expectResult(binaryBits(IntegerOperator::Rem, maxBits(f), UInt128{2}, f), 1, f, none);
}

TEST_P(IntegerBoundaryTest, NegateOfTheMinimum) {
    const IntegerFormat f = GetParam();
    const Result result = integerNegate(Value::integer(minBits(f), f), f);
    expectResult(result, minBits(f), f, f.isSigned ? overflow : none);
    expectResult(integerNegate(Value::integer(1, f), f), ~UInt128{0}, f, none);
    expectResult(integerNegate(Value::integer(maxBits(f), f), f), UInt128{0} - maxBits(f), f, none);
    expectResult(integerNegate(Value::integer(0, f), f), 0, f, none);
}

TEST_P(IntegerBoundaryTest, ComplementFlipsEveryBit) {
    const IntegerFormat f = GetParam();
    EXPECT_EQ(integerComplement(Value::integer(0, f), f).bits(), Value::integer(~UInt128{0}, f).bits());
    EXPECT_EQ(integerComplement(Value::integer(maxBits(f), f), f).bits(), minBits(f));
}

TEST_P(IntegerBoundaryTest, BitwiseOperatorsAtTheBoundaries) {
    const IntegerFormat f = GetParam();
    const UInt128 allOnes = ~UInt128{0}; // -1 for signed formats, the maximum for unsigned ones
    expectResult(binaryBits(IntegerOperator::And, minBits(f), maxBits(f), f), 0, f, none);
    expectResult(binaryBits(IntegerOperator::Or, minBits(f), maxBits(f), f), allOnes, f, none);
    expectResult(binaryBits(IntegerOperator::Xor, minBits(f), maxBits(f), f), allOnes, f, none);
    expectResult(binaryBits(IntegerOperator::And, allOnes, maxBits(f), f), maxBits(f), f, none);
    expectResult(binaryBits(IntegerOperator::Or, maxBits(f), UInt128{0}, f), maxBits(f), f, none);
    expectResult(binaryBits(IntegerOperator::Xor, allOnes, maxBits(f), f), minBits(f), f, none);
    expectResult(binaryBits(IntegerOperator::Xor, maxBits(f), maxBits(f), f), 0, f, none);
    expectResult(binaryBits(IntegerOperator::And, UInt128{1}, allOnes, f), 1, f, none);
}

TEST_P(IntegerBoundaryTest, ComparisonOrdersMinimumBelowMaximum) {
    const IntegerFormat f = GetParam();
    const Value minimum = Value::integer(minBits(f), f);
    const Value maximum = Value::integer(maxBits(f), f);
    const Value minusOne = integer(-1, f);
    EXPECT_TRUE(integerCompare(Comparison::Less, minimum, maximum, f));
    EXPECT_TRUE(integerCompare(Comparison::LessEqual, minimum, minimum, f));
    EXPECT_FALSE(integerCompare(Comparison::Greater, minimum, maximum, f));
    EXPECT_TRUE(integerCompare(Comparison::GreaterEqual, maximum, minusOne, f));
    EXPECT_TRUE(integerCompare(Comparison::Equal, maximum, maximum, f));
    EXPECT_TRUE(integerCompare(Comparison::NotEqual, minimum, maximum, f));
    // -1 is below 0 for signed formats; for unsigned formats it is the maximum.
    EXPECT_EQ(integerCompare(Comparison::Less, minusOne, Value::integer(0, f), f), f.isSigned);
}

TEST(IntegerTest, DivisionTruncatesTowardZero) {
    // C17 6.5.5p6: (a/b)*b + a%b == a.
    expectResult(binary(IntegerOperator::Div, -7, 2, s32), static_cast<UInt128>(-3), s32, none);
    expectResult(binary(IntegerOperator::Rem, -7, 2, s32), static_cast<UInt128>(-1), s32, none);
    expectResult(binary(IntegerOperator::Div, 7, -2, s32), static_cast<UInt128>(-3), s32, none);
    expectResult(binary(IntegerOperator::Rem, 7, -2, s32), 1, s32, none);
    expectResult(binary(IntegerOperator::Div, -7, -2, s64), 3, s64, none);
    expectResult(binary(IntegerOperator::Rem, -7, -2, s128), static_cast<UInt128>(-1), s128, none);
}

TEST(IntegerTest, UnsignedDivisionUsesTheWholeRange) {
    expectResult(binaryBits(IntegerOperator::Div, UInt128{0xFFFF'FFFF}, UInt128{0x8000'0000}, u32), 1, u32, none);
    expectResult(binaryBits(IntegerOperator::Rem, UInt128{0xFFFF'FFFF}, UInt128{0x8000'0000}, u32), 0x7FFF'FFFF, u32,
                 none);
}

TEST(IntegerTest, BitwiseOperators) {
    expectResult(binaryBits(IntegerOperator::And, UInt128{0xF0F0}, UInt128{0xFF00}, u16), 0xF000, u16, none);
    expectResult(binaryBits(IntegerOperator::Or, UInt128{0xF0F0}, UInt128{0x0F00}, u16), 0xFFF0, u16, none);
    expectResult(binaryBits(IntegerOperator::Xor, UInt128{0xF0F0}, UInt128{0xFFFF}, u16), 0x0F0F, u16, none);
    // On signed operands the bitwise operators act on the two's-complement representation.
    expectResult(binary(IntegerOperator::And, -1, 0x7F, s8), 0x7F, s8, none);
    expectResult(binary(IntegerOperator::Xor, -1, 0x7F, s8), 0x80, s8, none);
}

TEST(IntegerTest, MultiplicationKeepsTheLowBits) {
    // 65536 * 65536 overflows int; gcc -fwrapv gives 0.
    expectResult(binary(IntegerOperator::Mul, 65536, 65536, s32), 0, s32, overflow);
    expectResult(binary(IntegerOperator::Mul, 65536, 65536, u32), 0, u32, none);
    expectResult(binary(IntegerOperator::Mul, 0x1'0000'0001, 0x1'0000'0001, u64), 0x2'0000'0001, u64, none);
}

TEST(IntegerDeathTest, OperandOfAnotherFormatAsserts) {
    // 0xFFFFFFFF is not a canonical int: sema must convert it explicitly.
    EXPECT_DEATH(
        (void)integerBinary(IntegerOperator::Add, Value::integer(0xFFFF'FFFF, u32), Value::integer(1, s32), s32),
        "expected format");
}

// ---- Shifts ----
//
// Out-of-range counts give what x86 gives for a variable count: the count is masked to 5 bits (operands of up to 32
// bits), 6 bits (64-bit operands) or 7 bits (GCC's code for 128-bit operands). Confirmed with gcc-14 -m64 and gcc-13
// -m32; these values are hardware behaviour, not defined C.

Result shiftBy(ShiftOperator op, Int128 value, IntegerFormat format, Int128 count, IntegerFormat countFormat = s32) {
    return shift(op, integer(value, format), format, integer(count, countFormat), countFormat);
}

constexpr auto left = ShiftOperator::Left;
constexpr auto right = ShiftOperator::Right;
constexpr auto outOfRange = UndefinedBehavior::ShiftCountOutOfRange;

class ShiftTest : public testing::TestWithParam<IntegerFormat> {};

INSTANTIATE_TEST_SUITE_P(PromotedFormats, ShiftTest, testing::Values(s32, u32, s64, u64, s128, u128), testName);

TEST_P(ShiftTest, ShiftByZeroKeepsTheValue) {
    const IntegerFormat f = GetParam();
    expectResult(shiftBy(left, 5, f, 0), 5, f, none);
    expectResult(shiftBy(right, 5, f, 0), 5, f, none);
}

TEST_P(ShiftTest, ShiftIntoTheTopBit) {
    const IntegerFormat f = GetParam();
    const int top = f.bits - 1;
    // C17 6.5.7p4: 1 << (N-1) is not representable in a signed type.
    expectResult(shiftBy(left, 1, f, top), UInt128{1} << top, f, f.isSigned ? UndefinedBehavior::ShiftOverflow : none);
    expectResult(shiftBy(left, 1, f, top - 1), UInt128{1} << (top - 1), f, none);
    expectResult(shiftBy(left, 3, f, top - 1), UInt128{3} << (top - 1), f,
                 f.isSigned ? UndefinedBehavior::ShiftOverflow : none);
    expectResult(shiftBy(right, 1, f, top), 0, f, none);
}

TEST_P(ShiftTest, CountOfTheWidthIsMasked) {
    const IntegerFormat f = GetParam();
    expectResult(shiftBy(left, 1, f, f.bits), 1, f, outOfRange);
    expectResult(shiftBy(left, 1, f, f.bits + 1), 2, f, outOfRange);
    expectResult(shiftBy(right, 1, f, f.bits), 1, f, outOfRange);
    expectResult(shiftBy(right, 1, f, f.bits + 1), 0, f, outOfRange);
}

TEST_P(ShiftTest, NegativeCountIsMasked) {
    const IntegerFormat f = GetParam();
    expectResult(shiftBy(left, 1, f, -1), UInt128{1} << (f.bits - 1), f, outOfRange);
    expectResult(shiftBy(right, 1, f, -1), 0, f, outOfRange);
}

TEST_P(ShiftTest, RightShiftOfANegativeValue) {
    const IntegerFormat f = GetParam();
    // C17 6.5.7p5: implementation-defined; GCC shifts signed values arithmetically.
    expectResult(shiftBy(right, -8, f, 1), f.isSigned ? static_cast<UInt128>(-4) : (maxBits(f) - 7) >> 1, f, none);
}

TEST(ShiftTest, LeftShiftOfANegativeValue) {
    expectResult(shiftBy(left, -8, s32, 1), static_cast<UInt128>(-16), s32, UndefinedBehavior::ShiftOfNegative);
    expectResult(shiftBy(left, -1, s64, 0), static_cast<UInt128>(-1), s64, UndefinedBehavior::ShiftOfNegative);
    expectResult(shiftBy(left, -8, u32, 1), 0xFFFF'FFF0, u32, none);
}

TEST(ShiftTest, CountOutOfRangeTakesPrecedence) {
    // -8 << 33 on int: gcc gives -16 (count masked to 1).
    expectResult(shiftBy(left, -8, s32, 33), static_cast<UInt128>(-16), s32, outOfRange);
    expectResult(shiftBy(right, -8, s32, 33), static_cast<UInt128>(-4), s32, outOfRange);
}

TEST(ShiftTest, CountOfAnotherFormatUsesItsLowBits) {
    // int << long long and int << __int128: gcc gives 2 and 8.
    expectResult(shiftBy(left, 1, s32, 0x1'0000'0001, s64), 2, s32, outOfRange);
    expectResult(shiftBy(left, 1, s32, -31, s64), 2, s32, outOfRange);
    expectResult(shiftBy(left, 1, s32, (Int128{1} << 64) | 3, s128), 8, s32, outOfRange);
    expectResult(shiftBy(left, 1, s32, 3, u8), 8, s32, none);
}

TEST(ShiftTest, Int128CountsAreMaskedToSevenBits) {
    // -2 << 200 and -2 >> 200 on __int128 (count masked to 72), as gcc-14 -m64 computes them.
    const UInt128 high = 0xFFFF'FFFF'FFFF'FE00;
    expectResult(shiftBy(left, -2, s128, 200), high << 64, s128, outOfRange);
    expectResult(shiftBy(right, -2, s128, 200), ~UInt128{0}, s128, outOfRange);
    expectResult(shiftBy(right, -2, u128, 200), 0x00FF'FFFF'FFFF'FFFF, u128, outOfRange);
}

TEST(ShiftTest, NarrowOperandsShiftOutEverything) {
    // Not reachable from C, where operands are promoted, but defined like x86's 8- and 16-bit shifts.
    expectResult(shiftBy(left, 1, u8, 9), 0, u8, outOfRange);
    expectResult(shiftBy(right, -1, s16, 20), static_cast<UInt128>(-1), s16, outOfRange);
    expectResult(shiftBy(left, 1, s8, 6), 64, s8, none);
}

} // namespace
} // namespace nitwit::value
