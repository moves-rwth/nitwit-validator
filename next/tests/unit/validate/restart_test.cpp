// Values from the witness (validate/README.md, "Values"): hints of function_return waypoints, restarts that fix the
// inputs a failed follow constraint asks for (restart.hpp), and the one more run with other defaults.

#include "validate/restart.hpp"

#include <cstddef>
#include <new>
#include <string>

#include <gtest/gtest.h>

#include "test_programs.hpp"

namespace nitwit::validate::test {
namespace {

class RestartTest : public ValidateTest {};

// ---- Hints ----

TEST_F(RestartTest, AFollowHintSatisfiesTheConstraint) {
    program(std::string(inputsProgram));
    const ValidationResult result = validate(sequence({{follow(WaypointType::FunctionReturn, 4, 35, "\\result == 5")},
                                                       {follow(WaypointType::Branching, 6, 5, "true")},
                                                       {target(7, 9)}}),
                                             [](FakeExecution& x) { return runInputs(x); });
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message;
    EXPECT_EQ(result.runs, 1u);
}

constexpr std::string_view relationsProgram = R"(extern int __VERIFIER_nondet_int(void);
extern unsigned char __VERIFIER_nondet_uchar(void);
void reach_error() {}

int main() {
    int a = __VERIFIER_nondet_int();
    int b = __VERIFIER_nondet_int(); int c = __VERIFIER_nondet_int();
    unsigned char d = __VERIFIER_nondet_uchar();
    if (a > 5 && b != 0 && c < -3 && d == 255) {
        reach_error();
    }
    return 0;
}
)";

interp::RunResult runRelations(FakeExecution& x) {
    x.statement(6, 5);
    x.assign("a", x.nondet(6, 35));
    x.statement(7, 5);
    x.assign("b", x.nondet(7, 35));
    x.statement(7, 38);
    x.assign("c", x.nondet(7, 68));
    x.statement(8, 5);
    x.assign("d", x.nondet(8, 47));
    x.statement(9, 5);
    if (x.branch(9, 5)) {
        x.statement(10, 9);
        x.call(10, 21);
    }
    x.statement(12, 5);
    return returned(0);
}

TEST_F(RestartTest, HintsSatisfyRelationsInEitherOrder) {
    // testfiles/yaml-witnesses/function-return.handwritten.c.yml
    program(std::string(relationsProgram));
    const ValidationResult result =
        validate(sequence({{follow(WaypointType::FunctionReturn, 6, 35, "\\result > 5")},
                           {follow(WaypointType::FunctionReturn, 7, 35, "(\\result != 0)")},
                           {follow(WaypointType::FunctionReturn, 7, 68, "-3 > \\result")},
                           {follow(WaypointType::FunctionReturn, 8, 47, "\\result >= 255U")},
                           {follow(WaypointType::Branching, 9, 5, "true")},
                           {target(10, 9)}}),
                 runRelations);
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message;
    EXPECT_EQ(result.runs, 1u);
}

TEST_F(RestartTest, AHintSatisfiesAllRelationsOfAConjunction) {
    program(std::string(inputsProgram));
    const ValidationResult result = validate(sequence({{follow(WaypointType::FunctionReturn, 4, 35,
                                                               "\\result >= 2 && \\result <= 5 && \\result != 2 && "
                                                               "\\result > 4")},
                                                       {follow(WaypointType::Branching, 6, 5, "true")},
                                                       {target(7, 9)}}),
                                             [](FakeExecution& x) { return runInputs(x); });
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message;
    EXPECT_EQ(result.runs, 1u);
}

TEST_F(RestartTest, AnAvoidHintViolatesTheConstraint) {
    program(std::string(inputsProgram));
    // The avoid waypoint gets a != 0, i.e. 1; the assumption then holds without a restart.
    const ValidationResult result = validate(sequence({{avoid(WaypointType::FunctionReturn, 4, 35, "\\result == 0"),
                                                        follow(WaypointType::Assumption, 5, 5, "a == 1")},
                                                       {target(7, 9)}}),
                                             [](FakeExecution& x) { return runInputs(x); });
    EXPECT_EQ(result.verdict, Verdict::TargetWithoutError) << result.message;
    EXPECT_EQ(result.runs, 1u);
}

TEST_F(RestartTest, TheFollowHintWinsOverTheAvoidHint) {
    program(std::string(inputsProgram));
    const ValidationResult result = validate(sequence({{avoid(WaypointType::FunctionReturn, 4, 35, "\\result == 0"),
                                                        follow(WaypointType::FunctionReturn, 4, 35, "\\result == 5")},
                                                       {follow(WaypointType::Branching, 6, 5, "true")},
                                                       {target(7, 9)}}),
                                             [](FakeExecution& x) { return runInputs(x); });
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message;
    EXPECT_EQ(result.runs, 1u);
}

TEST_F(RestartTest, HintsComeFromTheCurrentSegmentOnly) {
    program(std::string(inputsProgram));
    // The hint of segment 2 would make the branching of segment 1 fail.
    const ValidationResult result = validate(sequence({{follow(WaypointType::Branching, 6, 5, "false")},
                                                       {follow(WaypointType::FunctionReturn, 4, 35, "\\result == 5")},
                                                       {target(7, 9)}}),
                                             [](FakeExecution& x) { return runInputs(x); });
    EXPECT_EQ(result.verdict, Verdict::ProgramFinished) << result.message;
    EXPECT_EQ(result.segmentsReached, 1u);
}

// ---- Restarts ----

TEST_F(RestartTest, AFailedEqualityFixesTheInputAndRestarts) {
    program(std::string(inputsProgram));
    const ValidationResult result = validate(sequence({{follow(WaypointType::Assumption, 6, 5, "a == 5")},
                                                       {follow(WaypointType::Branching, 6, 5, "true")},
                                                       {target(7, 9)}}),
                                             [](FakeExecution& x) { return runInputs(x); });
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message;
    EXPECT_EQ(result.runs, 2u);
    EXPECT_EQ(scriptRuns_, 2);
    EXPECT_TRUE(log().contains("run 1: fixing input 0 (the call of __VERIFIER_nondet_int at 4:35, occurrence 0) to 5"))
        << log();
}

TEST_F(RestartTest, RelationsFixTheInputToASatisfyingValue) {
    program(std::string(inputsProgram));
    const ValidationResult result =
        validate(sequence({{follow(WaypointType::Assumption, 6, 5, "c < -3")}, {target(9, 5)}}),
                 [](FakeExecution& x) { return runInputs(x); });
    EXPECT_EQ(result.verdict, Verdict::TargetWithoutError) << result.message;
    EXPECT_TRUE(log().contains("fixing input 2 (the call of __VERIFIER_nondet_int at 5:68, occurrence 0) to -4"))
        << log();
}

TEST_F(RestartTest, AConjunctionFixesSeveralInputsAtOnce) {
    program(std::string(inputsProgram));
    const ValidationResult result =
        validate(sequence({{follow(WaypointType::Assumption, 6, 5, "a == 5 && 7 == b")}, {target(7, 9)}}),
                 [](FakeExecution& x) { return runInputs(x); });
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message;
    EXPECT_EQ(result.runs, 2u);
    EXPECT_TRUE(log().contains("run 1: fixing input 1 (the call of __VERIFIER_nondet_int at 5:35, occurrence 0) to 7"))
        << log();
}

TEST_F(RestartTest, FixedInputsStayFixedInLaterRuns) {
    program(std::string(inputsProgram));
    const ValidationResult result = validate(sequence({{follow(WaypointType::Assumption, 5, 5, "a == 5")},
                                                       {follow(WaypointType::Assumption, 6, 5, "b == 7")},
                                                       {target(7, 9)}}),
                                             [](FakeExecution& x) { return runInputs(x); });
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message;
    EXPECT_EQ(result.runs, 3u);
}

TEST_F(RestartTest, ComputedValuesAreNotFixed) {
    program(std::string(twoIfsProgram));
    const ValidationResult result =
        validate(sequence({{follow(WaypointType::Assumption, 6, 5, "b == 2")}, {target(6, 34)}}), runTwoIfs);
    EXPECT_EQ(result.verdict, Verdict::LeftWitness) << result.message;
    EXPECT_EQ(result.runs, 1u);
}

TEST_F(RestartTest, HintedInputsAreNotFixed) {
    program(std::string(inputsProgram));
    const ValidationResult result = validate(sequence({{follow(WaypointType::FunctionReturn, 4, 35, "\\result == 1")},
                                                       {follow(WaypointType::Assumption, 6, 5, "a == 5")},
                                                       {target(7, 9)}}),
                                             [](FakeExecution& x) { return runInputs(x); });
    EXPECT_EQ(result.verdict, Verdict::LeftWitness) << result.message;
    EXPECT_EQ(result.runs, 1u);
}

TEST_F(RestartTest, ValuesOutsideTheInputsTypeAreNotFixed) {
    program(std::string(relationsProgram));
    const ValidationResult result =
        validate(sequence({{follow(WaypointType::Assumption, 9, 5, "d == 256")}, {target(10, 9)}}), runRelations);
    EXPECT_EQ(result.verdict, Verdict::LeftWitness) << result.message;
    EXPECT_EQ(result.runs, 1u);
}

TEST_F(RestartTest, AFailedFunctionReturnConstraintFixesTheReturnedInput) {
    program(std::string(inputsProgram));
    // The written cast hides `\result` from the hint; the failed relation still fixes the input it is a copy of.
    const ValidationResult result =
        validate(sequence({{follow(WaypointType::FunctionReturn, 4, 35, "(long)\\result == 5")},
                           {follow(WaypointType::Branching, 6, 5, "true")},
                           {target(7, 9)}}),
                 [](FakeExecution& x) { return runInputs(x); });
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message;
    EXPECT_EQ(result.runs, 2u);
}

TEST_F(RestartTest, InputsComparedAfterPromotionsAreFixed) {
    // testfiles/sv-comp-2019/06.nondet.c: `a3 == 1U` converts the unsigned short a3 to int and then to unsigned int.
    program(R"(extern unsigned short __VERIFIER_nondet_ushort(void);
void reach_error(void);
int main(void) {
    unsigned short a3 = __VERIFIER_nondet_ushort();
    if (a3 == 1U) reach_error();
    return 0;
}
)");
    const auto script = [](FakeExecution& x) {
        x.statement(4, 5);
        x.assign("a3", x.nondet(4, 50));
        x.statement(5, 5);
        if (x.branch(5, 5)) {
            x.statement(5, 19);
            x.call(5, 31);
        }
        return returned(0);
    };
    const ValidationResult result = validate(sequence({{follow(WaypointType::Assumption, 5, 5, "a3 == 1U")},
                                                       {follow(WaypointType::Branching, 5, 5, "true")},
                                                       {target(5, 19)}}),
                                             script);
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message;
    EXPECT_EQ(result.runs, 2u);
}

constexpr std::string_view narrowingProgram = R"(extern double __VERIFIER_nondet_double(void);
extern int __VERIFIER_nondet_int(void);
void reach_error(void);
int main(void) {
    float f = __VERIFIER_nondet_double();
    char c = __VERIFIER_nondet_int();
    if (f == 27.0f && c == 5) reach_error();
    return 0;
}
)";

