#include "value/term.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <format>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "value/arithmetic.hpp"
#include "value_formats.hpp"

namespace nitwit::value {
namespace {

using namespace test;

// Most tests record an operation on inputs and check that the term evaluates exactly like the arithmetic.hpp function
// applied directly, once with the values of the run and once with other input values.

constexpr InputId firstInput{0};
constexpr InputId secondInput{1};
constexpr InputId thirdInput{2};

constexpr auto lp64 = DataModel::LP64;
constexpr auto ilp32 = DataModel::ILP32;

constexpr std::array integerOperators{IntegerOperator::Add, IntegerOperator::Sub, IntegerOperator::Mul,
                                      IntegerOperator::Div, IntegerOperator::Rem, IntegerOperator::And,
                                      IntegerOperator::Or,  IntegerOperator::Xor};
constexpr std::array floatOperators{FloatOperator::Add, FloatOperator::Sub, FloatOperator::Mul, FloatOperator::Div};
constexpr std::array comparisons{Comparison::Equal,     Comparison::NotEqual, Comparison::Less,
                                 Comparison::LessEqual, Comparison::Greater,  Comparison::GreaterEqual};
constexpr std::array floatFormats{FloatFormat::Float, FloatFormat::Double, FloatFormat::LongDouble};

const double nan = std::numeric_limits<double>::quiet_NaN();
const long double longDoubleNan = std::numeric_limits<long double>::quiet_NaN(); // for floating()

/// A floating value of `format`.
Value floating(long double value, FloatFormat format) {
    switch (format) {
    case FloatFormat::Float:
        return Value::ofFloat(static_cast<float>(value));
    case FloatFormat::Double:
        return Value::ofDouble(static_cast<double>(value));
    case FloatFormat::LongDouble:
        return Value::ofLongDouble(value);
    }
    return {};
}

/// The value of a comparison, `!`, `&&` or `||`: an int.
Value truth(bool holds) {
    return Value::integer(holds ? 1u : 0u, s32);
}

/// The representation of a scalar, to compare values bit for bit (NaN, -0.0).
UInt128 representation(const Value& value) {
    switch (value.kind()) {
    case Value::Kind::Integer:
        return value.bits();
    case Value::Kind::Float:
        return std::bit_cast<std::uint32_t>(value.asFloat());
    case Value::Kind::Double:
        return std::bit_cast<std::uint64_t>(value.asDouble());
    case Value::Kind::LongDouble: {
        std::array<std::byte, 16> bytes{};
        encodeFloat(value, FloatFormat::LongDouble, bytes);
        return decodeInteger(bytes, u128).bits();
    }
    case Value::Kind::None:
    case Value::Kind::Bytes:
        break;
    }
    ADD_FAILURE() << "not a scalar";
    return 0;
}

std::string describe(const Value& value) {
    const UInt128 bits = representation(value);
    return std::format("value of kind {} with representation {:#x}:{:016x}", static_cast<unsigned>(value.kind()),
                       static_cast<std::uint64_t>(bits >> 64), static_cast<std::uint64_t>(bits));
}

/// Whether an evaluation gave `expected`'s value, bit for bit, or no value if `expected` has undefined behaviour.
::testing::AssertionResult evaluatesTo(const std::optional<Value>& actual, const Result& expected) {
    if (expected.undefinedBehavior != UndefinedBehavior::None) {
        if (actual) {
            return ::testing::AssertionFailure() << describe(*actual) << " instead of none";
        }
        return ::testing::AssertionSuccess();
    }
    if (!actual) {
        return ::testing::AssertionFailure() << "no value instead of " << describe(expected.value);
    }
    if (actual->kind() != expected.value.kind() || representation(*actual) != representation(expected.value)) {
        return ::testing::AssertionFailure() << describe(*actual) << " instead of " << describe(expected.value);
    }
    if (actual->term() || actual->origin()) {
        return ::testing::AssertionFailure() << "the value has a term or an origin";
    }
    return ::testing::AssertionSuccess();
}

::testing::AssertionResult evaluatesTo(const std::optional<Value>& actual, const Value& expected) {
    return evaluatesTo(actual, Result{expected});
}

Substitution replacing(InputId input, const Value& value) {
    Substitution substitution;
    substitution.set(input, value);
    return substitution;
}

/// `value` as the run's value of `input`, with its term.
Value registered(TermTable& table, InputId input, Value value, ScalarFormat format) {
    const std::optional<TermId> term = table.input(input, value, format);
    EXPECT_TRUE(term.has_value());
    value.setTerm(term);
    return value;
}

/// `value` with the term of an operation; the value itself does not matter to the table.
Value withTerm(std::optional<TermId> term, Value value = Value::integer(0, s32)) {
    EXPECT_TRUE(term.has_value());
    value.setTerm(term);
    return value;
}

using UnaryRecord = std::function<std::optional<TermId>(TermTable&, const Value&)>;
using UnaryApply = std::function<Result(const Value&)>;
using BinaryRecord = std::function<std::optional<TermId>(TermTable&, const Value&, const Value&)>;
using BinaryApply = std::function<Result(const Value&, const Value&)>;

/// Records an operation on an input whose value in the run is `value`, and checks that the term evaluates like `apply`
/// with that value and with `other` substituted.
void checkUnary(const UnaryRecord& record, const UnaryApply& apply, ScalarFormat format, const Value& value,
                const Value& other, DataModel model = lp64) {
    TermTable table(model);
    const std::optional<TermId> term = record(table, registered(table, firstInput, value, format));
    ASSERT_TRUE(term.has_value());
    EXPECT_TRUE(evaluatesTo(table.evaluate(*term, {}), apply(value)));
    EXPECT_TRUE(evaluatesTo(table.evaluate(*term, replacing(firstInput, other)), apply(other)));
}

/// Records `lhs op rhs` with an input as the left, the right or both operands (the others become constants), and checks
/// that the term evaluates like `apply` with the values of the run and with the inputs replaced by `otherLhs` and
/// `otherRhs`.
void checkBinary(const BinaryRecord& record, const BinaryApply& apply, ScalarFormat lhsFormat, const Value& lhs,
                 const Value& otherLhs, ScalarFormat rhsFormat, const Value& rhs, const Value& otherRhs) {
    for (const auto& [lhsIsInput, rhsIsInput] :
         {std::pair{true, false}, std::pair{false, true}, std::pair{true, true}}) {
        SCOPED_TRACE(std::format("input on the left: {}, on the right: {}", lhsIsInput, rhsIsInput));
        TermTable table(lp64);
        const Value left = lhsIsInput ? registered(table, firstInput, lhs, lhsFormat) : lhs;
        const Value right = rhsIsInput ? registered(table, secondInput, rhs, rhsFormat) : rhs;
        const std::optional<TermId> term = record(table, left, right);
        ASSERT_TRUE(term.has_value());
        EXPECT_TRUE(evaluatesTo(table.evaluate(*term, {}), apply(lhs, rhs)));
        Substitution substitution;
        substitution.set(firstInput, otherLhs);
        substitution.set(secondInput, otherRhs);
        EXPECT_TRUE(evaluatesTo(table.evaluate(*term, substitution),
                                apply(lhsIsInput ? otherLhs : lhs, rhsIsInput ? otherRhs : rhs)));
    }
}

// ---- Substitution ----

TEST(SubstitutionTest, SetReplacesTheValueOfAnInput) {
    Substitution substitution;
    EXPECT_TRUE(substitution.empty());
    substitution.set(firstInput, integer(1, s32));
    substitution.set(secondInput, integer(2, s32));
    substitution.set(firstInput, integer(3, s32));
    ASSERT_NE(substitution.find(firstInput), nullptr);
    EXPECT_EQ(substitution.find(firstInput)->bits(), 3u);
    EXPECT_EQ(substitution.find(secondInput)->bits(), 2u);
    EXPECT_EQ(substitution.find(thirdInput), nullptr);
    EXPECT_EQ(substitution.entries().size(), 2u);
    EXPECT_FALSE(substitution.empty());
}

// ---- Inputs and constants ----

TEST(TermTableTest, InputTermRecordsTheInput) {
    TermTable table(lp64);
    const std::optional<TermId> id = table.input(secondInput, integer(-5, s16), s16);
    ASSERT_TRUE(id.has_value());
    const Term& term = table.term(*id);
    EXPECT_EQ(term.kind, TermKind::Input);
    EXPECT_EQ(table.inputOf(term), secondInput);
    EXPECT_EQ(term.format, ScalarFormat{s16});
    EXPECT_EQ(term.depth, 0u);
    EXPECT_EQ(term.operandCount, 0u);
    EXPECT_EQ(table.size(), 1u);
}

TEST(TermTableTest, InputEvaluatesToTheRunValueOrTheSubstitutedOne) {
    TermTable table(lp64);
    Value runValue = Value::ofDouble(2.5);
    runValue.setOrigin(firstInput);
    const Value x = registered(table, firstInput, runValue, FloatFormat::Double);
    EXPECT_TRUE(evaluatesTo(table.evaluate(*x.term(), {}), Value::ofDouble(2.5)));
    EXPECT_TRUE(
        evaluatesTo(table.evaluate(*x.term(), replacing(firstInput, Value::ofDouble(-1.0))), Value::ofDouble(-1.0)));
}

TEST(TermTableTest, InputsNotSubstitutedKeepTheirRunValue) {
    TermTable table(lp64);
    const Value x = registered(table, firstInput, integer(10, s32), s32);
    const Value y = registered(table, secondInput, integer(3, s32), s32);
    const std::optional<TermId> difference = table.integerBinary(IntegerOperator::Sub, x, y, s32);
    ASSERT_TRUE(difference.has_value());
    EXPECT_TRUE(evaluatesTo(table.evaluate(*difference, replacing(secondInput, integer(4, s32))), integer(6, s32)));
    EXPECT_TRUE(evaluatesTo(table.evaluate(*difference, replacing(firstInput, integer(4, s32))), integer(1, s32)));
}

TEST(TermTableTest, OperandWithoutTermBecomesAConstant) {
    TermTable table(lp64);
    const Value x = registered(table, firstInput, integer(10, s32), s32);
    Value five = integer(5, s32);
    five.setOrigin(secondInput); // an origin without a term: the constant does not keep it
    const std::optional<TermId> sum = table.integerBinary(IntegerOperator::Add, five, x, s32);
    ASSERT_TRUE(sum.has_value());
    const Term& term = table.term(*sum);
    ASSERT_EQ(term.operandCount, 2u);
    EXPECT_EQ(term.operands[1], *x.term());
    const Term& constant = table.term(term.operands[0]);
    EXPECT_EQ(constant.kind, TermKind::Constant);
    EXPECT_EQ(constant.depth, 0u);
    EXPECT_EQ(table.constantOf(constant).bits(), 5u);
    EXPECT_FALSE(table.constantOf(constant).origin().has_value());
    EXPECT_TRUE(evaluatesTo(table.evaluate(term.operands[0], replacing(firstInput, integer(1, s32))), integer(5, s32)));
}

TEST(TermTableTest, OperationsWithoutAnOperandWithTermRecordNothing) {
    TermTable table(lp64);
    const Value a = integer(6, s32);
    const Value b = Value::ofDouble(1.5);
    EXPECT_FALSE(table.integerBinary(IntegerOperator::Add, a, a, s32));
    EXPECT_FALSE(table.shift(ShiftOperator::Left, a, s32, a, s32));
    EXPECT_FALSE(table.integerNegate(a, s32));
    EXPECT_FALSE(table.integerComplement(a, s32));
    EXPECT_FALSE(table.integerCompare(Comparison::Less, a, a, s32));
    EXPECT_FALSE(table.floatBinary(FloatOperator::Mul, b, b, FloatFormat::Double));
    EXPECT_FALSE(table.floatNegate(b, FloatFormat::Double));
    EXPECT_FALSE(table.floatCompare(Comparison::Equal, b, b, FloatFormat::Double));
    EXPECT_FALSE(table.convertInteger(a, s32, u8));
    EXPECT_FALSE(table.toBool(b, FloatFormat::Double));
    EXPECT_FALSE(table.integerToFloat(a, s32, FloatFormat::Float));
    EXPECT_FALSE(table.floatToInteger(b, FloatFormat::Double, s64));
    EXPECT_FALSE(table.convertFloat(b, FloatFormat::Double, FloatFormat::LongDouble));
    EXPECT_FALSE(table.logicalNot(a, s32));
    EXPECT_FALSE(table.logicalAnd(a, s32, &a, s32));
    EXPECT_FALSE(table.logicalAnd(a, s32, nullptr, s32));
    EXPECT_FALSE(table.logicalOr(b, FloatFormat::Double, &a, s32));
    EXPECT_FALSE(table.logicalOr(a, s32, nullptr, s32));
    EXPECT_FALSE(table.conditional(a, s32, true, b));
    EXPECT_EQ(table.size(), 0u);
}

TEST(TermTableTest, TermRecordsTheOperationAndItsFormats) {
    TermTable table(lp64);
    const Value x = registered(table, firstInput, integer(1, u64), u64);
    const std::optional<TermId> shifted = table.shift(ShiftOperator::Right, x, u64, integer(3, s32), s32);
    ASSERT_TRUE(shifted.has_value());
    const Term& term = table.term(*shifted);
    EXPECT_EQ(term.kind, TermKind::Shift);
    EXPECT_EQ(term.op, std::to_underlying(ShiftOperator::Right));
    EXPECT_EQ(term.format, ScalarFormat{u64});
    EXPECT_EQ(term.secondFormat, ScalarFormat{s32});
    EXPECT_EQ(term.operandCount, 2u);
}

// ---- Integer operations ----

TEST(TermEvaluationTest, IntegerBinaryWithEveryOperator) {
    for (const IntegerOperator op : integerOperators) {
        for (const IntegerFormat format : {s32, u8, u64, s128}) {
            SCOPED_TRACE(std::format("operator {}, format {}", std::to_underlying(op), formatName(format)));
            checkBinary([&](TermTable& table, const Value& lhs,
                            const Value& rhs) { return table.integerBinary(op, lhs, rhs, format); },
                        [&](const Value& lhs, const Value& rhs) { return integerBinary(op, lhs, rhs, format); }, format,
                        integer(100, format), integer(-7, format), format, integer(7, format), integer(3, format));
        }
    }
}

TEST(TermEvaluationTest, UnsignedArithmeticWrapsAround) {
    TermTable table(lp64);
    const Value x = registered(table, firstInput, Value::integer(0xFFFF'FFFF, u32), u32);
    const std::optional<TermId> sum = table.integerBinary(IntegerOperator::Add, x, integer(1, u32), u32);
    ASSERT_TRUE(sum.has_value());
    EXPECT_TRUE(evaluatesTo(table.evaluate(*sum, {}), integer(0, u32)));
    EXPECT_TRUE(evaluatesTo(table.evaluate(*sum, replacing(firstInput, integer(-3, u32))), integer(-2, u32)));
    const std::optional<TermId> product = table.integerBinary(IntegerOperator::Mul, x, integer(16, u32), u32);
    ASSERT_TRUE(product.has_value());
    EXPECT_TRUE(
        evaluatesTo(table.evaluate(*product, replacing(firstInput, integer(0x1000'0001, u32))), integer(16, u32)));
}

TEST(TermEvaluationTest, SignedOverflowUnderSubstitutionHasNoValue) {
    TermTable table(lp64);
    const Value x = registered(table, firstInput, integer(5, s32), s32);
    const std::optional<TermId> sum = table.integerBinary(IntegerOperator::Add, x, integer(1, s32), s32);
    ASSERT_TRUE(sum.has_value());
    EXPECT_TRUE(evaluatesTo(table.evaluate(*sum, {}), integer(6, s32)));
    const Value max = integer(std::numeric_limits<std::int32_t>::max(), s32);
    EXPECT_FALSE(table.evaluate(*sum, replacing(firstInput, max)).has_value());
    const std::optional<TermId> negated = table.integerNegate(x, s32);
    ASSERT_TRUE(negated.has_value());
    const Value min = integer(std::numeric_limits<std::int32_t>::min(), s32);
    EXPECT_FALSE(table.evaluate(*negated, replacing(firstInput, min)).has_value());
}

TEST(TermEvaluationTest, DivisionBySubstitutedZeroHasNoValue) {
    TermTable table(lp64);
    const Value x = registered(table, firstInput, integer(5, u32), u32);
    for (const IntegerOperator op : {IntegerOperator::Div, IntegerOperator::Rem}) {
        const std::optional<TermId> quotient = table.integerBinary(op, integer(100, u32), x, u32);
        ASSERT_TRUE(quotient.has_value());
        EXPECT_TRUE(evaluatesTo(table.evaluate(*quotient, {}), integer(op == IntegerOperator::Div ? 20 : 0, u32)));
        EXPECT_FALSE(table.evaluate(*quotient, replacing(firstInput, integer(0, u32))).has_value());
    }
}

TEST(TermEvaluationTest, ShiftWithMixedFormats) {
    const std::array<std::pair<IntegerFormat, IntegerFormat>, 4> formats{
        {{s32, u64}, {u64, s32}, {s128, u8}, {u32, s128}}};
    for (const ShiftOperator op : {ShiftOperator::Left, ShiftOperator::Right}) {
        for (const auto& [lhsFormat, rhsFormat] : formats) {
            SCOPED_TRACE(std::format("operator {}, {} by {}", std::to_underlying(op), formatName(lhsFormat),
                                     formatName(rhsFormat)));
            // The other values include a negative left operand and the counts 0 and width - 1.
            checkBinary([&](TermTable& table, const Value& lhs,
                            const Value& rhs) { return table.shift(op, lhs, lhsFormat, rhs, rhsFormat); },
                        [&](const Value& lhs, const Value& rhs) { return shift(op, lhs, lhsFormat, rhs, rhsFormat); },
                        lhsFormat, integer(0x1234, lhsFormat), integer(-8, lhsFormat), rhsFormat, integer(3, rhsFormat),
                        integer(lhsFormat.bits - 1, rhsFormat));
            checkBinary([&](TermTable& table, const Value& lhs,
                            const Value& rhs) { return table.shift(op, lhs, lhsFormat, rhs, rhsFormat); },
                        [&](const Value& lhs, const Value& rhs) { return shift(op, lhs, lhsFormat, rhs, rhsFormat); },
                        lhsFormat, integer(1, lhsFormat), integer(5, lhsFormat), rhsFormat, integer(1, rhsFormat),
                        integer(0, rhsFormat));
        }
    }
}

TEST(TermEvaluationTest, ShiftsWithUndefinedBehaviourHaveNoValue) {
    TermTable table(lp64);
    const Value count = registered(table, firstInput, integer(1, s32), s32);
    const Value value = registered(table, secondInput, integer(1, s32), s32);
    const std::optional<TermId> byCount = table.shift(ShiftOperator::Left, integer(1, u64), u64, count, s32);
    const std::optional<TermId> ofValue = table.shift(ShiftOperator::Left, value, s32, integer(30, u8), u8);
    ASSERT_TRUE(byCount.has_value());
    ASSERT_TRUE(ofValue.has_value());
    EXPECT_TRUE(evaluatesTo(table.evaluate(*byCount, replacing(firstInput, integer(63, s32))),
                            Value::integer(UInt128{1} << 63, u64)));
    EXPECT_FALSE(table.evaluate(*byCount, replacing(firstInput, integer(64, s32))).has_value());
    EXPECT_FALSE(table.evaluate(*byCount, replacing(firstInput, integer(-1, s32))).has_value());
    EXPECT_TRUE(evaluatesTo(table.evaluate(*ofValue, {}), integer(1 << 30, s32)));
    EXPECT_FALSE(table.evaluate(*ofValue, replacing(secondInput, integer(2, s32))).has_value());  // overflow
    EXPECT_FALSE(table.evaluate(*ofValue, replacing(secondInput, integer(-1, s32))).has_value()); // negative
}

TEST(TermEvaluationTest, RightShiftOfSubstitutedNegativeValueIsArithmetic) {
    TermTable table(lp64);
    const Value x = registered(table, firstInput, integer(64, s32), s32);
    const std::optional<TermId> shifted = table.shift(ShiftOperator::Right, x, s32, integer(2, s32), s32);
    ASSERT_TRUE(shifted.has_value());
    EXPECT_TRUE(evaluatesTo(table.evaluate(*shifted, replacing(firstInput, integer(-64, s32))), integer(-16, s32)));
}

TEST(TermEvaluationTest, IntegerNegateAndComplement) {
    for (const IntegerFormat format : {s32, u32, s8, u128}) {
        SCOPED_TRACE(formatName(format));
        checkUnary([&](TermTable& table, const Value& operand) { return table.integerNegate(operand, format); },
                   [&](const Value& operand) { return integerNegate(operand, format); }, format, integer(5, format),
                   Value::integer(minBits(format), format));
        checkUnary([&](TermTable& table, const Value& operand) { return table.integerComplement(operand, format); },
                   [&](const Value& operand) { return Result{integerComplement(operand, format)}; }, format,
                   integer(5, format), integer(-1, format));
    }
}

TEST(TermEvaluationTest, IntegerCompareWithEveryComparison) {
    for (const Comparison comparison : comparisons) {
        for (const IntegerFormat format : {s32, u32, s64, u128}) {
            SCOPED_TRACE(std::format("comparison {}, format {}", std::to_underlying(comparison), formatName(format)));
            checkBinary([&](TermTable& table, const Value& lhs,
                            const Value& rhs) { return table.integerCompare(comparison, lhs, rhs, format); },
                        [&](const Value& lhs, const Value& rhs) {
                            return Result{truth(integerCompare(comparison, lhs, rhs, format))};
                        },
                        format, integer(-1, format), integer(1, format), format, integer(1, format),
                        integer(1, format));
        }
    }
}

TEST(TermEvaluationTest, IntegerCompareGivesAnIntZeroOrOne) {
    TermTable table(lp64);
    const Value x = registered(table, firstInput, integer(-1, s64), s64);
    const std::optional<TermId> signedLess = table.integerCompare(Comparison::Less, x, integer(1, s64), s64);
    const Value y = registered(table, secondInput, integer(-1, u64), u64);
    const std::optional<TermId> unsignedLess = table.integerCompare(Comparison::Less, y, integer(1, u64), u64);
    ASSERT_TRUE(signedLess.has_value());
    ASSERT_TRUE(unsignedLess.has_value());
    EXPECT_TRUE(evaluatesTo(table.evaluate(*signedLess, {}), integer(1, s32)));
    EXPECT_TRUE(evaluatesTo(table.evaluate(*unsignedLess, {}), integer(0, s32)));
    EXPECT_TRUE(evaluatesTo(table.evaluate(*unsignedLess, replacing(secondInput, integer(0, u64))), integer(1, s32)));
}

// ---- Floating operations ----

TEST(TermEvaluationTest, FloatBinaryWithEveryOperator) {
    for (const FloatOperator op : floatOperators) {
        for (const FloatFormat format : floatFormats) {
            SCOPED_TRACE(std::format("operator {}, format {}", std::to_underlying(op), std::to_underlying(format)));
            checkBinary([&](TermTable& table, const Value& lhs,
                            const Value& rhs) { return table.floatBinary(op, lhs, rhs, format); },
                        [&](const Value& lhs, const Value& rhs) { return Result{floatBinary(op, lhs, rhs, format)}; },
                        format, floating(1.5L, format), floating(-0.0L, format), format, floating(0.1L, format),
                        floating(3.0L, format));
        }
    }
}

TEST(TermEvaluationTest, FloatDivisionBySubstitutedZeroIsInfinite) {
    TermTable table(lp64);
    const Value x = registered(table, firstInput, Value::ofDouble(2.0), FloatFormat::Double);
    const std::optional<TermId> quotient =
        table.floatBinary(FloatOperator::Div, Value::ofDouble(1.0), x, FloatFormat::Double);
    ASSERT_TRUE(quotient.has_value());
    EXPECT_TRUE(evaluatesTo(table.evaluate(*quotient, {}), Value::ofDouble(0.5)));
    EXPECT_TRUE(evaluatesTo(table.evaluate(*quotient, replacing(firstInput, Value::ofDouble(-0.0))),
                            Value::ofDouble(-std::numeric_limits<double>::infinity())));
}

TEST(TermEvaluationTest, FloatNegate) {
    for (const FloatFormat format : floatFormats) {
        SCOPED_TRACE(std::to_underlying(format));
        checkUnary([&](TermTable& table, const Value& operand) { return table.floatNegate(operand, format); },
                   [&](const Value& operand) { return Result{floatNegate(operand, format)}; }, format,
                   floating(0.0L, format), floating(longDoubleNan, format));
    }
}

TEST(TermEvaluationTest, FloatCompareWithEveryComparison) {
    for (const Comparison comparison : comparisons) {
        for (const FloatFormat format : floatFormats) {
            SCOPED_TRACE(
                std::format("comparison {}, format {}", std::to_underlying(comparison), std::to_underlying(format)));
            checkBinary([&](TermTable& table, const Value& lhs,
                            const Value& rhs) { return table.floatCompare(comparison, lhs, rhs, format); },
                        [&](const Value& lhs, const Value& rhs) {
                            return Result{truth(floatCompare(comparison, lhs, rhs, format))};
                        },
                        format, floating(-1.0L, format), floating(2.0L, format), format, floating(2.0L, format),
                        floating(-0.0L, format));
        }
    }
}

TEST(TermEvaluationTest, ComparisonWithSubstitutedNanHoldsOnlyForNotEqual) {
    for (const Comparison comparison : comparisons) {
        SCOPED_TRACE(std::to_underlying(comparison));
        TermTable table(lp64);
        const Value x = registered(table, firstInput, Value::ofFloat(1.0F), FloatFormat::Float);
        const std::optional<TermId> compared = table.floatCompare(comparison, x, x, FloatFormat::Float);
        ASSERT_TRUE(compared.has_value());
        EXPECT_TRUE(
            evaluatesTo(table.evaluate(*compared, replacing(firstInput, floating(longDoubleNan, FloatFormat::Float))),
                        truth(comparison == Comparison::NotEqual)));
    }
}

// ---- Conversions ----

TEST(TermEvaluationTest, ConvertIntegerIncludingChangesOfValue) {
    const std::array<std::pair<IntegerFormat, IntegerFormat>, 6> conversions{
        {{s32, u8}, {s32, u64}, {u32, s16}, {u128, s32}, {s8, s128}, {u64, s64}}};
    for (const auto& [from, to] : conversions) {
        SCOPED_TRACE(std::format("{} to {}", formatName(from), formatName(to)));
        checkUnary([&](TermTable& table, const Value& operand) { return table.convertInteger(operand, from, to); },
                   [&](const Value& operand) { return Result{convertInteger(operand, to)}; }, from, integer(300, from),
                   integer(-1, from));
    }
}

TEST(TermEvaluationTest, ConvertIntegerTruncates) {
    TermTable table(lp64);
    const Value x = registered(table, firstInput, integer(300, s32), s32);
    const std::optional<TermId> converted = table.convertInteger(x, s32, u8);
    ASSERT_TRUE(converted.has_value());
    EXPECT_TRUE(evaluatesTo(table.evaluate(*converted, {}), integer(44, u8)));
    EXPECT_TRUE(evaluatesTo(table.evaluate(*converted, replacing(firstInput, integer(-1, s32))), integer(255, u8)));
}

TEST(TermEvaluationTest, ToBoolOfIntegersAndFloatingValues) {
    const Value boolean0 = Value::integer(0, IntegerFormat{.bits = 8, .isSigned = false});
    checkUnary([](TermTable& table, const Value& operand) { return table.toBool(operand, s32); },
               [](const Value& operand) { return Result{toBool(operand)}; }, s32, integer(256, s32), integer(0, s32));
    for (const FloatFormat format : floatFormats) {
        SCOPED_TRACE(std::to_underlying(format));
        checkUnary([&](TermTable& table, const Value& operand) { return table.toBool(operand, format); },
                   [](const Value& operand) { return Result{toBool(operand)}; }, format, floating(-0.0L, format),
                   floating(longDoubleNan, format));
    }
    TermTable table(lp64);
    const Value x = registered(table, firstInput, integer(256, s32), s32);
    const std::optional<TermId> converted = table.toBool(x, s32);
    ASSERT_TRUE(converted.has_value());
    EXPECT_TRUE(evaluatesTo(table.evaluate(*converted, {}), Value::integer(1, u8))); // not a truncation to 8 bits
    EXPECT_TRUE(evaluatesTo(table.evaluate(*converted, replacing(firstInput, integer(0, s32))), boolean0));
}

TEST(TermEvaluationTest, IntegerToFloatRounds) {
    const std::array<std::pair<IntegerFormat, FloatFormat>, 4> conversions{{{s32, FloatFormat::Float},
                                                                            {u64, FloatFormat::Double},
                                                                            {s128, FloatFormat::Float},
                                                                            {u64, FloatFormat::LongDouble}}};
    for (const auto& [from, to] : conversions) {
        SCOPED_TRACE(std::format("{} to {}", formatName(from), std::to_underlying(to)));
        checkUnary([&](TermTable& table, const Value& operand) { return table.integerToFloat(operand, from, to); },
                   [&](const Value& operand) { return Result{integerToFloat(operand, from, to)}; }, from,
                   integer(16777217, from), integer(-1, from));
    }
}

TEST(TermEvaluationTest, FloatToIntegerTruncatesAndHasNoValueOutOfRange) {
    const std::array<std::pair<FloatFormat, IntegerFormat>, 5> conversions{{{FloatFormat::Double, s32},
                                                                            {FloatFormat::Double, u32},
                                                                            {FloatFormat::Float, u64},
                                                                            {FloatFormat::LongDouble, s128},
                                                                            {FloatFormat::Float, s8}}};
    for (const DataModel model : {lp64, ilp32}) {
        for (const auto& [from, to] : conversions) {
            SCOPED_TRACE(std::format("{} to {}, data model {}", std::to_underlying(from), formatName(to),
                                     std::to_underlying(model)));
            const UnaryRecord record = [&](TermTable& table, const Value& operand) {
                return table.floatToInteger(operand, from, to);
            };
            const UnaryApply apply = [&](const Value& operand) {
                return floatToInteger(operand, from, to, model);
            };
            checkUnary(record, apply, from, floating(3.9L, from), floating(-3.9L, from), model);
            checkUnary(record, apply, from, floating(100.5L, from), floating(1e30L, from), model);
            checkUnary(record, apply, from, floating(0.0L, from), floating(longDoubleNan, from), model);
        }
    }
}

TEST(TermEvaluationTest, ConvertFloatRounds) {
    for (const FloatFormat from : floatFormats) {
        for (const FloatFormat to : floatFormats) {
            SCOPED_TRACE(std::format("{} to {}", std::to_underlying(from), std::to_underlying(to)));
            checkUnary([&](TermTable& table, const Value& operand) { return table.convertFloat(operand, from, to); },
                       [&](const Value& operand) { return Result{convertFloat(operand, from, to)}; }, from,
                       floating(0.1L, from), floating(1e300L, from));
        }
    }
}

TEST(TermEvaluationTest, ConversionsUnderSubstitution) {
    TermTable table(lp64);
    const Value x = registered(table, firstInput, Value::ofDouble(1.0), FloatFormat::Double);
    const std::optional<TermId> toInt = table.floatToInteger(x, FloatFormat::Double, s32);
    const std::optional<TermId> toFloat = table.convertFloat(x, FloatFormat::Double, FloatFormat::Float);
    ASSERT_TRUE(toInt.has_value());
    ASSERT_TRUE(toFloat.has_value());
    EXPECT_TRUE(evaluatesTo(table.evaluate(*toInt, replacing(firstInput, Value::ofDouble(-3.9))), integer(-3, s32)));
    EXPECT_FALSE(table.evaluate(*toInt, replacing(firstInput, Value::ofDouble(3e9))).has_value());
    EXPECT_TRUE(evaluatesTo(table.evaluate(*toFloat, replacing(firstInput, Value::ofDouble(0.1))),
                            Value::ofFloat(0x1.99999ap-4F)));
}

// ---- Logical operations ----

TEST(TermEvaluationTest, LogicalNotGivesAnInt) {
    checkUnary([](TermTable& table, const Value& operand) { return table.logicalNot(operand, s64); },
               [](const Value& operand) { return Result{truth(toBool(operand).bits() == 0)}; }, s64, integer(0, s64),
               integer(-5, s64));
    checkUnary([](TermTable& table, const Value& operand) { return table.logicalNot(operand, FloatFormat::Double); },
               [](const Value& operand) { return Result{truth(toBool(operand).bits() == 0)}; }, FloatFormat::Double,
               Value::ofDouble(-0.0), Value::ofDouble(nan));
    TermTable table(lp64);
    const Value x = registered(table, firstInput, integer(7, s32), s32);
    const std::optional<TermId> negation = table.logicalNot(x, s32);
    ASSERT_TRUE(negation.has_value());
    EXPECT_TRUE(evaluatesTo(table.evaluate(*negation, {}), integer(0, s32)));
    EXPECT_TRUE(evaluatesTo(table.evaluate(*negation, replacing(firstInput, integer(0, s32))), integer(1, s32)));
}

TEST(TermEvaluationTest, LogicalAndWithBothOperands) {
    TermTable table(lp64);
    const Value x = registered(table, firstInput, integer(1, s32), s32);
    const Value y = registered(table, secondInput, Value::ofDouble(2.0), FloatFormat::Double);
    const std::optional<TermId> both = table.logicalAnd(x, s32, &y, FloatFormat::Double);
    ASSERT_TRUE(both.has_value());
    EXPECT_TRUE(evaluatesTo(table.evaluate(*both, {}), integer(1, s32)));
    EXPECT_TRUE(evaluatesTo(table.evaluate(*both, replacing(secondInput, Value::ofDouble(0.0))), integer(0, s32)));
    EXPECT_TRUE(evaluatesTo(table.evaluate(*both, replacing(secondInput, Value::ofDouble(nan))), integer(1, s32)));
    EXPECT_TRUE(evaluatesTo(table.evaluate(*both, replacing(firstInput, integer(0, s32))), integer(0, s32)));
}

TEST(TermEvaluationTest, LogicalAndIgnoresTheRightOperandIfTheLeftDecides) {
    // x && 10 / x, where 10 / x would divide by zero.
    TermTable table(lp64);
    const Value x = registered(table, firstInput, integer(5, s32), s32);
    const Value quotient = withTerm(table.integerBinary(IntegerOperator::Div, integer(10, s32), x, s32));
    const std::optional<TermId> both = table.logicalAnd(x, s32, &quotient, s32);
    ASSERT_TRUE(both.has_value());
    EXPECT_TRUE(evaluatesTo(table.evaluate(*both, replacing(firstInput, integer(0, s32))), integer(0, s32)));
    EXPECT_TRUE(evaluatesTo(table.evaluate(*both, replacing(firstInput, integer(20, s32))), integer(0, s32)));
    EXPECT_TRUE(evaluatesTo(table.evaluate(*both, replacing(firstInput, integer(10, s32))), integer(1, s32)));
}

TEST(TermEvaluationTest, LogicalAndWithoutTheRightOperand) {
    TermTable table(lp64);
    const Value x = registered(table, firstInput, integer(0, s32), s32);
    const std::optional<TermId> both = table.logicalAnd(x, s32, nullptr, s32);
    ASSERT_TRUE(both.has_value());
    EXPECT_EQ(table.term(*both).operandCount, 1u);
    EXPECT_TRUE(evaluatesTo(table.evaluate(*both, {}), integer(0, s32)));
    EXPECT_FALSE(table.evaluate(*both, replacing(firstInput, integer(3, s32))).has_value());
}

TEST(TermEvaluationTest, LogicalOrWithBothOperands) {
    TermTable table(lp64);
    const Value x = registered(table, firstInput, integer(0, s32), s32);
    const Value y = registered(table, secondInput, integer(3, u8), u8);
    const std::optional<TermId> either = table.logicalOr(x, s32, &y, u8);
    ASSERT_TRUE(either.has_value());
    EXPECT_TRUE(evaluatesTo(table.evaluate(*either, {}), integer(1, s32)));
    EXPECT_TRUE(evaluatesTo(table.evaluate(*either, replacing(secondInput, integer(0, u8))), integer(0, s32)));
    EXPECT_TRUE(evaluatesTo(table.evaluate(*either, replacing(firstInput, integer(-1, s32))), integer(1, s32)));
}

TEST(TermEvaluationTest, LogicalOrIgnoresTheRightOperandIfTheLeftDecides) {
    // x || 10 / x, where 10 / x would divide by zero.
    TermTable table(lp64);
    const Value x = registered(table, firstInput, integer(0, s32), s32);
    const Value quotient = withTerm(table.integerBinary(IntegerOperator::Div, integer(10, s32), x, s32));
    const std::optional<TermId> either = table.logicalOr(x, s32, &quotient, s32);
    ASSERT_TRUE(either.has_value());
    EXPECT_FALSE(table.evaluate(*either, {}).has_value()); // the run divided by zero
    EXPECT_TRUE(evaluatesTo(table.evaluate(*either, replacing(firstInput, integer(-2, s32))), integer(1, s32)));
}

TEST(TermEvaluationTest, LogicalOrWithoutTheRightOperand) {
    TermTable table(lp64);
    const Value x = registered(table, firstInput, Value::ofFloat(0.5F), FloatFormat::Float);
    const std::optional<TermId> either = table.logicalOr(x, FloatFormat::Float, nullptr, s32);
    ASSERT_TRUE(either.has_value());
    EXPECT_TRUE(evaluatesTo(table.evaluate(*either, {}), integer(1, s32)));
    EXPECT_TRUE(evaluatesTo(table.evaluate(*either, replacing(firstInput, Value::ofFloat(-2.0F))), integer(1, s32)));
    EXPECT_FALSE(table.evaluate(*either, replacing(firstInput, Value::ofFloat(0.0F))).has_value());
}

TEST(TermEvaluationTest, LogicalOperationsWithAConstantOperand) {
    // The operand without a term is a constant: 1 && x is x != 0, and 0 || x likewise.
    TermTable table(lp64);
    const Value x = registered(table, firstInput, integer(4, s32), s32);
    const std::optional<TermId> both = table.logicalAnd(integer(1, s32), s32, &x, s32);
    const std::optional<TermId> either = table.logicalOr(integer(0, s32), s32, &x, s32);
    ASSERT_TRUE(both.has_value());
    ASSERT_TRUE(either.has_value());
    for (const std::int32_t value : {0, 4}) {
        EXPECT_TRUE(evaluatesTo(table.evaluate(*both, replacing(firstInput, integer(value, s32))),
                                integer(value != 0 ? 1 : 0, s32)));
        EXPECT_TRUE(evaluatesTo(table.evaluate(*either, replacing(firstInput, integer(value, s32))),
                                integer(value != 0 ? 1 : 0, s32)));
    }
}

TEST(TermEvaluationTest, ConditionalHasAValueOnlyIfTheConditionSelectsTheSameOperand) {
    for (const bool held : {true, false}) {
        SCOPED_TRACE(held);
        TermTable table(lp64);
        const Value c = registered(table, firstInput, integer(held ? 2 : 0, s32), s32);
        const Value selected = registered(table, secondInput, Value::ofDouble(1.5), FloatFormat::Double);
        const std::optional<TermId> chosen = table.conditional(c, s32, held, selected);
        ASSERT_TRUE(chosen.has_value());
        EXPECT_EQ(table.term(*chosen).op, held ? 1u : 0u);
        EXPECT_TRUE(evaluatesTo(table.evaluate(*chosen, {}), Value::ofDouble(1.5)));
        EXPECT_TRUE(
            evaluatesTo(table.evaluate(*chosen, replacing(secondInput, Value::ofDouble(-4.0))), Value::ofDouble(-4.0)));
        EXPECT_FALSE(table.evaluate(*chosen, replacing(firstInput, integer(held ? 0 : -1, s32))).has_value());
        EXPECT_TRUE(evaluatesTo(table.evaluate(*chosen, replacing(firstInput, integer(held ? 7 : 0, s32))),
                                Value::ofDouble(1.5)));
    }
}

TEST(TermEvaluationTest, ConditionalWithAConstantOperand) {
    // c ? 3 : ..., with a floating condition; NaN is true.
    TermTable table(lp64);
    const Value c = registered(table, firstInput, Value::ofDouble(1.0), FloatFormat::Double);
    const std::optional<TermId> chosen = table.conditional(c, FloatFormat::Double, true, integer(3, s64));
    ASSERT_TRUE(chosen.has_value());
    EXPECT_TRUE(evaluatesTo(table.evaluate(*chosen, replacing(firstInput, Value::ofDouble(nan))), integer(3, s64)));
    EXPECT_FALSE(table.evaluate(*chosen, replacing(firstInput, Value::ofDouble(-0.0))).has_value());
}

TEST(TermEvaluationTest, ConditionalWithUndefinedBehaviourInTheSelectedOperand) {
    TermTable table(lp64);
    const Value x = registered(table, firstInput, integer(5, s32), s32);
    const Value quotient = withTerm(table.integerBinary(IntegerOperator::Div, integer(10, s32), x, s32));
    const std::optional<TermId> chosen = table.conditional(integer(1, s32), s32, true, quotient);
    ASSERT_TRUE(chosen.has_value());
    EXPECT_TRUE(evaluatesTo(table.evaluate(*chosen, {}), integer(2, s32)));
    EXPECT_FALSE(table.evaluate(*chosen, replacing(firstInput, integer(0, s32))).has_value());
}

// ---- Limits ----

TEST(TermLimitsTest, DepthIsOneMoreThanTheDeepestOperand) {
    TermTable table(lp64);
    const Value x = registered(table, firstInput, integer(1, s32), s32);
    const Value y = registered(table, secondInput, integer(2, s32), s32);
    const Value sum = withTerm(table.integerBinary(IntegerOperator::Add, x, integer(1, s32), s32));
    const Value negated = withTerm(table.integerNegate(sum, s32));
    const Value product = withTerm(table.integerBinary(IntegerOperator::Mul, y, negated, s32));
    EXPECT_EQ(table.term(*sum.term()).depth, 1u);
    EXPECT_EQ(table.term(*negated.term()).depth, 2u);
    EXPECT_EQ(table.term(*product.term()).depth, 3u);
}

TEST(TermLimitsTest, ResultDeeperThanTheLimitGetsNoTerm) {
    TermTable table(lp64, TermLimits{.maxDepth = 3});
    Value value = registered(table, firstInput, integer(1, s32), s32);
    for (int depth = 1; depth <= 3; ++depth) {
        value = withTerm(table.integerBinary(IntegerOperator::Add, value, integer(1, s32), s32));
    }
    const std::size_t size = table.size();
    EXPECT_FALSE(table.integerBinary(IntegerOperator::Add, value, integer(1, s32), s32).has_value());
    EXPECT_EQ(table.size(), size); // no constant left behind
    EXPECT_TRUE(evaluatesTo(table.evaluate(*value.term(), replacing(firstInput, integer(10, s32))), integer(13, s32)));
}

TEST(TermLimitsTest, DefaultDepthLimitIs64) {
    TermTable table(lp64);
    Value value = registered(table, firstInput, integer(1, u128), u128);
    for (int depth = 1; depth <= 64; ++depth) {
        value = withTerm(table.integerBinary(IntegerOperator::Add, value, value, u128));
    }
    EXPECT_FALSE(table.integerBinary(IntegerOperator::Add, value, value, u128).has_value());
    // Each level doubles the value: shared operands are evaluated once, not 2^64 times.
    EXPECT_TRUE(evaluatesTo(table.evaluate(*value.term(), replacing(firstInput, integer(3, u128))),
                            Value::integer(UInt128{3} << 64, u128)));
}

TEST(TermLimitsTest, NoTermsBeyondTheSizeLimit) {
    TermTable table(lp64, TermLimits{.maxTerms = 4});
    const Value x = registered(table, firstInput, integer(1, s32), s32);
    const Value sum = withTerm(table.integerBinary(IntegerOperator::Add, x, integer(1, s32), s32));
    EXPECT_EQ(table.size(), 3u);
    // A sum with a constant needs two terms; one is left.
    EXPECT_FALSE(table.integerBinary(IntegerOperator::Add, sum, integer(1, s32), s32).has_value());
    EXPECT_EQ(table.size(), 3u);
    EXPECT_TRUE(table.integerNegate(sum, s32).has_value());
    EXPECT_EQ(table.size(), 4u);
    EXPECT_FALSE(table.integerNegate(sum, s32).has_value());
    EXPECT_FALSE(table.input(secondInput, integer(2, s32), s32).has_value());
    EXPECT_EQ(table.size(), 4u);
}

TEST(TermLimitsTest, MaxTermsZeroRecordsNothing) {
    TermTable table(lp64, TermLimits{.maxTerms = 0});
    EXPECT_FALSE(table.input(firstInput, integer(1, s32), s32).has_value());
    EXPECT_EQ(table.size(), 0u);
}

// ---- Shared subterms ----

TEST(TermDagTest, OperandUsedTwiceInOneTerm) {
    TermTable table(lp64);
    const Value x = registered(table, firstInput, integer(3, s32), s32);
    const std::optional<TermId> square = table.integerBinary(IntegerOperator::Mul, x, x, s32);
    ASSERT_TRUE(square.has_value());
    EXPECT_TRUE(evaluatesTo(table.evaluate(*square, replacing(firstInput, integer(-7, s32))), integer(49, s32)));
    EXPECT_EQ(table.inputs(*square), std::vector{firstInput});
}

TEST(TermDagTest, TermUsedManyTimes) {
    TermTable table(lp64);
    const Value x = registered(table, firstInput, integer(1, s64), s64);
    const Value tripled = withTerm(table.integerBinary(IntegerOperator::Mul, x, integer(3, s64), s64));
    Value sum = tripled;
    for (int i = 0; i < 60; ++i) {
        sum = withTerm(table.integerBinary(IntegerOperator::Add, sum, tripled, s64));
    }
    EXPECT_TRUE(evaluatesTo(table.evaluate(*sum.term(), {}), integer(183, s64)));
    EXPECT_TRUE(evaluatesTo(table.evaluate(*sum.term(), replacing(firstInput, integer(2, s64))), integer(366, s64)));
    EXPECT_EQ(table.inputs(*sum.term()), std::vector{firstInput});
}

TEST(TermDagTest, ChainOf64LevelsEachUsedTwiceEvaluatesInLinearTime) {
    // Level i + 1 is (t + 1) + (t - 1) for level t: two paths to each level, 2^64 paths to the input.
    TermTable table(lp64, TermLimits{.maxDepth = 128});
    Value level = registered(table, firstInput, integer(1, u128), u128);
    for (int i = 0; i < 64; ++i) {
        const Value plus = withTerm(table.integerBinary(IntegerOperator::Add, level, integer(1, u128), u128));
        const Value minus = withTerm(table.integerBinary(IntegerOperator::Sub, level, integer(1, u128), u128));
        level = withTerm(table.integerBinary(IntegerOperator::Add, plus, minus, u128));
    }
    EXPECT_EQ(table.term(*level.term()).depth, 128u);
    EXPECT_TRUE(evaluatesTo(table.evaluate(*level.term(), {}), Value::integer(UInt128{1} << 64, u128)));
    EXPECT_TRUE(evaluatesTo(table.evaluate(*level.term(), replacing(firstInput, integer(3, u128))),
                            Value::integer(UInt128{3} << 64, u128)));
    EXPECT_EQ(table.inputs(*level.term()), std::vector{firstInput});
}

TEST(TermDagTest, DeepTermIsEvaluatedWithoutRecursion) {
    TermTable table(lp64, TermLimits{.maxDepth = std::numeric_limits<std::uint16_t>::max()});
    Value value = registered(table, firstInput, integer(5, s32), s32);
    for (unsigned depth = 1; depth <= std::numeric_limits<std::uint16_t>::max(); ++depth) {
        value = withTerm(table.integerComplement(value, s32));
    }
    EXPECT_FALSE(table.integerComplement(value, s32).has_value());
    // An odd number of complements: ~-1.
    EXPECT_TRUE(evaluatesTo(table.evaluate(*value.term(), replacing(firstInput, integer(-1, s32))), integer(0, s32)));
    EXPECT_EQ(table.inputs(*value.term()), std::vector{firstInput});
    EXPECT_FALSE(table.origin(*value.term()).has_value());
}

// ---- Inputs and origins ----

TEST(TermTableTest, InputsAreSortedAndUnique) {
    TermTable table(lp64);
    const Value x = registered(table, firstInput, integer(1, s32), s32);
    const Value y = registered(table, secondInput, integer(2, s32), s32);
    const Value z = registered(table, thirdInput, integer(3, s32), s32);
    const Value left = withTerm(table.integerBinary(IntegerOperator::Add, z, x, s32));
    const Value right = withTerm(table.integerBinary(IntegerOperator::Sub, y, x, s32));
    const std::optional<TermId> product = table.integerBinary(IntegerOperator::Mul, left, right, s32);
    ASSERT_TRUE(product.has_value());
    EXPECT_EQ(table.inputs(*product), (std::vector{firstInput, secondInput, thirdInput}));
    EXPECT_EQ(table.inputs(*right.term()), (std::vector{firstInput, secondInput}));
    EXPECT_EQ(table.inputs(*z.term()), std::vector{thirdInput});
}

TEST(TermTableTest, OriginFollowsConversionsOnly) {
    TermTable table(lp64);
    const Value x = registered(table, firstInput, integer(1, s32), s32);
    const Value wide = withTerm(table.convertInteger(x, s32, s64));
    const Value asDouble = withTerm(table.integerToFloat(wide, s64, FloatFormat::Double));
    const Value asFloat = withTerm(table.convertFloat(asDouble, FloatFormat::Double, FloatFormat::Float));
    const Value back = withTerm(table.floatToInteger(asFloat, FloatFormat::Float, s16));
    EXPECT_EQ(table.origin(*x.term()), firstInput);
    EXPECT_EQ(table.origin(*back.term()), firstInput);

    const Value sum = withTerm(table.integerBinary(IntegerOperator::Add, x, integer(1, s32), s32));
    EXPECT_FALSE(table.origin(*sum.term()).has_value());
    EXPECT_FALSE(table.origin(*withTerm(table.convertInteger(sum, s32, u8)).term()).has_value());
    EXPECT_FALSE(table.origin(*withTerm(table.toBool(x, s32)).term()).has_value());
    EXPECT_FALSE(table.origin(*withTerm(table.logicalNot(x, s32)).term()).has_value());
    EXPECT_FALSE(table.origin(*withTerm(table.integerNegate(x, s32)).term()).has_value());
}

TEST(TermTableTest, ClearForgetsTheRun) {
    TermTable table(lp64);
    const Value x = registered(table, firstInput, integer(1, s32), s32);
    EXPECT_TRUE(table.integerNegate(x, s32).has_value());
    table.clear();
    EXPECT_EQ(table.size(), 0u);
    const Value again = registered(table, firstInput, integer(8, s32), s32);
    const std::optional<TermId> negated = table.integerNegate(again, s32);
    ASSERT_TRUE(negated.has_value());
    EXPECT_TRUE(evaluatesTo(table.evaluate(*negated, {}), integer(-8, s32)));
}

// ---- Preconditions ----

TEST(TermTableDeathTest, InputRegisteredTwiceAsserts) {
    TermTable table(lp64);
    EXPECT_TRUE(table.input(firstInput, integer(1, s32), s32).has_value());
    EXPECT_DEATH((void)table.input(firstInput, integer(1, s32), s32), "registered twice");
}

TEST(TermTableDeathTest, InputOfAnotherFormatAsserts) {
    TermTable table(lp64);
    EXPECT_DEATH((void)table.input(firstInput, Value::ofFloat(1.0F), FloatFormat::Double), "format");
    EXPECT_DEATH((void)table.input(firstInput, Value::integer(255, u8), s8), "format");
}

TEST(TermTableDeathTest, SubstitutedValueOfAnotherFormatAsserts) {
    TermTable table(lp64);
    const Value x = registered(table, firstInput, integer(1, s32), s32);
    EXPECT_DEATH((void)table.evaluate(*x.term(), replacing(firstInput, integer(Int128{1} << 32, s64))), "format");
    EXPECT_DEATH((void)table.evaluate(*x.term(), replacing(firstInput, Value::ofDouble(1.0))), "format");
}

TEST(TermTableDeathTest, ReadingATermOfTheWrongKindAsserts) {
    TermTable table(lp64);
    const Value x = registered(table, firstInput, integer(1, s32), s32);
    const std::optional<TermId> sum = table.integerBinary(IntegerOperator::Add, x, integer(1, s32), s32);
    ASSERT_TRUE(sum.has_value());
    const Term& constant = table.term(table.term(*sum).operands[1]);
    EXPECT_DEATH((void)table.inputOf(constant), "not an input");
    EXPECT_DEATH((void)table.constantOf(table.term(*x.term())), "not a constant");
}

TEST(TermTableDeathTest, TermFromBeforeClearAsserts) {
    TermTable table(lp64);
    const Value x = registered(table, firstInput, integer(1, s32), s32);
    table.clear();
    EXPECT_DEATH((void)table.term(*x.term()), "not in this table");
}

} // namespace
} // namespace nitwit::value
