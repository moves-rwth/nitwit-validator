// The search for input values (search.hpp; validate/README.md, "Search") on term tables built by hand: decomposition,
// inversion, candidates, kept clauses, unknown atoms, the order of inputs and values, and the README's examples.

#include "validate/search.hpp"

#include <cstdint>
#include <format>
#include <map>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "sema/types.hpp"
#include "validate/validator.hpp"
#include "validate/values.hpp"

namespace nitwit::validate {
namespace {

using ast::BuiltinKind;
using value::Comparison;
using value::FloatOperator;
using value::InputId;
using value::IntegerOperator;
using value::TermKind;
using value::Value;

constexpr value::IntegerFormat s32{.bits = 32, .isSigned = true};
constexpr value::IntegerFormat u32{.bits = 32, .isSigned = false};

class SearchTest : public ::testing::Test {
protected:
    ast::QualType type(BuiltinKind kind) { return context_.builtin(kind); }

    value::ScalarFormat formatOf(ast::QualType of) {
        if (sema::isFloating(context_, of)) {
            return sema::floatFormat(context_, of);
        }
        return sema::integerFormat(context_, of);
    }

    /// Registers input `id` of the run with the value `number` of type `kind`.
    Value input(std::uint32_t id, Int128 number, InputSource source = InputSource::Default,
                BuiltinKind kind = BuiltinKind::Int) {
        const ast::QualType inputType = type(kind);
        Value value = valueOf(context_, number, inputType);
        if (inputs_.size() <= id) {
            inputs_.resize(id + std::size_t{1});
        }
        inputs_[id] = {.value = value, .type = inputType, .source = source, .history = {}};
        value.setTerm(terms_.input(InputId{id}, value, formatOf(inputType)));
        return value;
    }

    /// The values input `id` was fixed to in earlier runs.
    void fixedBefore(std::uint32_t id, std::vector<Int128> numbers) {
        std::vector<Value>& history = histories_[id];
        for (const Int128 number : numbers) {
            history.push_back(valueOf(context_, number, inputs_[id].type));
        }
        inputs_[id].history = history;
    }

    Value constant(Int128 number, BuiltinKind kind = BuiltinKind::Int) { return valueOf(context_, number, type(kind)); }

    Value arithmetic(IntegerOperator op, const Value& lhs, const Value& rhs, value::IntegerFormat format = s32) {
        Value result = value::integerBinary(op, lhs, rhs, format).value;
        result.setTerm(terms_.integerBinary(op, lhs, rhs, format));
        return result;
    }

    Value floating(FloatOperator op, const Value& lhs, const Value& rhs) {
        Value result = value::floatBinary(op, lhs, rhs, value::FloatFormat::Double);
        result.setTerm(terms_.floatBinary(op, lhs, rhs, value::FloatFormat::Double));
        return result;
    }

    Value compare(Comparison op, const Value& lhs, const Value& rhs, value::ScalarFormat format = s32) {
        const auto* integer = std::get_if<value::IntegerFormat>(&format);
        const bool holds = integer != nullptr ? value::integerCompare(op, lhs, rhs, *integer)
                                              : value::floatCompare(op, lhs, rhs, std::get<value::FloatFormat>(format));
        Value result = Value::integer(holds ? 1 : 0, s32);
        result.setTerm(integer != nullptr ? terms_.integerCompare(op, lhs, rhs, *integer)
                                          : terms_.floatCompare(op, lhs, rhs, std::get<value::FloatFormat>(format)));
        return result;
    }

    /// `operand` of type `from` converted to `to`, as C converts it.
    Value converted(const Value& operand, BuiltinKind from, BuiltinKind to) {
        Value result = *convert(context_, operand, type(from), type(to));
        const value::ScalarFormat fromFormat = formatOf(type(from));
        const value::ScalarFormat toFormat = formatOf(type(to));
        const bool isFromInteger = std::holds_alternative<value::IntegerFormat>(fromFormat);
        const bool isToInteger = std::holds_alternative<value::IntegerFormat>(toFormat);
        if (to == BuiltinKind::Bool) {
            result.setTerm(terms_.toBool(operand, fromFormat));
        } else if (isFromInteger && isToInteger) {
            result.setTerm(terms_.convertInteger(operand, std::get<value::IntegerFormat>(fromFormat),
                                                 std::get<value::IntegerFormat>(toFormat)));
        } else if (isFromInteger) {
            result.setTerm(terms_.integerToFloat(operand, std::get<value::IntegerFormat>(fromFormat),
                                                 std::get<value::FloatFormat>(toFormat)));
        } else if (isToInteger) {
            result.setTerm(terms_.floatToInteger(operand, std::get<value::FloatFormat>(fromFormat),
                                                 std::get<value::IntegerFormat>(toFormat)));
        } else {
            result.setTerm(terms_.convertFloat(operand, std::get<value::FloatFormat>(fromFormat),
                                               std::get<value::FloatFormat>(toFormat)));
        }
        return result;
    }

    /// `lhs && rhs` or `lhs || rhs` as the run evaluates it: the right operand only if the left one does not decide.
    Value logical(bool isAnd, const Value& lhs, const Value& rhs) {
        const bool left = value::toBool(lhs).bits() != 0;
        const bool decides = left != isAnd;
        const Value* evaluated = decides ? nullptr : &rhs;
        const bool holds = decides ? left : value::toBool(rhs).bits() != 0;
        Value result = Value::integer(holds ? 1 : 0, s32);
        result.setTerm(isAnd ? terms_.logicalAnd(lhs, s32, evaluated, s32)
                             : terms_.logicalOr(lhs, s32, evaluated, s32));
        return result;
    }

    Value opaque() {
        Value result = constant(0);
        result.setTerm(terms_.opaque());
        return result;
    }

    static Atom holds(const Value& value) { return {.term = value.term(), .op = Comparison::NotEqual, .constant = 0}; }
    static Atom fails(const Value& value) { return {.term = value.term(), .op = Comparison::Equal, .constant = 0}; }
    static Atom atom(const Value& value, Comparison op, Int128 constant) {
        return {.term = value.term(), .op = op, .constant = constant};
    }