interp::RunResult runNarrowing(FakeExecution& x) {
    x.statement(5, 5);
    x.assign("f", x.convertedTo("f", x.nondet(5, 40)));
    x.statement(6, 5);
    x.assign("c", x.convertedTo("c", x.nondet(6, 36)));
    x.statement(7, 5);
    if (x.branch(7, 5)) {
        x.statement(7, 31);
        x.call(7, 43);
    }
    return returned(0);
}

TEST_F(RestartTest, InputsAreFixedThroughNarrowingConversions) {
    // testfiles/sv-comp-2019/16.shadowing.c declares __VERIFIER_nondet_float to return double.
    program(std::string(narrowingProgram));
    const ValidationResult result = validate(sequence({{follow(WaypointType::Assumption, 7, 5, "f == 27.0f && c == 5")},
                                                       {follow(WaypointType::Branching, 7, 5, "true")},
                                                       {target(7, 31)}}),
                                             runNarrowing);
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message;
    EXPECT_EQ(result.runs, 2u);
}

TEST_F(RestartTest, ValuesTheConversionCannotProduceAreNotFixed) {
    program(std::string(narrowingProgram));
    const ValidationResult result =
        validate(sequence({{follow(WaypointType::Assumption, 7, 5, "c == 300")}, {target(7, 31)}}), runNarrowing);
    EXPECT_EQ(result.verdict, Verdict::LeftWitness) << result.message;
    EXPECT_EQ(result.runs, 1u);
}

