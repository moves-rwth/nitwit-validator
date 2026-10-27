// The input oracle (oracle.hpp): fixed inputs, then hints, then defaults (architecture.md, "Validation").

#include "validate/oracle.hpp"

#include <string>

#include <gtest/gtest.h>

#include "validate/values.hpp"
#include "validate_test_support.hpp"

namespace nitwit::validate::test {
namespace {

constexpr std::string_view source = R"(extern int __VERIFIER_nondet_int(void);
extern double __VERIFIER_nondet_double(void);
extern char *__VERIFIER_nondet_pointer(void);
extern _Bool __VERIFIER_nondet_bool(void);
int main(void) {
    int a = __VERIFIER_nondet_int();
    double b = __VERIFIER_nondet_double();
    char *c = __VERIFIER_nondet_pointer();
    _Bool d = __VERIFIER_nondet_bool();
    return 0;
}
)";

class OracleTest : public ValidateTest {
protected:
    void SetUp() override { program(std::string(source)); }

    /// The request of the call in `line` (column of its `)`), numbered `id` in the run.
    interp::InputRequest requestAt(std::uint32_t line, std::uint32_t column, std::uint32_t id,
                                   std::uint32_t occurrence = 0) {
        const ast::CallExpr* call = callAt(line, column);
        return {.kind = interp::InputRequest::Kind::NondetCall,
                .id = value::InputId{id},
                .site = call,
                .occurrence = occurrence,
                .type = call->type(),
                .function = call->directCallee()};
    }

