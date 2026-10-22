#include "value/term.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <expected>
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

using Outcome = std::expected<Value, NoValue>;

const char* describe(NoValue reason) {
    return reason == NoValue::Unknown ? "unknown" : "ends the run";
}

/// Whether an evaluation has no value for `reason`.
::testing::AssertionResult failsWith(const Outcome& actual, NoValue reason) {
    if (actual) {
        return ::testing::AssertionFailure() << describe(*actual) << " instead of " << describe(reason);
    }
    if (actual.error() != reason) {
        return ::testing::AssertionFailure() << describe(actual.error()) << " instead of " << describe(reason);
    }
    return ::testing::AssertionSuccess();
}

/// Whether an evaluation gave what the program gets from `expected`: its value, bit for bit, or the end of the run if
/// its undefined behaviour ends the run.
::testing::AssertionResult evaluatesTo(const Outcome& actual, const Result& expected) {
    if (endsRun(expected.undefinedBehavior)) {
        return failsWith(actual, NoValue::EndsRun);
    }
    if (!actual) {
        return ::testing::AssertionFailure() << describe(actual.error()) << " instead of " << describe(expected.value);
    }
    if (actual->kind() != expected.value.kind() || representation(*actual) != representation(expected.value)) {
        return ::testing::AssertionFailure() << describe(*actual) << " instead of " << describe(expected.value);
    }
    if (actual->term() || actual->origin()) {
        return ::testing::AssertionFailure() << "the value has a term or an origin";
    }
    return ::testing::AssertionSuccess();
}