TEST_F(RestartTest, ACaseValueFixesTheSwitchedInput) {
    program(std::string(switchProgram));
    const ValidationResult result = validate(sequence({{follow(WaypointType::Branching, 5, 5, "2")}, {target(9, 9)}}),
                                             [this](FakeExecution& x) { return runSwitch(*this, x); });
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message;
    EXPECT_EQ(result.runs, 2u);
}

TEST_F(RestartTest, ReadsOfUninitializedVariablesInConstraintsAreInputsToo) {
    program(R"(void reach_error(void);
int main(void) {
    int x;
    if (x == 4) reach_error();
    return 0;
}
)");
    const auto script = [](FakeExecution& x) {
        x.statement(3, 5);
        x.statement(4, 5);
        if (x.branch(4, 5)) {
            x.statement(4, 17);
            x.call(4, 29);
        }
        x.statement(5, 5);
        return returned(0);
    };
    const ValidationResult result = validate(sequence({{follow(WaypointType::Assumption, 4, 5, "x == 4")},
                                                       {follow(WaypointType::Branching, 4, 5, "true")},
                                                       {target(4, 17)}}),
                                             script);
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message;
    EXPECT_EQ(result.runs, 2u);
    EXPECT_TRUE(log().contains("fixing input 0 (the read at <constraint>:1:1, occurrence 0) to 4")) << log();
}