    std::string provided(WitnessOracle& oracle, const interp::InputRequest& request) {
        return toString(context(), oracle.provide(request), request.type);
    }
};

TEST_F(OracleTest, DefaultsAreZeroInTheRequestedType) {
    WitnessOracle oracle(context());
    EXPECT_EQ(provided(oracle, requestAt(6, 35, 0)), "0");
    const value::Value b = oracle.provide(requestAt(7, 41, 1));
    ASSERT_EQ(b.kind(), value::Value::Kind::Double);
    EXPECT_EQ(b.asDouble(), 0.0);
    EXPECT_EQ(provided(oracle, requestAt(8, 41, 2)), "0x0");
    EXPECT_EQ(provided(oracle, requestAt(9, 38, 3)), "0");
}

TEST_F(OracleTest, TheDefaultIsConfigurable) {
    WitnessOracle oracle(context(), 7);
    EXPECT_EQ(provided(oracle, requestAt(6, 35, 0)), "7");
    EXPECT_EQ(provided(oracle, requestAt(7, 41, 1)), "7");
    EXPECT_EQ(provided(oracle, requestAt(9, 38, 2)), "1");
}

TEST_F(OracleTest, AHintAnswersTheNextRequestAtItsCall) {
    WitnessOracle oracle(context());
    oracle.hint(callAt(6, 35), valueOf(context(), 5, context().builtin(ast::BuiltinKind::Int)));
    EXPECT_EQ(provided(oracle, requestAt(6, 35, 0)), "5");
    EXPECT_EQ(oracle.input(value::InputId{0}).source, InputSource::Hinted);
    EXPECT_EQ(provided(oracle, requestAt(6, 35, 1, 1)), "0"); // used up
    EXPECT_EQ(oracle.input(value::InputId{1}).source, InputSource::Default);
}

TEST_F(OracleTest, AHintIsNotUsedAtOtherCalls) {
    WitnessOracle oracle(context());
    oracle.hint(callAt(6, 35), valueOf(context(), 5, context().builtin(ast::BuiltinKind::Int)));
    EXPECT_EQ(provided(oracle, requestAt(9, 38, 0)), "0");
    oracle.clearHint();
    EXPECT_EQ(provided(oracle, requestAt(6, 35, 1)), "0");
}

TEST_F(OracleTest, FixedInputsAreKeyedBySiteAndOccurrence) {
    WitnessOracle oracle(context());
    oracle.fix({.site = callAt(6, 35), .occurrence = 1},
               valueOf(context(), 9, context().builtin(ast::BuiltinKind::Int)));
    EXPECT_EQ(provided(oracle, requestAt(6, 35, 0, 0)), "0");
    EXPECT_EQ(provided(oracle, requestAt(6, 35, 1, 1)), "9");
    EXPECT_EQ(oracle.input(value::InputId{1}).source, InputSource::Fixed);
    EXPECT_EQ(oracle.input(value::InputId{1}).key.occurrence, 1u);
}

TEST_F(OracleTest, AFixedInputMayBeFixedAgainAndKeepsItsHistory) {
    WitnessOracle oracle(context());
    const ast::QualType intType = context().builtin(ast::BuiltinKind::Int);
    const InputKey key{.site = callAt(6, 35), .occurrence = 0};
    EXPECT_TRUE(oracle.fixedValues(key).empty());
    oracle.fix(key, valueOf(context(), 9, intType));
    oracle.fix(key, valueOf(context(), 4, intType));
    EXPECT_EQ(provided(oracle, requestAt(6, 35, 0)), "4");
    ASSERT_EQ(oracle.fixedValues(key).size(), 2u);
    EXPECT_EQ(toString(context(), oracle.fixedValues(key)[0], intType), "9");
}

TEST_F(OracleTest, ARunIsRepeatedWithTheSameFixedValuesAndNoNewOnes) {
    WitnessOracle oracle(context());
    const auto number = [&](Int128 n) {
        return valueOf(context(), n, context().builtin(ast::BuiltinKind::Int));
    };
    const InputKey first{.site = callAt(6, 35), .occurrence = 0};
    const InputKey second{.site = callAt(6, 35), .occurrence = 1};
    oracle.beginRun(); // run 1: nothing fixed
    oracle.fix(first, number(1));
    EXPECT_EQ(oracle.repeatedRun(), std::nullopt);
    oracle.beginRun(); // run 2: first = 1
    oracle.fix(first, number(2));
    oracle.beginRun(); // run 3: first = 2, with the value 2 new since run 2
    oracle.fix(first, number(1));
    EXPECT_EQ(oracle.repeatedRun(), std::nullopt); // as run 2, but the search after it knows the value 2 now
    oracle.beginRun();                             // run 4: first = 1
    oracle.fix(first, number(2));
    EXPECT_EQ(oracle.repeatedRun(), 3u);
    // Run 3 had no fixed value for the second request.
    oracle.fix(second, number(0));
    EXPECT_EQ(oracle.repeatedRun(), std::nullopt);
    EXPECT_EQ(oracle.fixedValues(first).size(), 2u); // each value once
}

TEST_F(OracleTest, OnlyRunsWithTheCurrentDefaultAreRepeated) {
    WitnessOracle oracle(context());
    const InputKey first{.site = callAt(6, 35), .occurrence = 0};
    oracle.beginRun(); // run 1: nothing fixed
    oracle.fix(first, valueOf(context(), 1, context().builtin(ast::BuiltinKind::Int)));
    oracle.beginRun(); // run 2: first = 1
    EXPECT_EQ(oracle.repeatedRun(), 2u);
    oracle.setDefault(1);
    EXPECT_EQ(oracle.repeatedRun(), std::nullopt); // run 2 had the default 0
    oracle.beginRun();                             // run 3: first = 1, the default 1
    EXPECT_EQ(oracle.repeatedRun(), 3u);
}

TEST_F(OracleTest, RunsWhoseFingerprintsAgreeAreComparedExactly) {
    // The fingerprint sums the hashes of the values: swapping the values of two requests keeps it.
    WitnessOracle oracle(context());
    const auto number = [&](Int128 n) {
        return valueOf(context(), n, context().builtin(ast::BuiltinKind::Int));
    };
    const InputKey first{.site = callAt(6, 35), .occurrence = 0};
    const InputKey second{.site = callAt(6, 35), .occurrence = 1};
    oracle.beginRun(); // run 1: nothing fixed
    oracle.fix(first, number(1));
    oracle.fix(second, number(2));
    oracle.beginRun(); // run 2: 1 and 2
    oracle.fix(first, number(2));
    oracle.fix(second, number(1));
    oracle.beginRun(); // run 3: 2 and 1, each request fixed to both values by now
    oracle.fix(first, number(1));
    oracle.fix(second, number(2));
    EXPECT_EQ(oracle.repeatedRun(), std::nullopt); // run 3 agrees in fingerprint and values tried, not in values
    oracle.beginRun();                             // run 4: 1 and 2
    oracle.fix(first, number(2));
    oracle.fix(second, number(1));
    EXPECT_EQ(oracle.repeatedRun(), 3u);
}

TEST_F(OracleTest, TheRunKeepsTheAnswers) {
    WitnessOracle oracle(context(), 3);
    oracle.hint(callAt(9, 38), valueOf(context(), 1, context().builtin(ast::BuiltinKind::Bool)));
    (void)oracle.provide(requestAt(6, 35, 0));
    (void)oracle.provide(requestAt(9, 38, 1));
    EXPECT_EQ(oracle.inputCount(), 2u);
    EXPECT_EQ(toString(context(), oracle.input(value::InputId{0}).value, context().builtin(ast::BuiltinKind::Int)),
              "3");
    EXPECT_EQ(oracle.input(value::InputId{1}).value.bits(), 1u);
}

TEST_F(OracleTest, RequestsBeyondTheRecordedOnesAreAnsweredButNotKept) {
    WitnessOracle oracle(context(), 3, 2);
    oracle.fix({.site = callAt(6, 35), .occurrence = 2},
               valueOf(context(), 9, context().builtin(ast::BuiltinKind::Int)));
    EXPECT_EQ(provided(oracle, requestAt(6, 35, 0, 0)), "3");
    EXPECT_EQ(provided(oracle, requestAt(6, 35, 1, 1)), "3");
    EXPECT_EQ(provided(oracle, requestAt(6, 35, 2, 2)), "9");
    EXPECT_EQ(provided(oracle, requestAt(6, 35, 3, 3)), "3");
    EXPECT_EQ(oracle.inputCount(), 2u);
    EXPECT_EQ(oracle.requestCount(), 4u);
    oracle.beginRun();
    EXPECT_EQ(provided(oracle, requestAt(6, 35, 0, 0)), "3");
    EXPECT_EQ(oracle.inputCount(), 1u);
    EXPECT_EQ(oracle.requestCount(), 1u);
}

TEST_F(OracleTest, TheRecordedRequestsAreTakenWithoutACopy) {
    WitnessOracle oracle(context(), 3);
    EXPECT_EQ(provided(oracle, requestAt(6, 35, 0)), "3");
    EXPECT_EQ(provided(oracle, requestAt(9, 38, 1)), "1");
    const std::vector<ProvidedInput> taken = oracle.takeInputs();
    ASSERT_EQ(taken.size(), 2u);
    EXPECT_EQ(taken[0].key.site, callAt(6, 35));
    EXPECT_EQ(taken[1].key.site, callAt(9, 38));
    EXPECT_EQ(oracle.inputCount(), 0u);
    EXPECT_EQ(oracle.requestCount(), 2u);
}

TEST_F(OracleTest, WithRequiredHintsTheFirstRequestThatNoHintAnswersIsUngiven) {
    WitnessOracle oracle(context(), 3);
    oracle.requireHints();
    oracle.hint(callAt(6, 35), valueOf(context(), 5, context().builtin(ast::BuiltinKind::Int)));
    EXPECT_EQ(provided(oracle, requestAt(6, 35, 0)), "5");
    EXPECT_EQ(oracle.firstUngiven(), std::nullopt);
    EXPECT_EQ(provided(oracle, requestAt(8, 41, 1)), "0x0"); // still answered, so that the run reaches the monitor
    EXPECT_EQ(provided(oracle, requestAt(9, 38, 2)), "1");
    ASSERT_TRUE(oracle.firstUngiven().has_value());
    EXPECT_EQ(oracle.firstUngiven()->first, value::InputId{1});
    EXPECT_EQ(oracle.firstUngiven()->second.key.site, callAt(8, 41));
    oracle.beginRun();
    EXPECT_EQ(oracle.firstUngiven(), std::nullopt);
}

TEST_F(OracleTest, WithRequiredHintsAFixedInputIsUngiven) {
    WitnessOracle oracle(context());
    oracle.requireHints();
    oracle.fix({.site = callAt(6, 35), .occurrence = 0},
               valueOf(context(), 9, context().builtin(ast::BuiltinKind::Int)));
    EXPECT_EQ(provided(oracle, requestAt(6, 35, 0)), "9");
    EXPECT_TRUE(oracle.firstUngiven().has_value());
}

TEST_F(OracleTest, FixedInputsWinOverHints) {
    WitnessOracle oracle(context());
    const ast::QualType intType = context().builtin(ast::BuiltinKind::Int);
    oracle.fix({.site = callAt(6, 35), .occurrence = 0}, valueOf(context(), 9, intType));
    oracle.hint(callAt(6, 35), valueOf(context(), 5, intType));
    EXPECT_EQ(provided(oracle, requestAt(6, 35, 0)), "9");
}

TEST_F(OracleTest, ANewRunForgetsRequestsAndHintsButKeepsFixedInputs) {
    WitnessOracle oracle(context());
    const ast::QualType intType = context().builtin(ast::BuiltinKind::Int);
    oracle.fix({.site = callAt(6, 35), .occurrence = 0}, valueOf(context(), 9, intType));
    oracle.hint(callAt(9, 38), valueOf(context(), 1, context().builtin(ast::BuiltinKind::Bool)));
    EXPECT_EQ(provided(oracle, requestAt(6, 35, 0)), "9");
    oracle.beginRun();
    EXPECT_EQ(provided(oracle, requestAt(9, 38, 0)), "0");
    EXPECT_EQ(provided(oracle, requestAt(6, 35, 1)), "9");
}

} // namespace
} // namespace nitwit::validate::test