    SearchResult solve(const std::vector<Clause>& requirement, const std::vector<Clause>& kept = {}) {
        KeptClauses keptClauses(ValidationOptions{}.maxKeptAtoms);
        for (const Clause& clause : kept) {
            (void)keptClauses.keep(terms_, clause);
        }
        return search(context_, terms_, requirement, keptClauses, inputs_, limits_);
    }

    /// The changes as "input := value (method)", or "none".
    std::string changes(const std::vector<Clause>& requirement, const std::vector<Clause>& kept = {}) {
        const SearchResult result = solve(requirement, kept);
        std::string text;
        for (const Change& change : result.changes) {
            const SearchInput& changed = inputs_[std::to_underlying(change.input)];
            text += std::format("{}{} := {} ({})", text.empty() ? "" : ", ", std::to_underlying(change.input),
                                toString(context_, change.value, changed.type), nameOf(change.method));
        }
        return text.empty() ? "none" : text;
    }

    TargetInfo target_ = TargetInfo::forDataModel(DataModel::ILP32);
    StringInterner interner_;
    ast::AstContext context_{target_, interner_};
    value::TermTable terms_{DataModel::ILP32};
    std::vector<SearchInput> inputs_;
    std::map<std::uint32_t, std::vector<Value>> histories_;
    SearchLimits limits_;
};

// ---- Inversion ----

TEST_F(SearchTest, InvertsAdditionAndSubtraction) {
    const Value x = input(0, 0);
    EXPECT_EQ(changes({{atom(arithmetic(IntegerOperator::Add, x, constant(3)), Comparison::Equal, 10)}}),
              "0 := 7 (inversion)");
    EXPECT_EQ(changes({{atom(arithmetic(IntegerOperator::Sub, constant(10), x), Comparison::Equal, 4)}}),
              "0 := 6 (inversion)");
    EXPECT_EQ(changes({{atom(arithmetic(IntegerOperator::Sub, x, constant(10)), Comparison::Greater, 4)}}),
              "0 := 15 (inversion)");
}

TEST_F(SearchTest, AdditionWrapsAround) {
    const Value x = input(0, 0, InputSource::Default, BuiltinKind::UnsignedInt);
    const Value sum = arithmetic(IntegerOperator::Add, x, constant(5, BuiltinKind::UnsignedInt), u32);
    EXPECT_EQ(changes({{atom(sum, Comparison::Equal, 2)}}), "0 := 4294967293 (inversion)");
}

TEST_F(SearchTest, InvertsXorComplementAndNegation) {
    const Value x = input(0, 0);
    EXPECT_EQ(changes({{atom(arithmetic(IntegerOperator::Xor, x, constant(5)), Comparison::Equal, 1)}}),
              "0 := 4 (inversion)");
    Value complement = value::integerComplement(x, s32);
    complement.setTerm(terms_.integerComplement(x, s32));
    EXPECT_EQ(changes({{atom(complement, Comparison::Equal, -8)}}), "0 := 7 (inversion)");
    Value negated = value::integerNegate(x, s32).value;
    negated.setTerm(terms_.integerNegate(x, s32));
    EXPECT_EQ(changes({{atom(negated, Comparison::Equal, 3)}}), "0 := -3 (inversion)");
}

TEST_F(SearchTest, InvertsMultiplicationByAFactorThatDividesTheValue) {
    const Value x = input(0, 0);
    EXPECT_EQ(changes({{atom(arithmetic(IntegerOperator::Mul, x, constant(3)), Comparison::Equal, 21)}}),
              "0 := 7 (inversion)");
    EXPECT_EQ(changes({{atom(arithmetic(IntegerOperator::Mul, constant(-2), x), Comparison::Equal, 8)}}),
              "0 := -4 (inversion)");
    const Value linear =
        arithmetic(IntegerOperator::Add, arithmetic(IntegerOperator::Mul, x, constant(2)), constant(1));
    EXPECT_EQ(changes({{atom(linear, Comparison::Equal, 9)}}), "0 := 4 (inversion)");
    // A signed product that the factor does not divide is not inverted, and no candidate gives it.
    EXPECT_EQ(changes({{atom(arithmetic(IntegerOperator::Mul, x, constant(3)), Comparison::Equal, 22)}}), "none");
}

TEST_F(SearchTest, InvertsMultiplicationByAnOddFactorInAnUnsignedFormat) {
    const Value x = input(0, 0, InputSource::Default, BuiltinKind::UnsignedInt);
    const Value odd = arithmetic(IntegerOperator::Mul, x, constant(3, BuiltinKind::UnsignedInt), u32);
    EXPECT_EQ(changes({{atom(odd, Comparison::Equal, 22)}}), "0 := 2863311538 (inversion)"); // 3 * that wraps to 22
    // An even factor 2^k * odd needs k trailing zero bits in the value: 6 * x == 4 for x = 2 * 3^-1.
    const Value six = arithmetic(IntegerOperator::Mul, x, constant(6, BuiltinKind::UnsignedInt), u32);
    EXPECT_EQ(changes({{atom(six, Comparison::Equal, 4)}}), "0 := 1431655766 (inversion)");
    const Value even = arithmetic(IntegerOperator::Mul, x, constant(2, BuiltinKind::UnsignedInt), u32);
    EXPECT_EQ(changes({{atom(even, Comparison::Equal, 5)}}), "none");
}

TEST_F(SearchTest, InvertsDivisionByAFactor) {
    const Value x = input(0, 1);
    const Value quotient = arithmetic(IntegerOperator::Div, x, constant(4));
    EXPECT_EQ(changes({{atom(quotient, Comparison::Equal, 3)}}), "0 := 12 (inversion)");
    EXPECT_EQ(changes({{atom(quotient, Comparison::Greater, 2)}}), "0 := 12 (inversion)");
    // The divisor is not inverted: 12 / x == 3 for the candidate 4, the constant 3 + 1.
    EXPECT_EQ(changes({{atom(arithmetic(IntegerOperator::Div, constant(12), x), Comparison::Equal, 3)}}),
              "0 := 4 (candidate)");
}

TEST_F(SearchTest, InvertsFloatingAdditionWhereRoundingAllowsIt) {
    const Value d = input(0, 0, InputSource::Default, BuiltinKind::Double);
    const auto isEqual = [&](const Value& lhs, double rhs) {
        return holds(compare(Comparison::Equal, lhs, Value::ofDouble(rhs), value::FloatFormat::Double));
    };
    EXPECT_EQ(changes({{isEqual(floating(FloatOperator::Add, d, Value::ofDouble(0.5)), 2.0)}}), "0 := 1.5 (inversion)");
    EXPECT_EQ(changes({{isEqual(floating(FloatOperator::Sub, Value::ofDouble(1.0), d), 0.25)}}),
              "0 := 0.75 (inversion)");
    // 0.2 + 0.1 - 0.1 is not 0.2 in double, and no special value gives 0.2 either.
    EXPECT_EQ(changes({{isEqual(floating(FloatOperator::Sub, d, Value::ofDouble(0.1)), 0.2)}}), "none");
}

TEST_F(SearchTest, InvertsConversionsThatTruncate) {
    const Value x = input(0, 0);
    EXPECT_EQ(changes({{atom(converted(x, BuiltinKind::Int, BuiltinKind::SignedChar), Comparison::Equal, -1)}}),
              "0 := -1 (inversion)");
    EXPECT_EQ(changes({{atom(converted(x, BuiltinKind::Int, BuiltinKind::UnsignedChar), Comparison::Equal, 200)}}),
              "0 := 200 (inversion)");
}

TEST_F(SearchTest, AConversionCannotGiveAValueOutsideItsSourceType) {
    // char c; (int)c == 300 holds for no c.
    const Value c = input(0, 0, InputSource::Default, BuiltinKind::Char);
    EXPECT_EQ(changes({{atom(converted(c, BuiltinKind::Char, BuiltinKind::Int), Comparison::Equal, 300)}}), "none");
}

TEST_F(SearchTest, InvertsConversionsBetweenIntegersAndFloatingValues) {
    const Value d = input(0, 0, InputSource::Default, BuiltinKind::Double);
    EXPECT_EQ(changes({{atom(converted(d, BuiltinKind::Double, BuiltinKind::Int), Comparison::Equal, 3)}}),
              "0 := 3 (inversion)");
    const Value x = input(1, 0);
    const Value asDouble = converted(x, BuiltinKind::Int, BuiltinKind::Double);
    EXPECT_EQ(
        changes({{holds(compare(Comparison::Greater, asDouble, Value::ofDouble(2.5), value::FloatFormat::Double))}}),
        "1 := 3 (candidate)"); // 2.5000000000000004 does not come back from an int
    const Value f = input(2, 0, InputSource::Default, BuiltinKind::Float);
    EXPECT_EQ(changes({{holds(compare(Comparison::Equal, f, Value::ofFloat(27.0F), value::FloatFormat::Float))}}),
              "2 := 27 (inversion)");
}

TEST_F(SearchTest, InvertsAConversionToBool) {
    const Value x = input(0, 0);
    // (_Bool)x == 1 is no condition on a truth value; the conversion is inverted.
    EXPECT_EQ(changes({{atom(converted(x, BuiltinKind::Int, BuiltinKind::Bool), Comparison::Equal, 1)}}),
              "0 := 1 (inversion)");
}

// ---- Decomposition ----

TEST_F(SearchTest, AConditionOnAnInputIsSolvedForTheInput) {
    // README: `if (tmp)` after `tmp = __VERIFIER_nondet_int()`.
    const Value tmp = input(0, 0);
    EXPECT_EQ(changes({{holds(tmp)}}), "0 := 1 (inversion)");
    EXPECT_EQ(changes({{holds(converted(tmp, BuiltinKind::Int, BuiltinKind::Bool))}}), "0 := 1 (inversion)");
    Value notTmp = Value::integer(1, s32);
    notTmp.setTerm(terms_.logicalNot(tmp, s32));
    EXPECT_EQ(changes({{fails(notTmp)}}), "0 := 1 (inversion)");
}

TEST_F(SearchTest, AComparisonBecomesARelationWithTheSideWithoutInputsAsBound) {
    const Value x = input(0, 0);
    EXPECT_EQ(changes({{holds(compare(Comparison::Less, constant(3), x))}}), "0 := 4 (inversion)");
    // Its negation.
    EXPECT_EQ(changes({{fails(compare(Comparison::LessEqual, x, constant(4)))}}), "0 := 5 (inversion)");
}

TEST_F(SearchTest, OrTakesTheSecondDisjunctIfTheFirstCannotHold) {
    const Value y = input(0, 0, InputSource::Hinted);
    const Value x = input(1, 0);
    const Value either =
        logical(false, compare(Comparison::Equal, y, constant(7)), compare(Comparison::Equal, x, constant(3)));
    EXPECT_EQ(changes({{holds(either)}}), "1 := 3 (inversion)");
}

TEST_F(SearchTest, OrTakesTheFirstDisjunctThatCanHold) {
    const Value y = input(0, 0);
    const Value x = input(1, 0);
    const Value either =
        logical(false, compare(Comparison::Equal, y, constant(7)), compare(Comparison::Equal, x, constant(3)));
    EXPECT_EQ(changes({{holds(either)}}), "0 := 7 (inversion)");
}

TEST_F(SearchTest, AndWithSeveralInputs) {
    // x > 0 && y > x with x = 1, y = 0: the second conjunct fails, and y is the later input.
    const Value x = input(0, 1);
    const Value y = input(1, 0);
    const Value both = logical(true, compare(Comparison::Greater, x, constant(0)), compare(Comparison::Greater, y, x));
    EXPECT_EQ(changes({{holds(both)}}), "1 := 2 (inversion)");
    // Conjuncts of a constraint are clauses of their own; each gets its input.
    EXPECT_EQ(changes({{holds(compare(Comparison::Equal, x, constant(5)))},
                       {holds(compare(Comparison::Equal, constant(7), y))}}),
              "0 := 5 (inversion), 1 := 7 (inversion)");
}

TEST_F(SearchTest, AConjunctThatHoldsKeepsHoldingWhileTheNextIsSolved) {
    // x != 0 && x != 1 with x = 1: inverting the second conjunct gives 0, which breaks the first.
    const Value x = input(0, 1);
    const Value both =
        logical(true, compare(Comparison::NotEqual, x, constant(0)), compare(Comparison::NotEqual, x, constant(1)));
    EXPECT_EQ(changes({{holds(both)}}), "0 := 2 (candidate)");
}

TEST_F(SearchTest, AnOperandTheRunDidNotEvaluateIsUnknown) {
    // x == 1 && y == 2 with x = 0: the run did not evaluate y == 2, so making x == 1 is all the search can do.
    const Value x = input(0, 0);
    const Value y = input(1, 0);
    const Value both =
        logical(true, compare(Comparison::Equal, x, constant(1)), compare(Comparison::Equal, y, constant(2)));
    EXPECT_EQ(changes({{holds(both)}}), "0 := 1 (inversion)");
}

// ---- Candidates ----

TEST_F(SearchTest, CandidatesWhereNoOperationCanBeInverted) {
    const Value x = input(0, 0);
    const Value remainder = arithmetic(IntegerOperator::Rem, x, constant(10));
    // The run's value + 1 and - 1 come first, then 0, 1, -1, then the constant of the atom and those of the term.
    EXPECT_EQ(changes({{atom(remainder, Comparison::Equal, 1)}}), "0 := 1 (candidate)");
    EXPECT_EQ(changes({{atom(remainder, Comparison::Equal, -1)}}), "0 := -1 (candidate)");
    EXPECT_EQ(changes({{atom(remainder, Comparison::Equal, 3)}}), "0 := 3 (candidate)");
}

TEST_F(SearchTest, AnInputThatOccursTwiceIsNotInverted) {
    const Value x = input(0, 0);
    EXPECT_EQ(changes({{atom(arithmetic(IntegerOperator::Mul, x, x), Comparison::Equal, 49)}}), "none");
    EXPECT_EQ(changes({{atom(arithmetic(IntegerOperator::Mul, x, x), Comparison::Equal, 1)}}), "0 := 1 (candidate)");
}

TEST_F(SearchTest, DivisionByZeroRejectsACandidate) {
    // 10 / x == 10 with x = 5: the candidates are 6, 4, then 0, which ends the run, then 1.
    const Value x = input(0, 5);
    EXPECT_EQ(changes({{atom(arithmetic(IntegerOperator::Div, constant(10), x), Comparison::Equal, 10)}}),
              "0 := 1 (candidate)");
}

TEST_F(SearchTest, NanMakesAComparisonOfAnInputWithItselfFail) {
    // x == x false, as CPAchecker's nan_double needs it, in every floating format.
    const Value d = input(0, 0, InputSource::Default, BuiltinKind::Double);
    EXPECT_EQ(changes({{fails(compare(Comparison::Equal, d, d, value::FloatFormat::Double))}}), "0 := nan (candidate)");
    EXPECT_EQ(changes({{holds(compare(Comparison::NotEqual, d, d, value::FloatFormat::Double))}}),
              "0 := nan (candidate)");
    const Value f = input(1, 0, InputSource::Default, BuiltinKind::Float);
    EXPECT_EQ(changes({{fails(compare(Comparison::Equal, f, f, value::FloatFormat::Float))}}), "1 := nan (candidate)");
    const Value l = input(2, 0, InputSource::Default, BuiltinKind::LongDouble);
    EXPECT_EQ(changes({{fails(compare(Comparison::Equal, l, l, value::FloatFormat::LongDouble))}}),
              "2 := nan (candidate)");
}

TEST_F(SearchTest, NanSatisfiesTheNegationOfAnOrderedComparison) {
    // !(x < x + 1e-6), from neural-networks: NaN satisfies it, although not x >= x + 1e-6.
    const Value d = input(0, 0, InputSource::Default, BuiltinKind::Double);
    const Value larger = floating(FloatOperator::Add, d, Value::ofDouble(1e-6));
    const Clause notLess{fails(compare(Comparison::Less, d, larger, value::FloatFormat::Double))};
    EXPECT_EQ(changes({notLess}), "0 := nan (candidate)");
    // With NaN excluded, as by the program's own check x != x, an infinity.
    EXPECT_EQ(changes({notLess}, {{fails(compare(Comparison::NotEqual, d, d, value::FloatFormat::Double))}}),
              "0 := inf (candidate)");
    // Inversion takes the negated operator: !(x < 2.5) gives 2.5.
    EXPECT_EQ(changes({{fails(compare(Comparison::Less, d, Value::ofDouble(2.5), value::FloatFormat::Double))}}),
              "0 := 2.5 (inversion)");
}

TEST_F(SearchTest, NegativeZeroIsACandidate) {
    // 1.0 / x < -1e300 holds for -0.0 only, where the quotient is -inf.
    const Value d = input(0, 0, InputSource::Default, BuiltinKind::Double);
    const Value quotient = floating(FloatOperator::Div, Value::ofDouble(1.0), d);
    EXPECT_EQ(
        changes({{holds(compare(Comparison::Less, quotient, Value::ofDouble(-1e300), value::FloatFormat::Double))}}),
        "0 := -0 (candidate)");
}

TEST_F(SearchTest, IntegerExtremesSolveOverflowChecks) {
    // (x + 10) < x, from ldv-regression's test24-2, holds where + wraps around: at the maximum.
    const Value x = input(0, 0);
    EXPECT_EQ(changes({{holds(compare(Comparison::Less, arithmetic(IntegerOperator::Add, x, constant(10)), x))}}),
              "0 := 2147483647 (candidate)");
    // !((x - 1) < x) at the minimum.
    EXPECT_EQ(changes({{fails(compare(Comparison::Less, arithmetic(IntegerOperator::Sub, x, constant(1)), x))}}),
              "0 := -2147483648 (candidate)");
    // Their neighbours, where a kept clause excludes the extreme.
    EXPECT_EQ(changes({{holds(compare(Comparison::Less, arithmetic(IntegerOperator::Add, x, constant(2)), x))}},
                      {{atom(x, Comparison::NotEqual, 2147483647)}}),
              "0 := 2147483646 (candidate)");
    EXPECT_EQ(changes({{fails(compare(Comparison::Less, arithmetic(IntegerOperator::Sub, x, constant(2)), x))}},
                      {{atom(x, Comparison::NotEqual, -2147483648LL)}}),
              "0 := -2147483647 (candidate)");
    // The extremes of the input's format.
    constexpr value::IntegerFormat s64{.bits = 64, .isSigned = true};
    const Value y = input(1, 0, InputSource::Default, BuiltinKind::LongLong);
    const Value sum = arithmetic(IntegerOperator::Add, y, constant(10, BuiltinKind::LongLong), s64);
    EXPECT_EQ(changes({{holds(compare(Comparison::Less, sum, y, s64))}}), "1 := 9223372036854775807 (candidate)");
}

TEST_F(SearchTest, SpecialValuesComeAfterTheOtherCandidates) {
    // x * x == 1 holds for 1, -1 and the extremes of int (the square of 2^31 - 1 wraps around to 1).
    const Value x = input(0, 0);
    const Clause squareIsOne{atom(arithmetic(IntegerOperator::Mul, x, x), Comparison::Equal, 1)};
    EXPECT_EQ(changes({squareIsOne}), "0 := 1 (candidate)");
    EXPECT_EQ(changes({squareIsOne}, {{atom(x, Comparison::NotEqual, 1)}}), "0 := -1 (candidate)");
    EXPECT_EQ(changes({squareIsOne}, {{atom(x, Comparison::NotEqual, 1)}, {atom(x, Comparison::NotEqual, -1)}}),
              "0 := 2147483647 (candidate)");
    // Values of earlier runs come after them: (y + 10) < y with y fixed to the maximum before.
    const Value y = input(1, 0, InputSource::Fixed);
    fixedBefore(1, {2147483647});
    EXPECT_EQ(changes({{holds(compare(Comparison::Less, arithmetic(IntegerOperator::Add, y, constant(10)), y))}}),
              "1 := 2147483646 (candidate)");
}

// ---- Kept clauses ----

TEST_F(SearchTest, ClausesAreKeptOnceAndIndexedByTheirInputs) {
    const Value x = input(0, 0);
    const Value y = input(1, 0);
    const Clause onX{atom(x, Comparison::NotEqual, 1)};
    const Clause onBoth{holds(compare(Comparison::Equal, arithmetic(IntegerOperator::Add, x, y), constant(0)))};
    KeptClauses kept(ValidationOptions{}.maxKeptAtoms);
    EXPECT_EQ(kept.keep(terms_, onX), KeptClauses::Outcome::Kept);
    EXPECT_EQ(kept.keep(terms_, onX), KeptClauses::Outcome::Ignored);
    EXPECT_EQ(kept.keep(terms_, onBoth), KeptClauses::Outcome::Kept);
    // No input can make a clause false that has no atom on a term other than the opaque term.
    EXPECT_EQ(kept.keep(terms_, Clause{{.term = std::nullopt, .op = Comparison::NotEqual, .constant = 0}}),
              KeptClauses::Outcome::Ignored);
    EXPECT_EQ(kept.keep(terms_, Clause{holds(opaque())}), KeptClauses::Outcome::Ignored);
    ASSERT_EQ(kept.size(), 2u);
    EXPECT_EQ(kept[0].size(), 1u);
    EXPECT_EQ(kept[0][0].constant, 1);
    EXPECT_EQ(std::ranges::to<std::vector>(kept.mentioning(InputId{0})), (std::vector<std::uint32_t>{0, 1}));
    EXPECT_EQ(std::ranges::to<std::vector>(kept.mentioning(InputId{1})), std::vector<std::uint32_t>{1});
    EXPECT_TRUE(kept.mentioning(InputId{7}).empty());
    kept.clear();
    EXPECT_EQ(kept.size(), 0u);
    EXPECT_TRUE(kept.mentioning(InputId{0}).empty());
}

TEST_F(SearchTest, ClausesAreKeptUpToALimit) {
    const Value x = input(0, 0);
    KeptClauses kept(3); // atoms
    EXPECT_EQ(kept.keep(terms_, Clause{atom(x, Comparison::NotEqual, 1)}), KeptClauses::Outcome::Kept);
    EXPECT_EQ(kept.keep(terms_, Clause{atom(x, Comparison::Equal, 2), atom(x, Comparison::Equal, 3)}),
              KeptClauses::Outcome::Kept);
    EXPECT_EQ(kept.keep(terms_, Clause{atom(x, Comparison::NotEqual, 4)}), KeptClauses::Outcome::Full);
    EXPECT_EQ(kept.keep(terms_, Clause{atom(x, Comparison::NotEqual, 1)}), KeptClauses::Outcome::Ignored);
    EXPECT_EQ(kept.size(), 2u);
}

TEST_F(SearchTest, AKeptClauseBlocksAValue) {
    // x != 5 with x = 5 gives 0, unless a kept clause excludes it; then the run's value + 1.
    const Value x = input(0, 5);
    const Clause notFive{holds(compare(Comparison::NotEqual, x, constant(5)))};
    EXPECT_EQ(changes({notFive}), "0 := 0 (inversion)");
    EXPECT_EQ(changes({notFive}, {{holds(compare(Comparison::NotEqual, x, constant(0)))}}), "0 := 6 (candidate)");
    EXPECT_EQ(changes({notFive}, {{holds(compare(Comparison::Greater, x, constant(3)))},
                                  {holds(compare(Comparison::Less, x, constant(6)))}}),
              "0 := 4 (candidate)");
}

TEST_F(SearchTest, AKeptClauseThatBecomesUnknownDoesNotBlock) {
    // The kept clause !(x == 0 && y == 1) held with x = 1 without evaluating y == 1.
    const Value x = input(0, 1);
    const Value y = input(1, 0, InputSource::Hinted);
    const Value both =
        logical(true, compare(Comparison::Equal, x, constant(0)), compare(Comparison::Equal, y, constant(1)));
    EXPECT_EQ(changes({{holds(compare(Comparison::Equal, x, constant(0)))}}, {{fails(both)}}), "0 := 0 (inversion)");
}

TEST_F(SearchTest, AKeptClauseThatEndsTheRunBlocks) {
    const Value x = input(0, 5);
    const Clause kept{holds(arithmetic(IntegerOperator::Div, constant(10), x))};
    EXPECT_EQ(changes({{holds(compare(Comparison::NotEqual, x, constant(5)))}}, {kept}), "0 := 6 (candidate)");
}

TEST_F(SearchTest, KeptClausesWithoutTheChangedInputAreNotEvaluated) {
    // The kept clause on y is false; it does not concern x.
    const Value x = input(0, 0);
    const Value y = input(1, 3);
    EXPECT_EQ(changes({{holds(compare(Comparison::Equal, x, constant(2)))}}, {{fails(y)}}), "0 := 2 (inversion)");
}

TEST_F(SearchTest, EarlierClausesOfTheRequirementStay) {
    // The default of a switch with cases 0, 1 and 2, and x = 1: each case value is excluded in turn, and 3 is the
    // smallest non-negative value that is no case value.
    const Value x = input(0, 1);
    const std::vector<Clause> requirement{
        {atom(x, Comparison::NotEqual, 0)}, {atom(x, Comparison::NotEqual, 1)}, {atom(x, Comparison::NotEqual, 2)}};
    EXPECT_EQ(changes(requirement), "0 := 3 (candidate)");
}

TEST_F(SearchTest, NonNegativeCandidatesComeFirst) {
    // x != 0, then x != 1, with x = 0: the candidates are 1, 0, then 2 (the constant 1 + 1) before -1.
    const Value x = input(0, 0);
    EXPECT_EQ(changes({{atom(x, Comparison::NotEqual, 0)}, {atom(x, Comparison::NotEqual, 1)}}), "0 := 2 (candidate)");
    // A negative value is taken where no other one helps.
    EXPECT_EQ(changes({{atom(x, Comparison::Less, 0)}}), "0 := -1 (inversion)");
    EXPECT_EQ(changes({{atom(arithmetic(IntegerOperator::Rem, x, constant(10)), Comparison::Equal, -1)}}),
              "0 := -1 (candidate)");
}

TEST_F(SearchTest, AClauseThatBlocksAValueGivesCandidates) {
    // x != 0 with x = 0 gives 1, which the kept clause x != 1 excludes; its constant 1 + 1 is a candidate.
    const Value x = input(0, 0);
    EXPECT_EQ(changes({{atom(x, Comparison::NotEqual, 0)}}, {{atom(x, Comparison::NotEqual, 1)}}),
              "0 := 2 (candidate)");
}

TEST_F(SearchTest, AKeptClauseIsRepairedThroughAnotherInput) {
    // x + y == 10 with y = 10 is kept; y == 3 breaks it, and x := 7 repairs it.
    const Value x = input(0, 0);
    const Value y = input(1, 10, InputSource::Fixed);
    const Value sum = arithmetic(IntegerOperator::Add, x, y);
    const std::vector<Clause> kept{{holds(compare(Comparison::Equal, sum, constant(10)))}};
    EXPECT_EQ(changes({{holds(compare(Comparison::Equal, y, constant(3)))}}, kept),
              "1 := 3 (inversion), 0 := 7 (inversion)");
}

TEST_F(SearchTest, ARepairKeepsTheOtherClausesAndDoesNotRepairInTurn) {
    const Value x = input(0, 0, InputSource::Fixed);
    const Value y = input(1, 10, InputSource::Fixed);
    const Value z = input(2, 0, InputSource::Hinted);
    const Value sum = arithmetic(IntegerOperator::Add, x, y);
    const Clause sumIsTen{holds(compare(Comparison::Equal, sum, constant(10)))};
    const Clause needsThree{holds(compare(Comparison::Equal, y, constant(3)))};
    // x + z == 0 holds and mentions x only among the changeable inputs: x cannot become 7.
    const Clause xIsZero{holds(compare(Comparison::Equal, arithmetic(IntegerOperator::Add, x, z), constant(0)))};
    EXPECT_EQ(changes({needsThree}, {sumIsTen, xIsZero}), "none");
    // Two kept clauses that y == 3 breaks are not repaired.
    const Clause yIsTen{holds(compare(Comparison::Equal, y, constant(10)))};
    EXPECT_EQ(changes({needsThree}, {sumIsTen, yIsTen}), "none");
}

// ---- Unknown atoms ----

TEST_F(SearchTest, OpaqueAndTermlessAtomsAreUnknown) {
    const Value x = input(0, 0);
    const Atom termless{.term = std::nullopt, .op = Comparison::NotEqual, .constant = 0};
    EXPECT_EQ(changes({{holds(opaque())}}), "none");
    EXPECT_EQ(changes({{termless}}), "none");
    // No input is changed for them, but other atoms and clauses are solved.
    const Atom isThree = atom(x, Comparison::Equal, 3);
    EXPECT_EQ(changes({{fails(opaque()), isThree}}), "0 := 3 (inversion)");
    EXPECT_EQ(changes({{termless, isThree}}), "0 := 3 (inversion)");
    EXPECT_EQ(changes({{holds(opaque())}, {isThree}}), "0 := 3 (inversion)");
}

TEST_F(SearchTest, HintedInputsAreNotChanged) {
    const Value x = input(0, 0, InputSource::Hinted);
    EXPECT_EQ(changes({{atom(x, Comparison::Equal, 5)}}), "none");
}

TEST_F(SearchTest, AValueMustBeOneOfTheInputsType) {
    // A _Bool is 0 or 1.
    const Value b = input(0, 0, InputSource::Default, BuiltinKind::Bool);
    const Value promoted = converted(b, BuiltinKind::Bool, BuiltinKind::Int);
    EXPECT_EQ(changes({{atom(promoted, Comparison::Equal, 2)}}), "none");
    EXPECT_EQ(changes({{atom(promoted, Comparison::Equal, 1)}}), "0 := 1 (inversion)");
}

TEST_F(SearchTest, ARequirementThatHoldsChangesNothing) {
    const Value x = input(0, 4);
    EXPECT_EQ(changes({{atom(x, Comparison::Equal, 4)}}), "none");
}

// ---- Avoid waypoints ----

TEST_F(SearchTest, AvoidedOutcomesOfABranching) {
    // An avoid waypoint `true` at `if (x == 0)` that passes with x = 0.
    const Value x = input(0, 0);
    EXPECT_EQ(changes({{fails(compare(Comparison::Equal, x, constant(0)))}}), "0 := 1 (inversion)");
}

TEST_F(SearchTest, AnAvoidedConstraintNeedsOneConjunctToFail) {
    // `y == 1 && x == 2` with the hinted y = 1 and x = 2: only x can make it fail.
    const Value y = input(0, 1, InputSource::Hinted);
    const Value x = input(1, 2);
    EXPECT_EQ(changes({{fails(compare(Comparison::Equal, y, constant(1))),
                        fails(compare(Comparison::Equal, x, constant(2)))}}),
              "1 := 0 (inversion)");
}

TEST_F(SearchTest, AnAvoidedDefaultTakesACaseValueThatNoKeptClauseExcludes) {
    // Cases 1 and 2; an avoid waypoint at case 1 that did not pass keeps x != 1.
    const Value x = input(0, 0);
    const Clause someCase{atom(x, Comparison::Equal, 1), atom(x, Comparison::Equal, 2)};
    EXPECT_EQ(changes({someCase}), "0 := 1 (inversion)");
    EXPECT_EQ(changes({someCase}, {{atom(x, Comparison::NotEqual, 1)}}), "0 := 2 (inversion)");
}

// ---- Order ----

TEST_F(SearchTest, ValuesTheInputWasFixedToBeforeComeLast) {
    const Value x = input(0, 5, InputSource::Fixed);
    fixedBefore(0, {0, 5});
    EXPECT_EQ(changes({{atom(x, Comparison::NotEqual, 5)}}), "0 := 6 (candidate)");
    // They are still taken if nothing else helps.
    EXPECT_EQ(changes({{atom(x, Comparison::Equal, 0)}}), "0 := 0 (inversion)");
}

TEST_F(SearchTest, AValueTriedBeforeIsAvoidedInFavourOfANewOne) {
    // x was 1, 0, then 1 again; the kept `x == 1 || ok` holds, unknown for x != 1 since the run did not evaluate
    // `ok`. x != 1 gives 0, tried before, so the next candidate 2.
    const Value x = input(0, 1, InputSource::Fixed);
    fixedBefore(0, {1, 0});
    const Value either = logical(false, compare(Comparison::Equal, x, constant(1)), constant(0));
    EXPECT_EQ(changes({{atom(x, Comparison::NotEqual, 1)}}, {{holds(either)}}), "0 := 2 (candidate)");
}

TEST_F(SearchTest, TheWholeRequirementIsSolvedWithoutValuesOfEarlierRunsFirst) {
    // x == 1 || x == 5 with x = 0, which was 1 before: the first disjunct would give 1 again.
    const Value x = input(0, 0, InputSource::Fixed);
    fixedBefore(0, {1, 0});
    const Value either =
        logical(false, compare(Comparison::Equal, x, constant(1)), compare(Comparison::Equal, x, constant(5)));
    EXPECT_EQ(changes({{holds(either)}}), "0 := 5 (inversion)");
}

TEST_F(SearchTest, DefaultInputsComeFirstAndLaterRequestsBeforeEarlierOnes) {
    const Value x = input(0, 0);
    const Value y = input(1, 0);
    const Value fixed = input(2, 0, InputSource::Fixed);
    const Value fixedLater = input(3, 0, InputSource::Fixed);
    EXPECT_EQ(changes({{atom(arithmetic(IntegerOperator::Add, x, y), Comparison::Equal, 5)}}), "1 := 5 (inversion)");
    EXPECT_EQ(changes({{atom(arithmetic(IntegerOperator::Add, fixedLater, x), Comparison::Equal, 5)}}),
              "0 := 5 (inversion)");
    EXPECT_EQ(changes({{atom(arithmetic(IntegerOperator::Add, fixed, fixedLater), Comparison::Equal, 5)}}),
              "3 := 5 (inversion)");
}

TEST_F(SearchTest, PointerInputsAreNotTriedInPlaceOfOthers) {
    // The sum of an int and 8 later pointer inputs: the pointers would take all the places of the inputs tried.
    const Value x = input(0, 0);
    Value sum = x;
    for (std::uint32_t id = 1; id <= 8; ++id) {
        const ast::QualType pointer = context_.pointerTo(type(BuiltinKind::Char));
        const value::IntegerFormat format = sema::integerFormat(context_, pointer);
        Value p = Value::integer(0, format);
        inputs_.push_back({.value = p, .type = pointer, .source = InputSource::Default, .history = {}});
        p.setTerm(terms_.input(InputId{id}, p, format));
        sum = arithmetic(IntegerOperator::Add, sum, converted(p, BuiltinKind::UnsignedInt, BuiltinKind::Int));
    }
    EXPECT_EQ(changes({{atom(sum, Comparison::Equal, 5)}}), "0 := 5 (inversion)");
}

// ---- Shared subterms and `?:` ----

TEST_F(SearchTest, SharedSubtermsAreDecomposedOnce) {
    // c = c || c, 64 times, on x == 1 with a hinted x: every path fails, and there are 2^64 of them.
    const Value x = input(0, 0, InputSource::Hinted);
    Value c = compare(Comparison::Equal, x, constant(1));
    for (int i = 0; i < 63; ++i) {
        Value next = Value::integer(0, s32);
        next.setTerm(terms_.logicalOr(c, s32, &c, s32));
        c = next;
    }
    EXPECT_EQ(terms_.term(*c.term()).kind, TermKind::LogicalOr);
    EXPECT_EQ(changes({{holds(c)}}), "none");
}

TEST_F(SearchTest, AGoalThatFailedUnderAGuardIsTriedWithoutIt) {
    // (x > 5 && g) || g with g = x == 3 and x = 6: g fails while x > 5 must hold, and holds alone.
    const Value x = input(0, 6);
    const Value g = compare(Comparison::Equal, x, constant(3));
    const Value shared = logical(false, logical(true, compare(Comparison::Greater, x, constant(5)), g), g);
    EXPECT_EQ(changes({{holds(shared)}}), "0 := 3 (inversion)");
}

TEST_F(SearchTest, APassDecomposesALimitedNumberOfGoals) {
    // x == 1 || x == 2 || ... || x == 31 || y == 7 with a hinted x: y == 7 is the 63rd goal.
    const Value x = input(0, 0, InputSource::Hinted);
    const Value y = input(1, 0);
    Value either = compare(Comparison::Equal, x, constant(1));
    for (int k = 2; k <= 31; ++k) {
        either = logical(false, either, compare(Comparison::Equal, x, constant(k)));
    }
    either = logical(false, either, compare(Comparison::Equal, y, constant(7)));
    EXPECT_EQ(changes({{holds(either)}}), "1 := 7 (inversion)");
    limits_.goals = 62;
    EXPECT_EQ(changes({{holds(either)}}), "none");
}

TEST_F(SearchTest, TheSearchStopsAfterItsEvaluations) {
    // Each value of x breaks a kept clause whose repair tries values of y, each checked against the kept clauses of y.
    const Value x = input(0, 0);
    const Value y = input(1, 10);
    std::vector<Clause> kept{{atom(x, Comparison::Equal, 0), atom(y, Comparison::NotEqual, 10)}};
    for (int k = 0; k < 100; ++k) {
        kept.push_back({holds(compare(Comparison::LessEqual, y, constant(1000 + k)))});
    }
    kept.push_back({atom(y, Comparison::Equal, 10)});
    EXPECT_EQ(solve({{atom(x, Comparison::NotEqual, 0)}}, kept).explanation, "needs `in0 != 0`: no input values found");
    limits_.evaluations = 1000;
    EXPECT_EQ(solve({{atom(x, Comparison::NotEqual, 0)}}, kept).explanation,
              "needs `in0 != 0`: the search stopped after 1000 evaluations");
}

TEST_F(SearchTest, AConditionalChangesItsConditionToTheOperandTheRunDidNotEvaluate) {
    // a = x > 3 ? 1 : 0 with x = 0 selected 0; a != 0 needs the other operand, whose value the next run computes.
    const Value x = input(0, 0);
    const Value greater = compare(Comparison::Greater, x, constant(3));
    Value a = constant(0);
    a.setTerm(terms_.conditional(greater, s32, constant(0), s32));
    EXPECT_EQ(changes({{holds(a)}}), "0 := 4 (inversion)");
    // Through the operand the run evaluated, the condition keeps its value: b = y > 0 ? z : 0 with y = 1.
    const Value y = input(1, 1);
    const Value z = input(2, 0);
    Value b = z;
    b.setTerm(terms_.conditional(compare(Comparison::Greater, y, constant(0)), s32, z, s32));
    EXPECT_EQ(changes({{atom(b, Comparison::Equal, 5)}}), "2 := 5 (inversion)");
}

// ---- The README's examples ----

TEST_F(SearchTest, TheInputCheckOfRersFollowedByAComparisonWithTheInput) {
    // if ((input != 1) && (input != 2) && (input != 3)) return -2; ... if (input == 3) ...
    const auto check = [&](const Value& value) {
        const Value first = logical(true, compare(Comparison::NotEqual, value, constant(1)),
                                    compare(Comparison::NotEqual, value, constant(2)));
        return logical(true, first, compare(Comparison::NotEqual, value, constant(3)));
    };
    // Run 1: input = 0 passes the check, which the witness wants to fail.
    const Value input0 = input(0, 0);
    EXPECT_EQ(changes({{fails(check(input0))}}), "0 := 1 (inversion)");

    // Run 2: input = 1 fails the check, kept; then input == 3 is needed.
    terms_.clear();
    inputs_.clear();
    const Value input1 = input(0, 1);
    const std::vector<Clause> kept{{fails(check(input1))}};
    EXPECT_EQ(changes({{holds(compare(Comparison::Equal, input1, constant(3)))}}, kept), "0 := 3 (inversion)");
}

TEST_F(SearchTest, ALoopCounterBoundedByAnInput) {
    // while (i <= a) i++; with a = 100: the witness passes the loop condition 33 times, then needs it false.
    const Value a = input(0, 100);
    std::vector<Clause> kept;
    for (int i = 0; i <= 32; ++i) {
        kept.push_back({holds(compare(Comparison::LessEqual, constant(i), a))});
    }
    EXPECT_EQ(changes({{fails(compare(Comparison::LessEqual, constant(33), a))}}, kept), "0 := 32 (inversion)");
}

// ---- The explanation ----

TEST_F(SearchTest, TheExplanationSaysWhatWasNeededAndChosen) {
    const Value x = input(0, 0);
    const Value y = input(1, 0, InputSource::Hinted);
    const Value sum = arithmetic(IntegerOperator::Add, x, constant(3));
    EXPECT_EQ(solve({{holds(compare(Comparison::Equal, sum, constant(5)))}}).explanation,
              "needs `(in0 + 3) == 5`; input 0 := 2 (inversion)");
    EXPECT_EQ(solve({{fails(compare(Comparison::Equal, y, constant(0)))}}).explanation,
              "needs `!(in1 == 0)`: no input values found");
    EXPECT_EQ(solve({{atom(converted(x, BuiltinKind::Int, BuiltinKind::UnsignedChar), Comparison::Equal, 200),
                      atom(opaque(), Comparison::Equal, 1)}})
                  .explanation,
              "needs `(unsigned char)in0 == 200 || <opaque> == 1`; input 0 := 200 (inversion)");
    // An avoid function_enter that passes has no requirement.
    EXPECT_EQ(solve({}).explanation, "needs another path, which the terms do not describe: no input values found");
    EXPECT_EQ(solve({{atom(x, Comparison::Equal, 0)}}).explanation,
              "needs another path, which the terms do not describe: no input values found");
}

} // namespace
} // namespace nitwit::validate