TEST_F(RestartTest, TheNumberOfRestartsIsLimited) {
    program(std::string(inputsProgram));
    options_.maxRestarts = 1;
    const ValidationResult result = validate(sequence({{follow(WaypointType::Assumption, 5, 5, "a == 5")},
                                                       {follow(WaypointType::Assumption, 6, 5, "b == 7")},
                                                       {target(7, 9)}}),
                                             [](FakeExecution& x) { return runInputs(x); });
    EXPECT_EQ(result.verdict, Verdict::Unknown) << result.message;
    EXPECT_EQ(exitCode(result), 4);
    EXPECT_EQ(result.runs, 2u);
    EXPECT_EQ(result.message, "the limit of 1 restarts was reached: the follow waypoint of segment 2 (assumption at "
                              "6:5) failed at 6:5");
}

TEST_F(RestartTest, TheRestartLimitScalesWithTheWaypoints) {
    ValidationOptions options;
    EXPECT_EQ(restartLimit(options, 0), 16u);
    EXPECT_EQ(restartLimit(options, 3), 22u);
    EXPECT_EQ(restartLimit(options, 1'000'000), options.maxRestarts);
    options.maxRestarts = 20;
    EXPECT_EQ(restartLimit(options, 3), 20u);
}

TEST_F(RestartTest, NoRestartsAtAll) {
    program(std::string(inputsProgram));
    options_.maxRestarts = 0;
    const ValidationResult result =
        validate(sequence({{follow(WaypointType::Assumption, 6, 5, "a == 5")}, {target(7, 9)}}),
                 [](FakeExecution& x) { return runInputs(x); });
    EXPECT_EQ(result.verdict, Verdict::Unknown) << result.message;
    EXPECT_EQ(result.runs, 1u);
}