::testing::AssertionResult evaluatesTo(const Outcome& actual, const Value& expected) {
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

/// `value`, the result of an operation, with the operation's term.
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
    const std::size_t before = table.size();
    const std::optional<TermId> id = table.input(secondInput, integer(-5, s16), s16);
    ASSERT_TRUE(id.has_value());
    const Term& term = table.term(*id);
    EXPECT_EQ(term.kind, TermKind::Input);
    EXPECT_EQ(table.inputOf(term), secondInput);
    EXPECT_EQ(term.format, ScalarFormat{s16});
    EXPECT_EQ(term.depth, 0u);
    EXPECT_EQ(term.operandCount, 0u);
    EXPECT_EQ(table.size(), before + 1);
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

TEST(TermTableTest, InputsMayBeRegisteredInAnyOrder) {
    TermTable table(lp64);
    const Value y = registered(table, secondInput, integer(10, s32), s32);
    const Value x = registered(table, firstInput, integer(3, s32), s32);
    const std::optional<TermId> difference = table.integerBinary(IntegerOperator::Sub, y, x, s32);
    ASSERT_TRUE(difference.has_value());
    EXPECT_TRUE(evaluatesTo(table.evaluate(*difference, {}), integer(7, s32)));
    EXPECT_TRUE(evaluatesTo(table.evaluate(*difference, replacing(firstInput, integer(4, s32))), integer(6, s32)));
    EXPECT_EQ(table.inputs(*difference), (std::vector{firstInput, secondInput}));
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

TEST(TermTableTest, ConstantsRecordTheFormatOfTheirOperand) {
    TermTable table(lp64);
    const Value x = registered(table, firstInput, integer(1, u64), u64);
    const Value y = registered(table, secondInput, Value::ofDouble(0.0), FloatFormat::Double);
    const std::optional<TermId> shifted = table.shift(ShiftOperator::Left, x, u64, integer(3, s8), s8);
    const std::optional<TermId> either = table.logicalOr(integer(0, s16), s16, &y, FloatFormat::Double);
    const std::optional<TermId> compared =
        table.floatCompare(Comparison::Equal, Value::ofDouble(2.0), y, FloatFormat::Double);
    const std::optional<TermId> chosen = table.conditional(x, u64, integer(-2, s128), s128);
    ASSERT_TRUE(shifted && either && compared && chosen);
    EXPECT_EQ(table.term(table.term(*shifted).operands[1]).format, ScalarFormat{s8});
    EXPECT_EQ(table.term(table.term(*either).operands[0]).format, ScalarFormat{s16});
    EXPECT_EQ(table.term(table.term(*compared).operands[0]).format, ScalarFormat{FloatFormat::Double});
    EXPECT_EQ(table.term(table.term(*chosen).operands[1]).format, ScalarFormat{s128}); // the result's format
}

TEST(TermTableTest, OperationsWithoutAnOperandWithTermRecordNothing) {
    TermTable table(lp64);
    const std::size_t before = table.size();
    const Value a = integer(6, s32);
    const Value b = Value::ofDouble(1.5);
    const Value zero = integer(0, s32);
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
    // The right operands of && and || are given exactly when the left one does not decide the result.
    EXPECT_FALSE(table.logicalAnd(a, s32, &b, FloatFormat::Double));
    EXPECT_FALSE(table.logicalAnd(zero, s32, nullptr, s32));
    EXPECT_FALSE(table.logicalOr(zero, s32, &b, FloatFormat::Double));
    EXPECT_FALSE(table.logicalOr(b, FloatFormat::Double, nullptr, s32));
    EXPECT_FALSE(table.conditional(a, s32, b, FloatFormat::Double));
    EXPECT_EQ(table.size(), before);
}

TEST(TermTableTest, TermsRecordTheirKindOperatorAndFormats) {
    TermTable table(lp64);
    const Value i = registered(table, firstInput, integer(1, s16), s16);
    const Value f = registered(table, secondInput, Value::ofFloat(1.0F), FloatFormat::Float);
    struct Expected {
        std::optional<TermId> term;
        TermKind kind;
        unsigned op;
        ScalarFormat format;
        std::optional<ScalarFormat> secondFormat;
    };
    const std::array expected{
        Expected{table.integerBinary(IntegerOperator::Xor, i, integer(3, s16), s16), TermKind::IntegerBinary,
                 std::to_underlying(IntegerOperator::Xor), s16, std::nullopt},
        Expected{table.shift(ShiftOperator::Right, i, s16, integer(2, u8), u8), TermKind::Shift,
                 std::to_underlying(ShiftOperator::Right), s16, u8},
        Expected{table.integerNegate(i, s16), TermKind::IntegerNegate, 0, s16, std::nullopt},
        Expected{table.integerComplement(i, s16), TermKind::IntegerComplement, 0, s16, std::nullopt},
        Expected{table.integerCompare(Comparison::GreaterEqual, i, i, s16), TermKind::IntegerCompare,
                 std::to_underlying(Comparison::GreaterEqual), s16, std::nullopt},
        Expected{table.floatBinary(FloatOperator::Div, f, f, FloatFormat::Float), TermKind::FloatBinary,
                 std::to_underlying(FloatOperator::Div), FloatFormat::Float, std::nullopt},
        Expected{table.floatNegate(f, FloatFormat::Float), TermKind::FloatNegate, 0, FloatFormat::Float, std::nullopt},
        Expected{table.floatCompare(Comparison::Less, f, Value::ofFloat(2.0F), FloatFormat::Float),
                 TermKind::FloatCompare, std::to_underlying(Comparison::Less), FloatFormat::Float, std::nullopt},
        Expected{table.convertInteger(i, s16, u64), TermKind::ConvertInteger, 0, s16, u64},
        Expected{table.toBool(f, FloatFormat::Float), TermKind::ToBool, 0, FloatFormat::Float, std::nullopt},
        Expected{table.integerToFloat(i, s16, FloatFormat::LongDouble), TermKind::IntegerToFloat, 0, s16,
                 FloatFormat::LongDouble},
        Expected{table.floatToInteger(f, FloatFormat::Float, s8), TermKind::FloatToInteger, 0, FloatFormat::Float, s8},
        Expected{table.convertFloat(f, FloatFormat::Float, FloatFormat::Double), TermKind::ConvertFloat, 0,
                 FloatFormat::Float, FloatFormat::Double},
        Expected{table.logicalNot(f, FloatFormat::Float), TermKind::LogicalNot, 0, FloatFormat::Float, std::nullopt},
        Expected{table.logicalAnd(i, s16, &f, FloatFormat::Float), TermKind::LogicalAnd, 0, s16, FloatFormat::Float},
        Expected{table.logicalOr(f, FloatFormat::Float, nullptr, s16), TermKind::LogicalOr, 0, FloatFormat::Float, s16},
        Expected{table.conditional(i, s16, f, FloatFormat::Float), TermKind::Conditional, 1, s16, FloatFormat::Float},
    };
    for (const Expected& entry : expected) {
        SCOPED_TRACE(std::format("{}", std::to_underlying(entry.kind)));
        ASSERT_TRUE(entry.term.has_value());
        const Term& term = table.term(*entry.term);
        EXPECT_EQ(term.kind, entry.kind);
        EXPECT_EQ(term.op, entry.op);
        EXPECT_EQ(term.format, entry.format);
        if (entry.secondFormat) {
            EXPECT_EQ(term.secondFormat, *entry.secondFormat);
        }
    }
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

TEST(TermEvaluationTest, SignedOverflowUnderSubstitutionGivesTheWrappedValue) {
    // The run continues after signed overflow with the hardware's value (arithmetic.hpp, endsRun).
    TermTable table(lp64);
    const Value x = registered(table, firstInput, integer(5, s32), s32);
    const Value max = integer(std::numeric_limits<std::int32_t>::max(), s32);
    const Value min = integer(std::numeric_limits<std::int32_t>::min(), s32);
    const std::optional<TermId> sum = table.integerBinary(IntegerOperator::Add, x, integer(1, s32), s32);
    const std::optional<TermId> negated = table.integerNegate(x, s32);
    const std::optional<TermId> quotient = table.integerBinary(IntegerOperator::Div, x, integer(-1, s32), s32);
    ASSERT_TRUE(sum && negated && quotient);
    EXPECT_TRUE(evaluatesTo(table.evaluate(*sum, {}), integer(6, s32)));
    EXPECT_TRUE(evaluatesTo(table.evaluate(*sum, replacing(firstInput, max)), min));
    EXPECT_TRUE(evaluatesTo(table.evaluate(*negated, replacing(firstInput, min)), min));
    EXPECT_TRUE(evaluatesTo(table.evaluate(*quotient, replacing(firstInput, min)), min));
}

TEST(TermEvaluationTest, DivisionBySubstitutedZeroEndsTheRun) {
    // Division by zero ends the run.
    TermTable table(lp64);
    const Value x = registered(table, firstInput, integer(5, u32), u32);
    for (const IntegerOperator op : {IntegerOperator::Div, IntegerOperator::Rem}) {
        const std::optional<TermId> quotient = table.integerBinary(op, integer(100, u32), x, u32);
        ASSERT_TRUE(quotient.has_value());
        EXPECT_TRUE(evaluatesTo(table.evaluate(*quotient, {}), integer(op == IntegerOperator::Div ? 20 : 0, u32)));
        EXPECT_TRUE(failsWith(table.evaluate(*quotient, replacing(firstInput, integer(0, u32))), NoValue::EndsRun));
    }
}

TEST(TermEvaluationTest, ShiftWithMixedFormats) {
    const std::array<std::pair<IntegerFormat, IntegerFormat>, 4> formats{
        {{s32, u64}, {u64, s32}, {s128, u8}, {u32, s128}}};
    for (const ShiftOperator op : {ShiftOperator::Left, ShiftOperator::Right}) {
        for (const auto& [lhsFormat, rhsFormat] : formats) {
            SCOPED_TRACE(std::format("operator {}, {} by {}", std::to_underlying(op), formatName(lhsFormat),
                                     formatName(rhsFormat)));
            const BinaryRecord record = [&](TermTable& table, const Value& lhs, const Value& rhs) {
                return table.shift(op, lhs, lhsFormat, rhs, rhsFormat);
            };
            const BinaryApply apply = [&](const Value& lhs, const Value& rhs) {
                return shift(op, lhs, lhsFormat, rhs, rhsFormat);
            };
            // The other values include a negative left operand, and the counts width - 1, 0 and width.
            checkBinary(record, apply, lhsFormat, integer(0x1234, lhsFormat), integer(-8, lhsFormat), rhsFormat,
                        integer(3, rhsFormat), integer(lhsFormat.bits - 1, rhsFormat));
            checkBinary(record, apply, lhsFormat, integer(1, lhsFormat), integer(5, lhsFormat), rhsFormat,
                        integer(1, rhsFormat), integer(0, rhsFormat));
            checkBinary(record, apply, lhsFormat, integer(1, lhsFormat), integer(5, lhsFormat), rhsFormat,
                        integer(1, rhsFormat), integer(lhsFormat.bits, rhsFormat));
        }
    }
}

TEST(TermEvaluationTest, ShiftsWithUndefinedBehaviourGiveTheHardwareValue) {
    // The run continues after invalid shifts with the hardware's value: x86 masks the count to 6 bits for 64-bit
    // operands.
    TermTable table(lp64);
    const Value count = registered(table, firstInput, integer(1, s32), s32);
    const Value value = registered(table, secondInput, integer(1, s32), s32);
    const std::optional<TermId> byCount = table.shift(ShiftOperator::Left, integer(1, u64), u64, count, s32);
    const std::optional<TermId> ofValue = table.shift(ShiftOperator::Left, value, s32, integer(30, u8), u8);
    ASSERT_TRUE(byCount && ofValue);
    EXPECT_TRUE(evaluatesTo(table.evaluate(*byCount, replacing(firstInput, integer(63, s32))),
                            Value::integer(UInt128{1} << 63, u64)));
    EXPECT_TRUE(evaluatesTo(table.evaluate(*byCount, replacing(firstInput, integer(64, s32))), integer(1, u64)));
    EXPECT_TRUE(evaluatesTo(table.evaluate(*byCount, replacing(firstInput, integer(-1, s32))),
                            Value::integer(UInt128{1} << 63, u64)));
    EXPECT_TRUE(evaluatesTo(table.evaluate(*ofValue, {}), integer(1 << 30, s32)));
    EXPECT_TRUE(evaluatesTo(table.evaluate(*ofValue, replacing(secondInput, integer(2, s32))),
                            integer(std::numeric_limits<std::int32_t>::min(), s32))); // overflow
    EXPECT_TRUE(evaluatesTo(table.evaluate(*ofValue, replacing(secondInput, integer(-1, s32))),
                            integer(-(1 << 30), s32))); // a negative value
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
    ASSERT_TRUE(signedLess && unsignedLess);
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
        SCOPED_TRACE(std::format("{}", std::to_underlying(format)));
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
        SCOPED_TRACE(std::format("{}", std::to_underlying(comparison)));
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

TEST(TermEvaluationTest, ToBoolOfAnIntegerComparesItWithZero) {
    TermTable table(lp64);
    const Value x = registered(table, firstInput, integer(256, s32), s32);
    const std::optional<TermId> converted = table.toBool(x, s32);
    ASSERT_TRUE(converted.has_value());
    EXPECT_TRUE(evaluatesTo(table.evaluate(*converted, {}), Value::integer(1, u8))); // not a truncation to 8 bits
    EXPECT_TRUE(evaluatesTo(table.evaluate(*converted, replacing(firstInput, integer(0, s32))), Value::integer(0, u8)));
    EXPECT_TRUE(
        evaluatesTo(table.evaluate(*converted, replacing(firstInput, integer(-1, s32))), Value::integer(1, u8)));
}

TEST(TermEvaluationTest, ToBoolOfFloatingValues) {
    // -0.0 converts to 0, NaN to 1.
    for (const FloatFormat format : floatFormats) {
        SCOPED_TRACE(std::format("{}", std::to_underlying(format)));
        checkUnary([&](TermTable& table, const Value& operand) { return table.toBool(operand, format); },
                   [](const Value& operand) { return Result{toBool(operand)}; }, format, floating(-0.0L, format),
                   floating(longDoubleNan, format));
    }
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

TEST(TermEvaluationTest, FloatToIntegerLikeTheRunInBothDataModels) {
    // Out of range, the run continues with the value GCC-compiled code gives, which depends on the data model.
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
            checkUnary(record, apply, from, floating(100.5L, from), floating(5e9L, from), model);
            checkUnary(record, apply, from, floating(0.0L, from), floating(longDoubleNan, from), model);
        }
    }
}

TEST(TermEvaluationTest, FloatToIntegerOutOfRangeDependsOnTheDataModel) {
    // (unsigned int)5e9: x86-64 converts to a 64-bit integer and truncates it, i386 converts 5e9 - 2^31 to a 32-bit
    // integer, which overflows (tests/unit/value/data/float_to_integer.txt).
    for (const auto& [model, expected] : {std::pair{lp64, 0x2A05'F200u}, std::pair{ilp32, 0u}}) {
        SCOPED_TRACE(std::format("{}", std::to_underlying(model)));
        TermTable table(model);
        const Value x = registered(table, firstInput, Value::ofDouble(1.0), FloatFormat::Double);
        const std::optional<TermId> converted = table.floatToInteger(x, FloatFormat::Double, u32);
        ASSERT_TRUE(converted.has_value());
        EXPECT_TRUE(evaluatesTo(table.evaluate(*converted, replacing(firstInput, Value::ofDouble(5e9))),
                                Value::integer(expected, u32)));
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
    ASSERT_TRUE(toInt && toFloat);
    EXPECT_TRUE(evaluatesTo(table.evaluate(*toInt, replacing(firstInput, Value::ofDouble(-3.9))), integer(-3, s32)));
    // Out of range: cvttsd2si gives the "integer indefinite" value.
    EXPECT_TRUE(evaluatesTo(table.evaluate(*toInt, replacing(firstInput, Value::ofDouble(3e9))),
                            integer(std::numeric_limits<std::int32_t>::min(), s32)));
    EXPECT_TRUE(evaluatesTo(table.evaluate(*toFloat, replacing(firstInput, Value::ofDouble(0.1))),
                            Value::ofFloat(0x1.99999ap-4F)));
}

// ---- Logical operations ----

TEST(TermEvaluationTest, LogicalNotOfAnIntegerGivesAnInt) {
    TermTable table(lp64);
    const Value x = registered(table, firstInput, integer(7, s64), s64);
    const std::optional<TermId> negation = table.logicalNot(x, s64);
    ASSERT_TRUE(negation.has_value());
    EXPECT_TRUE(evaluatesTo(table.evaluate(*negation, {}), integer(0, s32)));
    EXPECT_TRUE(evaluatesTo(table.evaluate(*negation, replacing(firstInput, integer(0, s64))), integer(1, s32)));
    EXPECT_TRUE(evaluatesTo(table.evaluate(*negation, replacing(firstInput, integer(-5, s64))), integer(0, s32)));
}

TEST(TermEvaluationTest, LogicalNotOfAFloatingValue) {
    // !-0.0 is 1, !NaN is 0.
    checkUnary([](TermTable& table, const Value& operand) { return table.logicalNot(operand, FloatFormat::Double); },
               [](const Value& operand) { return Result{truth(toBool(operand).bits() == 0)}; }, FloatFormat::Double,
               Value::ofDouble(-0.0), Value::ofDouble(nan));
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
    EXPECT_TRUE(failsWith(table.evaluate(*both, replacing(firstInput, integer(3, s32))), NoValue::Unknown));
}

TEST(TermEvaluationTest, LogicalAndWithALeftOperandThatDividesByZero) {
    // 10 / x && y
    TermTable table(lp64);
    const Value x = registered(table, firstInput, integer(5, s32), s32);
    const Value y = registered(table, secondInput, integer(1, s32), s32);
    const Value quotient =
        withTerm(table.integerBinary(IntegerOperator::Div, integer(10, s32), x, s32), integer(2, s32));
    const std::optional<TermId> both = table.logicalAnd(quotient, s32, &y, s32);
    ASSERT_TRUE(both.has_value());
    EXPECT_TRUE(evaluatesTo(table.evaluate(*both, {}), integer(1, s32)));
    EXPECT_TRUE(failsWith(table.evaluate(*both, replacing(firstInput, integer(0, s32))), NoValue::EndsRun));
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
    // x || 10 / (x + 1), where 10 / (x + 1) would divide by zero for x = -1.
    TermTable table(lp64);
    const Value x = registered(table, firstInput, integer(0, s32), s32);
    const Value successor = withTerm(table.integerBinary(IntegerOperator::Add, x, integer(1, s32), s32));
    const Value quotient = withTerm(table.integerBinary(IntegerOperator::Div, integer(10, s32), successor, s32));
    const std::optional<TermId> either = table.logicalOr(x, s32, &quotient, s32);
    ASSERT_TRUE(either.has_value());
    EXPECT_TRUE(evaluatesTo(table.evaluate(*either, {}), integer(1, s32)));
    EXPECT_TRUE(evaluatesTo(table.evaluate(*either, replacing(firstInput, integer(-1, s32))), integer(1, s32)));
}

TEST(TermEvaluationTest, LogicalOrWithoutTheRightOperand) {
    TermTable table(lp64);
    const Value x = registered(table, firstInput, Value::ofFloat(0.5F), FloatFormat::Float);
    const std::optional<TermId> either = table.logicalOr(x, FloatFormat::Float, nullptr, s32);
    ASSERT_TRUE(either.has_value());
    EXPECT_TRUE(evaluatesTo(table.evaluate(*either, {}), integer(1, s32)));
    EXPECT_TRUE(evaluatesTo(table.evaluate(*either, replacing(firstInput, Value::ofFloat(-2.0F))), integer(1, s32)));
    EXPECT_TRUE(failsWith(table.evaluate(*either, replacing(firstInput, Value::ofFloat(0.0F))), NoValue::Unknown));
}

TEST(TermEvaluationTest, LogicalOrWithALeftOperandThatDividesByZero) {
    // 10 / x || y
    TermTable table(lp64);
    const Value x = registered(table, firstInput, integer(20, s32), s32);
    const Value y = registered(table, secondInput, integer(1, s32), s32);
    const Value quotient = withTerm(table.integerBinary(IntegerOperator::Div, integer(10, s32), x, s32));
    const std::optional<TermId> either = table.logicalOr(quotient, s32, &y, s32);
    ASSERT_TRUE(either.has_value());
    EXPECT_TRUE(evaluatesTo(table.evaluate(*either, {}), integer(1, s32)));
    EXPECT_TRUE(failsWith(table.evaluate(*either, replacing(firstInput, integer(0, s32))), NoValue::EndsRun));
}

TEST(TermEvaluationTest, LogicalOperationsWithAConstantOperand) {
    // The operand without a term is a constant: 1 && x is x != 0, and 0 || x likewise.
    TermTable table(lp64);
    const Value x = registered(table, firstInput, integer(4, s32), s32);
    const std::optional<TermId> both = table.logicalAnd(integer(1, s32), s32, &x, s32);
    const std::optional<TermId> either = table.logicalOr(integer(0, s32), s32, &x, s32);
    ASSERT_TRUE(both && either);
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
        const std::optional<TermId> chosen = table.conditional(c, s32, selected, FloatFormat::Double);
        ASSERT_TRUE(chosen.has_value());
        EXPECT_EQ(table.term(*chosen).op, held ? 1u : 0u);
        EXPECT_TRUE(evaluatesTo(table.evaluate(*chosen, {}), Value::ofDouble(1.5)));
        EXPECT_TRUE(
            evaluatesTo(table.evaluate(*chosen, replacing(secondInput, Value::ofDouble(-4.0))), Value::ofDouble(-4.0)));
        EXPECT_TRUE(
            failsWith(table.evaluate(*chosen, replacing(firstInput, integer(held ? 0 : -1, s32))), NoValue::Unknown));
        EXPECT_TRUE(evaluatesTo(table.evaluate(*chosen, replacing(firstInput, integer(held ? 7 : 0, s32))),
                                Value::ofDouble(1.5)));
    }
}

TEST(TermEvaluationTest, ConditionalWithAConstantOperand) {
    // c ? 3 : ..., with a floating condition; NaN is true.
    TermTable table(lp64);
    const Value c = registered(table, firstInput, Value::ofDouble(1.0), FloatFormat::Double);
    const std::optional<TermId> chosen = table.conditional(c, FloatFormat::Double, integer(3, s64), s64);
    ASSERT_TRUE(chosen.has_value());
    EXPECT_TRUE(evaluatesTo(table.evaluate(*chosen, replacing(firstInput, Value::ofDouble(nan))), integer(3, s64)));
    EXPECT_TRUE(failsWith(table.evaluate(*chosen, replacing(firstInput, Value::ofDouble(-0.0))), NoValue::Unknown));
}

TEST(TermEvaluationTest, ConditionalWithAConstantConditionIsTheSelectedOperand) {
    // 1 ? 10 / x : ...
    TermTable table(lp64);
    const Value x = registered(table, firstInput, integer(5, s32), s32);
    const Value quotient = withTerm(table.integerBinary(IntegerOperator::Div, integer(10, s32), x, s32));
    const std::size_t before = table.size();
    EXPECT_EQ(table.conditional(integer(1, s32), s32, quotient, s32), quotient.term());
    EXPECT_EQ(table.size(), before);
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

TEST(TermLimitsTest, ResultDeeperThanTheLimitGetsTheOpaqueTerm) {
    TermTable table(lp64, TermLimits{.maxDepth = 3});
    Value value = registered(table, firstInput, integer(1, s32), s32);
    for (int depth = 1; depth <= 3; ++depth) {
        value = withTerm(table.integerBinary(IntegerOperator::Add, value, integer(1, s32), s32));
    }
    const std::size_t size = table.size();
    EXPECT_EQ(table.integerBinary(IntegerOperator::Add, value, integer(1, s32), s32), table.opaque());
    EXPECT_EQ(table.size(), size); // no constant left behind
    EXPECT_TRUE(evaluatesTo(table.evaluate(*value.term(), replacing(firstInput, integer(10, s32))), integer(13, s32)));
}

TEST(TermLimitsTest, DefaultDepthLimitIs64) {
    TermTable table(lp64);
    Value value = registered(table, firstInput, integer(1, u128), u128);
    for (int depth = 1; depth <= 64; ++depth) {
        value = withTerm(table.integerBinary(IntegerOperator::Add, value, value, u128));
    }
    EXPECT_EQ(table.integerBinary(IntegerOperator::Add, value, value, u128), table.opaque());
    // Each level doubles the value: shared operands are evaluated once, not 2^64 times.
    EXPECT_TRUE(evaluatesTo(table.evaluate(*value.term(), replacing(firstInput, integer(3, u128))),
                            Value::integer(UInt128{3} << 64, u128)));
}

TEST(TermLimitsTest, ResultsBeyondTheSizeLimitGetTheOpaqueTerm) {
    // The opaque term, the input, and the sum with its constant fill the table.
    TermTable table(lp64, TermLimits{.maxTerms = 4});
    const Value x = registered(table, firstInput, integer(1, s32), s32);
    const Value sum = withTerm(table.integerBinary(IntegerOperator::Add, x, integer(1, s32), s32));
    EXPECT_EQ(table.size(), 4u);
    EXPECT_EQ(table.integerBinary(IntegerOperator::Add, sum, integer(1, s32), s32), table.opaque());
    EXPECT_EQ(table.integerNegate(sum, s32), table.opaque());
    EXPECT_EQ(table.size(), 4u);
}

TEST(TermLimitsTest, AnOperationThatDoesNotFitLeavesNoConstantBehind) {
    // One term is left, but a sum with a constant needs two.
    TermTable table(lp64, TermLimits{.maxTerms = 3});
    const Value x = registered(table, firstInput, integer(1, s32), s32);
    EXPECT_EQ(table.integerBinary(IntegerOperator::Add, x, integer(1, s32), s32), table.opaque());
    EXPECT_EQ(table.size(), 2u);
    EXPECT_NE(table.integerNegate(x, s32), table.opaque());
    EXPECT_EQ(table.size(), 3u);
}

TEST(TermLimitsTest, InputsGetATermInAFullTable) {
    TermTable table(lp64, TermLimits{.maxTerms = 2});
    const Value x = registered(table, firstInput, integer(1, s32), s32);
    EXPECT_EQ(table.integerNegate(x, s32), table.opaque());
    const Value y = registered(table, secondInput, integer(2, s32), s32);
    EXPECT_EQ(table.size(), 3u);
    EXPECT_TRUE(evaluatesTo(table.evaluate(*y.term(), replacing(secondInput, integer(5, s32))), integer(5, s32)));
    EXPECT_EQ(table.inputs(*y.term()), std::vector{secondInput});
}

TEST(TermLimitsTest, MaxTermsZeroRecordsNothing) {
    TermTable table(lp64, TermLimits{.maxTerms = 0});
    EXPECT_FALSE(table.opaque().has_value());
    EXPECT_FALSE(table.input(firstInput, integer(1, s32), s32).has_value());
    EXPECT_EQ(table.size(), 0u);
    table.clear();
    EXPECT_EQ(table.size(), 0u);
}

TEST(TermLimitsTest, InputsBeyondTwiceTheSizeLimitGetTheOpaqueTerm) {
    // The opaque term and three inputs fill twice the size limit.
    TermTable table(lp64, TermLimits{.maxTerms = 2});
    for (std::uint32_t i = 0; i < 3; ++i) {
        const std::optional<TermId> input = table.input(InputId{i}, integer(i, s32), s32);
        ASSERT_TRUE(input.has_value());
        EXPECT_FALSE(table.isOpaque(*input));
    }
    EXPECT_EQ(table.size(), 4u);
    EXPECT_EQ(table.input(InputId{3}, integer(3, s32), s32), table.opaque());
    EXPECT_EQ(table.size(), 4u);
}

// ---- The opaque term ----

TEST(TermOpaqueTest, EveryTableHasAnOpaqueTerm) {
    TermTable table(lp64);
    for (int run = 0; run < 2; ++run) {
        const std::optional<TermId> opaque = table.opaque();
        ASSERT_TRUE(opaque.has_value());
        EXPECT_TRUE(table.isOpaque(*opaque));
        EXPECT_EQ(table.term(*opaque).kind, TermKind::Opaque);
        EXPECT_TRUE(failsWith(table.evaluate(*opaque, {}), NoValue::Unknown));
        EXPECT_TRUE(table.inputs(*opaque).empty());
        EXPECT_FALSE(table.origin(*opaque).has_value());
        EXPECT_EQ(table.size(), 1u);
        table.clear();
    }
}

TEST(TermOpaqueTest, OperationsOnTheOpaqueTermGiveTheOpaqueTerm) {
    TermTable table(lp64);
    const Value x = registered(table, firstInput, integer(1, s32), s32);
    const Value unknown = withTerm(table.opaque(), integer(1, s32));
    const std::size_t before = table.size();
    EXPECT_EQ(table.integerBinary(IntegerOperator::Add, x, unknown, s32), table.opaque());
    EXPECT_EQ(table.integerNegate(unknown, s32), table.opaque());
    EXPECT_EQ(table.convertInteger(unknown, s32, u8), table.opaque());
    EXPECT_EQ(table.logicalAnd(x, s32, &unknown, s32), table.opaque());
    EXPECT_EQ(table.logicalOr(unknown, s32, nullptr, s32), table.opaque());
    EXPECT_EQ(table.conditional(unknown, s32, x, s32), table.opaque());
    EXPECT_EQ(table.conditional(x, s32, unknown, s32), table.opaque());
    EXPECT_EQ(table.conditional(integer(1, s32), s32, unknown, s32), table.opaque());
    EXPECT_EQ(table.size(), before);
}

TEST(TermOpaqueTest, OtherTermsAreNotOpaque) {
    TermTable table(lp64);
    const Value x = registered(table, firstInput, integer(1, s32), s32);
    const std::optional<TermId> negated = table.integerNegate(x, s32);
    ASSERT_TRUE(negated.has_value());
    EXPECT_FALSE(table.isOpaque(*x.term()));
    EXPECT_FALSE(table.isOpaque(*negated));
}

// ---- Unknown values and the end of the run ----

TEST(TermNoValueTest, AnOperationEndsTheRunIfAnyOperandDoesEvenIfAnotherIsUnknown) {
    // (x || ...) + 10 / y, where the run did not evaluate the right operand of ||.
    TermTable table(lp64);
    const Value x = registered(table, firstInput, integer(1, s32), s32);
    const Value y = registered(table, secondInput, integer(5, s32), s32);
    const Value either = withTerm(table.logicalOr(x, s32, nullptr, s32), integer(1, s32));
    const Value quotient =
        withTerm(table.integerBinary(IntegerOperator::Div, integer(10, s32), y, s32), integer(2, s32));
    const std::optional<TermId> sum = table.integerBinary(IntegerOperator::Add, either, quotient, s32);
    ASSERT_TRUE(sum.has_value());
    EXPECT_TRUE(evaluatesTo(table.evaluate(*sum, {}), integer(3, s32)));
    Substitution both;
    both.set(firstInput, integer(0, s32));
    both.set(secondInput, integer(0, s32));
    EXPECT_TRUE(failsWith(table.evaluate(*sum, both), NoValue::EndsRun));
    EXPECT_TRUE(failsWith(table.evaluate(*sum, replacing(firstInput, integer(0, s32))), NoValue::Unknown));
    EXPECT_TRUE(failsWith(table.evaluate(*sum, replacing(secondInput, integer(0, s32))), NoValue::EndsRun));
}

TEST(TermNoValueTest, LogicalAndWithAnUnknownLeftOperandIsUnknown) {
    // (x || ...) && 10 / y: whether the program reaches 10 / y is unknown.
    TermTable table(lp64);
    const Value x = registered(table, firstInput, integer(1, s32), s32);
    const Value y = registered(table, secondInput, integer(5, s32), s32);
    const Value either = withTerm(table.logicalOr(x, s32, nullptr, s32), integer(1, s32));
    const Value quotient =
        withTerm(table.integerBinary(IntegerOperator::Div, integer(10, s32), y, s32), integer(2, s32));
    const std::optional<TermId> both = table.logicalAnd(either, s32, &quotient, s32);
    ASSERT_TRUE(both.has_value());
    Substitution substitution;
    substitution.set(firstInput, integer(0, s32));
    substitution.set(secondInput, integer(0, s32));
    EXPECT_TRUE(failsWith(table.evaluate(*both, substitution), NoValue::Unknown));
    EXPECT_TRUE(failsWith(table.evaluate(*both, replacing(secondInput, integer(0, s32))), NoValue::EndsRun));
}

TEST(TermNoValueTest, ConditionalWithAConditionThatEndsTheRun) {
    // 10 / x ? y : ...
    TermTable table(lp64);
    const Value x = registered(table, firstInput, integer(5, s32), s32);
    const Value y = registered(table, secondInput, integer(7, s32), s32);
    const Value quotient =
        withTerm(table.integerBinary(IntegerOperator::Div, integer(10, s32), x, s32), integer(2, s32));
    const std::optional<TermId> chosen = table.conditional(quotient, s32, y, s32);
    ASSERT_TRUE(chosen.has_value());
    EXPECT_TRUE(evaluatesTo(table.evaluate(*chosen, replacing(secondInput, integer(9, s32))), integer(9, s32)));
    EXPECT_TRUE(failsWith(table.evaluate(*chosen, replacing(firstInput, integer(0, s32))), NoValue::EndsRun));
    EXPECT_TRUE(failsWith(table.evaluate(*chosen, replacing(firstInput, integer(20, s32))), NoValue::Unknown));
}

// ---- Strict terms ----

TEST(TermStrictTest, TermsRecordedInAStrictScopeAreStrict) {
    TermTable table(lp64);
    const Value x = registered(table, firstInput, integer(1, s32), s32);
    const std::optional<TermId> before = table.integerNegate(x, s32);
    std::optional<TermId> inside;
    {
        const TermTable::StrictScope scope(table);
        inside = table.integerNegate(x, s32);
    }
    const std::optional<TermId> after = table.integerNegate(x, s32);
    ASSERT_TRUE(before && inside && after);
    EXPECT_FALSE(table.term(*before).strict);
    EXPECT_TRUE(table.term(*inside).strict);
    EXPECT_FALSE(table.term(*after).strict);
}

TEST(TermStrictTest, AScopeRestoresThePreviousMode) {
    TermTable table(lp64);
    const Value x = registered(table, firstInput, integer(1, s32), s32);
    {
        const TermTable::StrictScope outer(table);
        {
            const TermTable::StrictScope inner(table);
        }
        const std::optional<TermId> negated = table.integerNegate(x, s32);
        ASSERT_TRUE(negated.has_value());
        EXPECT_TRUE(table.term(*negated).strict);
    }
    const std::optional<TermId> negated = table.integerNegate(x, s32);
    ASSERT_TRUE(negated.has_value());
    EXPECT_FALSE(table.term(*negated).strict);
}

TEST(TermStrictTest, AnyUndefinedBehaviourOfAStrictTermEndsTheRun) {
    TermTable table(lp64);
    const Value x = registered(table, firstInput, integer(5, s32), s32);
    const Value d = registered(table, secondInput, Value::ofDouble(1.0), FloatFormat::Double);
    std::optional<TermId> sum;
    std::optional<TermId> shifted;
    std::optional<TermId> converted;
    {
        const TermTable::StrictScope scope(table);
        sum = table.integerBinary(IntegerOperator::Add, x, integer(1, s32), s32);
        shifted = table.shift(ShiftOperator::Left, integer(1, s32), s32, x, s32);
        converted = table.floatToInteger(d, FloatFormat::Double, s32);
    }
    ASSERT_TRUE(sum && shifted && converted);
    EXPECT_TRUE(evaluatesTo(table.evaluate(*sum, {}), integer(6, s32)));
    const Value max = integer(std::numeric_limits<std::int32_t>::max(), s32);
    EXPECT_TRUE(failsWith(table.evaluate(*sum, replacing(firstInput, max)), NoValue::EndsRun));
    EXPECT_TRUE(failsWith(table.evaluate(*shifted, replacing(firstInput, integer(32, s32))), NoValue::EndsRun));
    EXPECT_TRUE(failsWith(table.evaluate(*converted, replacing(secondInput, Value::ofDouble(3e9))), NoValue::EndsRun));
}

TEST(TermStrictTest, SubtermsRecordedOutsideTheScopeKeepTheProgramsSemantics) {
    // s = x + 1 in the program; s == 0 and s + 1 in a constraint.
    TermTable table(lp64);
    const Value x = registered(table, firstInput, integer(5, s32), s32);
    const Value s = withTerm(table.integerBinary(IntegerOperator::Add, x, integer(1, s32), s32), integer(6, s32));
    std::optional<TermId> compared;
    std::optional<TermId> incremented;
    {
        const TermTable::StrictScope scope(table);
        compared = table.integerCompare(Comparison::Equal, s, integer(0, s32), s32);
        incremented = table.integerBinary(IntegerOperator::Add, s, integer(1, s32), s32);
    }
    ASSERT_TRUE(compared && incremented);
    // With x = INT_MAX, the program's s wraps around to INT_MIN.
    const std::int32_t max = std::numeric_limits<std::int32_t>::max();
    EXPECT_TRUE(evaluatesTo(table.evaluate(*compared, replacing(firstInput, integer(max, s32))), integer(0, s32)));
    EXPECT_TRUE(evaluatesTo(table.evaluate(*incremented, replacing(firstInput, integer(max, s32))),
                            integer(std::numeric_limits<std::int32_t>::min() + 1, s32)));
    // With x = INT_MAX - 1, s is INT_MAX, and the constraint's s + 1 overflows.
    EXPECT_TRUE(
        failsWith(table.evaluate(*incremented, replacing(firstInput, integer(max - 1, s32))), NoValue::EndsRun));
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
    EXPECT_EQ(table.integerComplement(value, s32), table.opaque());
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

TEST(TermTableTest, SubtermsListEveryTermOnceWithOperandsFirst) {
    TermTable table(lp64);
    const Value x = registered(table, firstInput, integer(1, s32), s32);
    const Value y = registered(table, secondInput, integer(2, s32), s32);
    const Value sum = withTerm(table.integerBinary(IntegerOperator::Add, x, y, s32));
    const Value twice = withTerm(table.integerBinary(IntegerOperator::Mul, sum, sum, s32));
    const std::vector<TermId> subterms = table.subterms(*twice.term());
    EXPECT_EQ(subterms, (std::vector{*x.term(), *y.term(), *sum.term(), *twice.term()}));
    EXPECT_EQ(table.subterms(*y.term()), std::vector{*y.term()});
    // A constant operand is a subterm too.
    const Value next = withTerm(table.integerBinary(IntegerOperator::Add, x, integer(7, s32), s32));
    const std::vector<TermId> withConstant = table.subterms(*next.term());
    ASSERT_EQ(withConstant.size(), 3u);
    EXPECT_EQ(table.term(withConstant[1]).kind, TermKind::Constant);
}

TEST(TermTableTest, ResultFormatsAreThoseOfTheOperationsValue) {
    TermTable table(lp64);
    const Value i = registered(table, firstInput, integer(1, s16), s16);
    const Value f = registered(table, secondInput, Value::ofFloat(1.0F), FloatFormat::Float);
    const std::array<std::pair<std::optional<TermId>, ScalarFormat>, 14> expected{{
        {i.term(), s16},
        {table.integerBinary(IntegerOperator::Xor, i, integer(3, s16), s16), s16},
        {table.shift(ShiftOperator::Right, i, s16, integer(2, u8), u8), s16},
        {table.integerNegate(i, s16), s16},
        {table.integerCompare(Comparison::Less, i, i, s16), s32},
        {table.floatBinary(FloatOperator::Div, f, f, FloatFormat::Float), FloatFormat::Float},
        {table.floatCompare(Comparison::Less, f, f, FloatFormat::Float), s32},
        {table.convertInteger(i, s16, u64), u64},
        {table.toBool(f, FloatFormat::Float), u8},
        {table.integerToFloat(i, s16, FloatFormat::LongDouble), FloatFormat::LongDouble},
        {table.floatToInteger(f, FloatFormat::Float, s8), s8},
        {table.logicalNot(f, FloatFormat::Float), s32},
        {table.logicalAnd(i, s16, &f, FloatFormat::Float), s32},
        {table.conditional(i, s16, f, FloatFormat::Float), FloatFormat::Float},
    }};
    for (const auto& [term, format] : expected) {
        ASSERT_TRUE(term.has_value());
        EXPECT_EQ(table.resultFormat(*term), format) << std::to_underlying(table.term(*term).kind);
    }
}

TEST(TermTableTest, OriginFollowsConversionsOnly) {
    TermTable table(lp64);
    const Value x = registered(table, firstInput, integer(1, s32), s32);
    const Value wide = withTerm(table.convertInteger(x, s32, s64));
    const Value asDouble = withTerm(table.integerToFloat(wide, s64, FloatFormat::Double));
    const Value asFloat = withTerm(table.convertFloat(asDouble, FloatFormat::Double, FloatFormat::Float));
    const Value back = withTerm(table.floatToInteger(asFloat, FloatFormat::Float, s16));
    const Value boolean = withTerm(table.toBool(back, s16));
    EXPECT_EQ(table.origin(*x.term()), firstInput);
    EXPECT_EQ(table.origin(*back.term()), firstInput);
    EXPECT_EQ(table.origin(*boolean.term()), firstInput);

    const Value sum = withTerm(table.integerBinary(IntegerOperator::Add, x, integer(1, s32), s32));
    EXPECT_FALSE(table.origin(*sum.term()).has_value());
    EXPECT_FALSE(table.origin(*withTerm(table.convertInteger(sum, s32, u8)).term()).has_value());
    EXPECT_FALSE(table.origin(*withTerm(table.logicalNot(x, s32)).term()).has_value());
    EXPECT_FALSE(table.origin(*withTerm(table.integerNegate(x, s32)).term()).has_value());
}

TEST(TermTableTest, ClearForgetsTheRun) {
    TermTable table(lp64);
    const Value x = registered(table, firstInput, integer(1, s32), s32);
    EXPECT_TRUE(table.integerNegate(x, s32).has_value());
    table.clear();
    EXPECT_EQ(table.size(), 1u); // the opaque term
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

TEST(TermTableDeathTest, RightOperandOfLogicalOperationsMustMatchTheRun) {
    TermTable table(lp64);
    const Value x = registered(table, firstInput, integer(1, s32), s32);
    const Value y = registered(table, secondInput, integer(0, s32), s32);
    EXPECT_DEATH((void)table.logicalAnd(x, s32, nullptr, s32), "right operand");
    EXPECT_DEATH((void)table.logicalAnd(y, s32, &x, s32), "right operand");
    EXPECT_DEATH((void)table.logicalOr(y, s32, nullptr, s32), "right operand");
    EXPECT_DEATH((void)table.logicalOr(x, s32, &y, s32), "right operand");
}

TEST(TermTableDeathTest, ConstantOperandOfAnotherFormatAsserts) {
    TermTable table(lp64);
    const Value x = registered(table, firstInput, integer(1, s32), s32);
    EXPECT_DEATH((void)table.conditional(x, s32, Value::ofBytes({std::byte{1}, std::byte{2}}), s32), "its format");
    EXPECT_DEATH((void)table.integerBinary(IntegerOperator::Add, x, Value(), s32), "its format");
    EXPECT_DEATH((void)table.integerBinary(IntegerOperator::Add, x, Value::integer(UInt128{1} << 32, u64), s32),
                 "its format");
    EXPECT_DEATH((void)table.floatCompare(Comparison::Less, withTerm(x.term(), Value::ofFloat(1.0F)),
                                          Value::ofDouble(1.0), FloatFormat::Float),
                 "its format");
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
