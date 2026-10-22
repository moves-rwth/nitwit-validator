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

TEST_F(OracleTest, ARunWithTheFixedValuesOfAnEarlierRunIsFound) {
    WitnessOracle oracle(context());
    const ast::QualType intType = context().builtin(ast::BuiltinKind::Int);
    const InputKey first{.site = callAt(6, 35), .occurrence = 0};
    const InputKey second{.site = callAt(6, 35), .occurrence = 1};
    oracle.beginRun(); // run 1: nothing fixed
    oracle.fix(first, valueOf(context(), 1, intType));
    EXPECT_EQ(oracle.repeatedRun(), std::nullopt);
    oracle.beginRun(); // run 2: first = 1
    oracle.fix(first, valueOf(context(), 2, intType));
    EXPECT_EQ(oracle.repeatedRun(), std::nullopt);
    oracle.beginRun(); // run 3: first = 2
    oracle.fix(first, valueOf(context(), 1, intType));
    EXPECT_EQ(oracle.repeatedRun(), 2u);
    // Run 2 had no fixed value for the second request.
    oracle.fix(second, valueOf(context(), 0, intType));
    EXPECT_EQ(oracle.repeatedRun(), std::nullopt);
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