// ---- One more run with other defaults ----

constexpr std::string_view ifInputProgram = R"(extern int __VERIFIER_nondet_int(void);
void reach_error(void);
int main(void) {
    int a = __VERIFIER_nondet_int();
    if (a) reach_error();
    return 0;
}
)";

interp::RunResult runIfInput(FakeExecution& x) {
    x.statement(4, 5);
    x.assign("a", x.nondet(4, 35));
    x.statement(5, 5);
    if (x.branch(5, 5)) {
        x.statement(5, 12);
        x.call(5, 24);
    }
    x.statement(6, 5);
    return returned(0);
}

TEST_F(RestartTest, ARunWithoutFailedWaypointIsRetriedWithOtherDefaults) {
    // A witness with the target only: the search has nothing to solve, and only the default decides the path.
    program(std::string(ifInputProgram));
    options_.retryDefaultInput = 1;
    const ValidationResult result = validate(sequence({{target(5, 12)}}), runIfInput);
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message;
    EXPECT_EQ(result.runs, 2u);
    EXPECT_TRUE(log().contains("run 1: no waypoint failed; one more run with inputs 1 by default")) << log();
}

constexpr std::string_view abortOnInputProgram = R"(extern int __VERIFIER_nondet_int(void);
extern void abort(void);
void reach_error(void);
int main(void) {
    int a = __VERIFIER_nondet_int();
    if (a) abort();
    if (a == 5) reach_error();
    return 0;
}
)";

interp::RunResult runAbortOnInput(FakeExecution& x) {
    x.statement(5, 5);
    x.assign("a", x.nondet(5, 35));
    x.statement(6, 5);
    if (x.branch(6, 5)) {
        x.statement(6, 12);
        x.call(6, 18);
        return endedBy(interp::Termination::Abort, "abort");
    }
    x.statement(7, 5);
    if (x.branch(7, 5)) {
        x.statement(7, 17);
        x.call(7, 29);
    }
    x.statement(8, 5);
    return returned(0);
}

TEST_F(RestartTest, ARetryThatDoesNotConfirmKeepsTheVerdictOfTheRunBefore) {
    // Run 1 returns in the target segment; the retry aborts.
    program(std::string(abortOnInputProgram));
    options_.retryDefaultInput = 1;
    const ValidationResult result = validate(sequence({{target(7, 17)}}), runAbortOnInput);
    EXPECT_EQ(result.verdict, Verdict::TargetWithoutError) << result.message;
    EXPECT_EQ(result.message, "the target segment was reached, but main returned 0 without calling 'reach_error'");
    EXPECT_EQ(result.runs, 2u);
    EXPECT_EQ(scriptRuns_, 2);
    EXPECT_TRUE(log().contains("run 2: with inputs 1 by default, the witness was not confirmed either; the verdict is "
                               "that of run 1"))
        << log();
}

TEST_F(RestartTest, AnAvoidWaypointThatPassesInTheRetryKeepsTheVerdictOfTheRunBefore) {
    // With a = 1 the error is called at the target, but the avoid waypoint passes before.
    program(std::string(ifInputProgram));
    options_.retryDefaultInput = 1;
    const ValidationResult result =
        validate(sequence({{avoid(WaypointType::Assumption, 5, 12, "a == 1"), target(5, 12)}}), runIfInput);
    EXPECT_EQ(result.verdict, Verdict::TargetWithoutError) << result.message;
    EXPECT_EQ(scriptRuns_, 2);
    EXPECT_FALSE(log().contains("needs")) << log();
}

