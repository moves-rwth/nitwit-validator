#include <bit>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

#include "value/arithmetic.hpp"

namespace nitwit::value {
namespace {

// Expected values are IEEE 754 results with rounding to nearest, written as hexadecimal literals.

Value floatOp(FloatOperator op, float lhs, float rhs) {
    return floatBinary(op, Value::ofFloat(lhs), Value::ofFloat(rhs), FloatFormat::Float);
}

Value doubleOp(FloatOperator op, double lhs, double rhs) {
    return floatBinary(op, Value::ofDouble(lhs), Value::ofDouble(rhs), FloatFormat::Double);
}

Value longDoubleOp(FloatOperator op, long double lhs, long double rhs) {
    return floatBinary(op, Value::ofLongDouble(lhs), Value::ofLongDouble(rhs), FloatFormat::LongDouble);
}

TEST(FloatTest, FloatIsRoundedToSinglePrecision) {
    // Computed in double, 1/3 would differ; the target computes float in float (FLT_EVAL_METHOD 0).
    EXPECT_EQ(std::bit_cast<std::uint32_t>(floatOp(FloatOperator::Div, 1.0F, 3.0F).asFloat()), 0x3EAA'AAABu);
    EXPECT_EQ(floatOp(FloatOperator::Add, 0x1p24F, 1.0F).asFloat(), 0x1p24F);
    EXPECT_EQ(floatOp(FloatOperator::Mul, 0x1.000002p0F, 0x1.000002p0F).asFloat(), 0x1.000004p0F);
}

TEST(FloatTest, DoubleArithmetic) {
    EXPECT_EQ(doubleOp(FloatOperator::Add, 0.1, 0.2).asDouble(), 0x1.3333333333334p-2);
    EXPECT_EQ(doubleOp(FloatOperator::Sub, 1.0, 0x1p-53).asDouble(), 0x1.fffffffffffffp-1);
    EXPECT_EQ(doubleOp(FloatOperator::Div, 1.0, 3.0).asDouble(), 0x1.5555555555555p-2);
}

TEST(FloatTest, LongDoubleHasA64BitSignificand) {
    EXPECT_EQ(longDoubleOp(FloatOperator::Add, 1.0L, 0x1p-63L).asLongDouble(), 0x1.0000000000000002p0L);
    EXPECT_EQ(longDoubleOp(FloatOperator::Add, 1.0L, 0x1p-64L).asLongDouble(), 1.0L); // tie, rounds to even
    EXPECT_EQ(longDoubleOp(FloatOperator::Div, 1.0L, 3.0L).asLongDouble(), 0xa.aaaaaaaaaaaaaabp-5L);
}

TEST(FloatTest, DivisionByZeroGivesInfinityOrNan) {
    EXPECT_EQ(doubleOp(FloatOperator::Div, 1.0, 0.0).asDouble(), std::numeric_limits<double>::infinity());
    EXPECT_EQ(doubleOp(FloatOperator::Div, 1.0, -0.0).asDouble(), -std::numeric_limits<double>::infinity());
    EXPECT_TRUE(std::isnan(floatOp(FloatOperator::Div, 0.0F, 0.0F).asFloat()));
    EXPECT_TRUE(std::isnan(longDoubleOp(FloatOperator::Div, 0.0L, 0.0L).asLongDouble()));
}

TEST(FloatTest, OverflowAndUnderflow) {
    EXPECT_EQ(floatOp(FloatOperator::Mul, FLT_MAX, 2.0F).asFloat(), std::numeric_limits<float>::infinity());
    EXPECT_EQ(doubleOp(FloatOperator::Div, DBL_MIN, 0x1p52).asDouble(), 0x1p-1074); // the smallest subnormal
    EXPECT_EQ(doubleOp(FloatOperator::Div, 0x1p-1074, 2.0).asDouble(), 0.0);        // tie, rounds to even
    EXPECT_TRUE(std::isnan(doubleOp(FloatOperator::Sub, HUGE_VAL, HUGE_VAL).asDouble()));
}

TEST(FloatTest, NegateFlipsTheSignBit) {
    EXPECT_EQ(std::bit_cast<std::uint32_t>(floatNegate(Value::ofFloat(0.0F), FloatFormat::Float).asFloat()),
              0x8000'0000u);
    EXPECT_EQ(floatNegate(Value::ofDouble(-2.5), FloatFormat::Double).asDouble(), 2.5);
    EXPECT_EQ(floatNegate(Value::ofLongDouble(1.0L), FloatFormat::LongDouble).asLongDouble(), -1.0L);
    const double negatedNan =
        floatNegate(Value::ofDouble(std::numeric_limits<double>::quiet_NaN()), FloatFormat::Double).asDouble();
    EXPECT_TRUE(std::isnan(negatedNan));
    EXPECT_TRUE(std::signbit(negatedNan));
}

TEST(FloatTest, ComparisonsWithNanAreFalseExceptNotEqual) {
    const Value nan = Value::ofDouble(std::numeric_limits<double>::quiet_NaN());
    const Value one = Value::ofDouble(1.0);
    for (const Comparison comparison :
         {Comparison::Equal, Comparison::Less, Comparison::LessEqual, Comparison::Greater, Comparison::GreaterEqual}) {
        EXPECT_FALSE(floatCompare(comparison, nan, one, FloatFormat::Double));
        EXPECT_FALSE(floatCompare(comparison, nan, nan, FloatFormat::Double));
    }
    EXPECT_TRUE(floatCompare(Comparison::NotEqual, nan, nan, FloatFormat::Double));
    EXPECT_TRUE(floatCompare(Comparison::NotEqual, one, nan, FloatFormat::Double));
}

TEST(FloatTest, ComparisonsOfZerosAndInfinities) {
    EXPECT_TRUE(floatCompare(Comparison::Equal, Value::ofFloat(-0.0F), Value::ofFloat(0.0F), FloatFormat::Float));
    EXPECT_FALSE(floatCompare(Comparison::Less, Value::ofFloat(-0.0F), Value::ofFloat(0.0F), FloatFormat::Float));
    EXPECT_TRUE(floatCompare(Comparison::Greater, Value::ofLongDouble(HUGE_VALL), Value::ofLongDouble(LDBL_MAX),
                             FloatFormat::LongDouble));
    EXPECT_TRUE(floatCompare(Comparison::LessEqual, Value::ofDouble(-HUGE_VAL), Value::ofDouble(-DBL_MAX),
                             FloatFormat::Double));
    EXPECT_TRUE(
        floatCompare(Comparison::GreaterEqual, Value::ofDouble(2.0), Value::ofDouble(2.0), FloatFormat::Double));
}

} // namespace
} // namespace nitwit::value