TEST_F(RestartTest, AFollowWaypointThatFailsInTheRetryIsNotSearchedFor) {
    // The search would fix a to 2 and restart; the retry rejects at once instead.
    program(std::string(ifInputProgram));
    options_.retryDefaultInput = 1;
    const ValidationResult result =
        validate(sequence({{follow(WaypointType::Assumption, 5, 12, "a == 2")}, {target(5, 12)}}), runIfInput);
    EXPECT_EQ(result.verdict, Verdict::ProgramFinished) << result.message;
    EXPECT_EQ(result.runs, 2u);
    EXPECT_EQ(scriptRuns_, 2);
    EXPECT_FALSE(log().contains("needs")) << log();
}

constexpr std::string_view nestedInputsProgram = R"(extern int __VERIFIER_nondet_int(void);
void reach_error(void);
int main(void) {
    int a = __VERIFIER_nondet_int();
    int b = __VERIFIER_nondet_int();
    if (a == 5) if (b) reach_error();
    return 0;
}
)";

interp::RunResult runNestedInputs(FakeExecution& x) {
    x.statement(4, 5);
    x.assign("a", x.nondet(4, 35));
    x.statement(5, 5);
    x.assign("b", x.nondet(5, 35));
    x.statement(6, 5);
    if (x.branch(6, 5)) {
        x.statement(6, 17);
        if (x.branch(6, 17)) {
            x.statement(6, 24);
            x.call(6, 36);
        }
    }
    x.statement(7, 5);
    return returned(0);
}

TEST_F(RestartTest, TheRetryKeepsTheFixedInputs) {
    // Run 1 fixes a to 5; run 2 ends in the target segment with b = 0; the retry gives b 1 and keeps a = 5.
    program(std::string(nestedInputsProgram));
    options_.retryDefaultInput = 1;
    const ValidationResult result =
        validate(sequence({{follow(WaypointType::Branching, 6, 5, "true")}, {target(6, 24)}}), runNestedInputs);
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message;
    EXPECT_EQ(result.runs, 3u);
    EXPECT_TRUE(log().contains("run 2: no waypoint failed; one more run with inputs 1 by default")) << log();
}

constexpr std::string_view twoErrorsProgram = R"(extern int __VERIFIER_nondet_int(void);
void reach_error(void);
int main(void) {
    int a = __VERIFIER_nondet_int();
    if (a) reach_error();
    else reach_error();
    return 0;
}
)";

interp::RunResult runTwoErrors(FakeExecution& x) {
    x.statement(4, 5);
    x.assign("a", x.nondet(4, 35));
    x.statement(5, 5);
    if (x.branch(5, 5)) {
        x.statement(5, 12);
        x.call(5, 24);
    } else {
        x.statement(6, 10);
        x.call(6, 22);
    }
    x.statement(7, 5);
    return returned(0);
}

TEST_F(RestartTest, TheErrorFunctionCalledElsewhereIsRetried) {
    program(std::string(twoErrorsProgram));
    options_.retryDefaultInput = 1;
    const ValidationResult result = validate(sequence({{target(5, 12)}}), runTwoErrors);
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message;
    EXPECT_EQ(result.runs, 2u);
}

constexpr std::string_view withoutInputsProgram = R"(void reach_error(void);
int main(void) {
    int i = 0;
    if (i) reach_error();
    return 0;
}
)";

TEST_F(RestartTest, ARunThatGaveNoInputTheDefaultIsNotRetried) {
    // The retry would repeat the run.
    program(std::string(withoutInputsProgram));
    options_.retryDefaultInput = 1;
    const ValidationResult result = validate(sequence({{target(4, 12)}}), [](FakeExecution& x) {
        x.statement(3, 5);
        x.assign("i", x.number("i", 0));
        x.statement(4, 5);
        if (x.branch(4, 5)) {
            x.statement(4, 12);
            x.call(4, 24);
        }
        x.statement(5, 5);
        return returned(0);
    });
    EXPECT_EQ(result.verdict, Verdict::TargetWithoutError) << result.message;
    EXPECT_EQ(scriptRuns_, 1);
}

TEST_F(RestartTest, TheRetryRunningOutOfMemoryKeepsTheVerdictOfTheRunBefore) {
    program(std::string(ifInputProgram));
    options_.retryDefaultInput = 1;
    const ValidationResult result = validate(sequence({{target(5, 12)}}), [](FakeExecution& x) {
        x.statement(4, 5);
        x.assign("a", x.nondet(4, 35));
        if (x.integer("a") != 0) {
            throw std::bad_alloc();
        }
        x.statement(5, 5);
        x.branch(5, 5);
        x.statement(6, 5);
        return returned(0);
    });
    EXPECT_EQ(result.verdict, Verdict::TargetWithoutError) << result.message;
    EXPECT_EQ(result.runs, 2u);
}

TEST_F(RestartTest, TheDiagnosticsOfTheRetryAreDiscarded) {
    // The constraint is first compiled in the retry, which ends as unsupported.
    program(std::string(ifInputProgram));
    options_.retryDefaultInput = 1;
    const std::size_t before = diagnostics_.diagnostics().size();
    const ValidationResult result =
        validate(sequence({{follow(WaypointType::Assumption, 5, 12, "unknown == 1")}, {target(5, 12)}}), runIfInput);
    EXPECT_EQ(result.verdict, Verdict::ProgramFinished) << result.message;
    EXPECT_EQ(scriptRuns_, 2);
    EXPECT_EQ(diagnostics_.diagnostics().size(), before);
}

constexpr std::string_view loopOnInputProgram = R"(extern int __VERIFIER_nondet_int(void);
void reach_error(void);
int main(void) {
    int a = __VERIFIER_nondet_int();
    if (a == 2) reach_error();
    while (a) a = a;
    return 0;
}
)";

interp::RunResult runLoopOnInput(FakeExecution& x) {
    x.statement(4, 5);
    x.assign("a", x.nondet(4, 35));
    x.statement(5, 5);
    if (x.branch(5, 5)) {
        x.statement(5, 17);
        x.call(5, 29);
    }
    x.statement(6, 5);
    while (x.branch(6, 5)) {
        x.statement(6, 15);
    }
    x.statement(7, 5);
    return returned(0);
}

TEST_F(RestartTest, TheRetryStopsAtItsBudget) {
    // With a = 1, the loop does not end.
    program(std::string(loopOnInputProgram));
    options_.retryDefaultInput = 1;
    options_.retryExtraEvents = 100;
    const ValidationResult result = validate(sequence({{target(5, 17)}}), runLoopOnInput);
    EXPECT_EQ(result.verdict, Verdict::TargetWithoutError) << result.message;
    EXPECT_EQ(result.runs, 2u);
    // The 8 events of run 1, and 100.
    EXPECT_TRUE(log().contains("run 2: the run exceeded its budget of 108 events")) << log();
}

TEST_F(RestartTest, NoRetryAfterAWaypointFailed) {
    program(std::string(ifInputProgram));
    options_.retryDefaultInput = 1;
    // The follow waypoint fails with a = 0, and the search cannot change the constant.
    const ValidationResult result =
        validate(sequence({{follow(WaypointType::Assumption, 5, 5, "0")}, {target(5, 12)}}), runIfInput);
    EXPECT_EQ(result.verdict, Verdict::LeftWitness) << result.message;
    EXPECT_EQ(scriptRuns_, 1);
}

TEST_F(RestartTest, NoRetryWithoutOtherDefaults) {
    program(std::string(ifInputProgram));
    const ValidationResult result = validate(sequence({{target(5, 12)}}), runIfInput);
    EXPECT_EQ(result.verdict, Verdict::TargetWithoutError) << result.message;
    EXPECT_EQ(scriptRuns_, 1);
}

} // namespace
} // namespace nitwit::validate::test
