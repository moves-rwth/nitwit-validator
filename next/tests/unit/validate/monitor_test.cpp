// The segment monitor (monitor.hpp) with scripted runs: progress through the segments, evaluation points, follow and
// avoid waypoints, cascading, branching and switch values, and the error function (validate/README.md).

#include "validate/monitor.hpp"

#include <string>

#include <gtest/gtest.h>

#include "test_programs.hpp"

namespace nitwit::validate::test {
namespace {

class MonitorTest : public ValidateTest {};

// ---- Progress and the target ----

TEST_F(MonitorTest, ConfirmsWhenTheErrorFunctionIsCalledInTheTargetSegment) {
    program(std::string(inputsProgram));
    const ValidationResult result =
        validate(sequence({{follow(WaypointType::Branching, 6, 5, "true")}, {target(7, 9)}}),
                 [](FakeExecution& x) { return runInputs(x, true); });
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message;
    EXPECT_EQ(exitCode(result), 0);
    EXPECT_EQ(result.segmentsReached, 1u);
    EXPECT_EQ(result.runs, 1u);
    EXPECT_EQ(result.message, "the witness was followed to its target: 'reach_error' was called at 7:9 in segment 2 "
                              "of 2");
}

TEST_F(MonitorTest, TheErrorFunctionBeforeTheTargetSegmentIsNoConfirmation) {
    program(std::string(inputsProgram));
    const ValidationResult result = validate(sequence({{follow(WaypointType::Assumption, 9, 5, "1")}, {target(7, 9)}}),
                                             [](FakeExecution& x) { return runInputs(x, true); });
    EXPECT_EQ(result.verdict, Verdict::ErrorBeforeTarget) << result.message;
    EXPECT_EQ(exitCode(result), 252);
    EXPECT_EQ(result.message, "the target was not reached: 'reach_error' was called at 7:9 in segment 1 of 2");
}

TEST_F(MonitorTest, TheErrorFunctionElsewhereThanTheTargetIsNoConfirmation) {
    program(std::string(inputsProgram));
    const ValidationResult result =
        validate(sequence({{follow(WaypointType::Branching, 6, 5, "true")}, {target(9, 5)}}),
                 [](FakeExecution& x) { return runInputs(x, true); });
    EXPECT_EQ(result.verdict, Verdict::ErrorBeforeTarget) << result.message;
    EXPECT_EQ(result.message,
              "the target was not reached: 'reach_error' was called at 7:9 in segment 2 of 2, not at the target");
}

TEST_F(MonitorTest, ATargetWithoutColumnMatchesACallInItsLine) {
    program(std::string(inputsProgram));
    const ValidationResult result =
        validate(sequence({{follow(WaypointType::Branching, 6, 5, "true")}, {target(7, std::nullopt)}}),
                 [](FakeExecution& x) { return runInputs(x, true); });
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message;
}

TEST_F(MonitorTest, TheProgramFinishingBeforeTheTargetSegment) {
    program(std::string(inputsProgram));
    const ValidationResult result = validate(sequence({{follow(WaypointType::Assumption, 7, 9, "1")}, {target(7, 9)}}),
                                             [](FakeExecution& x) { return runInputs(x); });
    EXPECT_EQ(result.verdict, Verdict::ProgramFinished) << result.message;
    EXPECT_EQ(exitCode(result), 242);
    EXPECT_EQ(result.message, "main returned 0 in segment 1 of 2 without calling 'reach_error'");
}

TEST_F(MonitorTest, TheTargetSegmentWithoutTheErrorFunction) {
    program(std::string(inputsProgram));
    const ValidationResult result =
        validate(sequence({{follow(WaypointType::Branching, 6, 5, "false")}, {target(7, 9)}}),
                 [](FakeExecution& x) { return runInputs(x); });
    EXPECT_EQ(result.verdict, Verdict::TargetWithoutError) << result.message;
    EXPECT_EQ(exitCode(result), 250);
    EXPECT_EQ(result.segmentsReached, 1u);
}

TEST_F(MonitorTest, TheErrorFunctionIsAnOption) {
    program(std::string(inputsProgram));
    options_.errorFunction = "fail";
    const ValidationResult result =
        validate(sequence({{follow(WaypointType::Branching, 6, 5, "true")}, {target(7, 9)}}),
                 [](FakeExecution& x) { return runInputs(x, true); });
    EXPECT_EQ(result.verdict, Verdict::TargetWithoutError) << result.message;
    EXPECT_EQ(result.message, "the target segment was reached, but main returned 0 without calling 'fail'");
}

TEST_F(MonitorTest, AFunctionEnterWaypointPassesAtTheCall) {
    program(std::string(inputsProgram));
    const ValidationResult result = validate(sequence({{follow(WaypointType::FunctionEnter, 7, 21)}, {target(7, 9)}}),
                                             [](FakeExecution& x) { return runInputs(x, true); });
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message;
}

// ---- Evaluation points ----

TEST_F(MonitorTest, AColumnSelectsOneOfTheCallsInALine) {
    program(std::string(inputsProgram));
    const ValidationResult result = validate(sequence({{follow(WaypointType::FunctionReturn, 5, 68)},
                                                       {follow(WaypointType::Branching, 6, 5, "false")},
                                                       {target(9, 5)}}),
                                             [](FakeExecution& x) { return runInputs(x); });
    EXPECT_EQ(result.verdict, Verdict::TargetWithoutError) << result.message;
    EXPECT_TRUE(log().contains("run 1: segment 1 of 3 passed at 5:68 (function_return)")) << log();
}

TEST_F(MonitorTest, WithoutAColumnTheFirstEvaluationInTheLineCounts) {
    program(std::string(inputsProgram));
    const ValidationResult result =
        validate(sequence({{follow(WaypointType::FunctionReturn, 5, std::nullopt)}, {target(9, 5)}}),
                 [](FakeExecution& x) { return runInputs(x); });
    EXPECT_EQ(result.verdict, Verdict::TargetWithoutError) << result.message;
    EXPECT_TRUE(log().contains("run 1: segment 1 of 2 passed at 5:35 (function_return)")) << log();
}

TEST_F(MonitorTest, AFollowWaypointMustPassAtItsFirstEvaluationPoint) {
    program(std::string(twoIfsProgram));
    // The first if in line 6 is false, the second true.
    const ValidationResult result =
        validate(sequence({{follow(WaypointType::Branching, 6, std::nullopt, "true")}, {target(6, 34)}}), runTwoIfs);
    EXPECT_EQ(result.verdict, Verdict::LeftWitness) << result.message;
    EXPECT_EQ(exitCode(result), 241);
    EXPECT_EQ(result.message, "the execution left the witness: the follow waypoint of segment 1 (branching at 6) did "
                              "not pass at its first evaluation point, 6:5");
}

TEST_F(MonitorTest, AColumnSkipsTheOtherConditionsOfTheLine) {
    program(std::string(twoIfsProgram));
    const ValidationResult result =
        validate(sequence({{follow(WaypointType::Branching, 6, 23, "true")}, {target(6, 34)}}), runTwoIfs);
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message;
}

TEST_F(MonitorTest, AnAssumptionAtABlockIsEvaluatedBeforeItsFirstStatement) {
    // testfiles/sv-comp-2019/16.shadowing.c: CPAchecker places assumptions at the `{` of a block.
    program(R"(extern float __VERIFIER_nondet_float(void);
void reach_error(void);
int main(void) {
    float f = __VERIFIER_nondet_float();
    {
        float f = 2;
    }
    if (f == 27.0f) reach_error();
    return 0;
}
)");
    const auto script = [](FakeExecution& x) {
        x.statement(4, 5);
        x.assign("f", x.nondet(4, 39));
        x.statement(6, 9);
        x.statement(8, 5);
        if (x.branch(8, 5)) {
            x.statement(8, 21);
            x.call(8, 33);
        }
        return returned(0);
    };
    const ValidationResult result = validate(sequence({{follow(WaypointType::Assumption, 5, 5, "f == 27.0f")},
                                                       {follow(WaypointType::Branching, 8, 5, "true")},
                                                       {target(8, 21)}}),
                                             script);
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message;
    EXPECT_EQ(result.runs, 2u);
}

TEST_F(MonitorTest, EventsOfOtherTypesAtTheLocationDoNotEvaluateAWaypoint) {
    program(std::string(twoIfsProgram));
    // The statement at 6:5 is the if whose condition is false: an assumption there is not a branching.
    const ValidationResult result = validate(sequence({{follow(WaypointType::Assumption, 6, 5, "1")},
                                                       {follow(WaypointType::Branching, 6, 5, "false")},
                                                       {target(6, 34)}}),
                                             runTwoIfs);
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message;
}

// ---- Avoid waypoints ----

TEST_F(MonitorTest, APassingAvoidWaypointRejectsTheExecution) {
    program(std::string(inputsProgram));
    const ValidationResult result =
        validate(sequence({{avoid(WaypointType::Branching, 6, 5, "false"), follow(WaypointType::Assumption, 9, 5, "1")},
                           {target(9, 5)}}),
                 [](FakeExecution& x) { return runInputs(x); });
    EXPECT_EQ(result.verdict, Verdict::LeftWitness) << result.message;
    EXPECT_EQ(result.message,
              "the execution left the witness: the avoid waypoint of segment 1 (branching at 6:5) passed at 6:5");
}

TEST_F(MonitorTest, AFailingAvoidWaypointIsIgnored) {
    program(std::string(inputsProgram));
    const ValidationResult result =
        validate(sequence({{avoid(WaypointType::Branching, 6, 5, "true"), follow(WaypointType::Assumption, 9, 5, "1")},
                           {target(9, 5)}}),
                 [](FakeExecution& x) { return runInputs(x); });
    EXPECT_EQ(result.verdict, Verdict::TargetWithoutError) << result.message;
}

TEST_F(MonitorTest, APassingAvoidWaypointWinsOverTheFollowWaypoint) {
    program(std::string(inputsProgram));
    const ValidationResult result = validate(
        sequence({{avoid(WaypointType::Branching, 6, 5, "false"), follow(WaypointType::Branching, 6, 5, "false")},
                  {target(9, 5)}}),
        [](FakeExecution& x) { return runInputs(x); });
    EXPECT_EQ(result.verdict, Verdict::LeftWitness) << result.message;
}

TEST_F(MonitorTest, OnlyTheWaypointsOfTheCurrentSegmentAreEvaluated) {
    program(std::string(inputsProgram));
    const ValidationResult result = validate(sequence({{follow(WaypointType::Assumption, 9, 5, "1")},
                                                       {avoid(WaypointType::Branching, 6, 5, "false"), target(7, 9)}}),
                                             [](FakeExecution& x) { return runInputs(x); });
    EXPECT_EQ(result.verdict, Verdict::TargetWithoutError) << result.message;
}

TEST_F(MonitorTest, AvoidConstraintsOnUnfixedInputsDoNotPass) {
    program(std::string(inputsProgram));
    // a has its default 0, but no run fixed it.
    const ValidationResult result = validate(
        sequence({{avoid(WaypointType::Assumption, 6, 5, "a == 0"), follow(WaypointType::Branching, 6, 5, "false")},
                  {target(9, 5)}}),
        [](FakeExecution& x) { return runInputs(x); });
    EXPECT_EQ(result.verdict, Verdict::TargetWithoutError) << result.message;
    EXPECT_EQ(result.runs, 1u);
}

TEST_F(MonitorTest, AnAvoidedReturnValueThatIsNoInputPasses) {
    program(std::string(inputsProgram));
    // The call returns 0 without asking the oracle, as a defined function would.
    const ValidationResult result =
        validate(sequence({{avoid(WaypointType::FunctionReturn, 4, 35, "\\result == 0"),
                            follow(WaypointType::Assumption, 9, 5, "1")},
                           {target(9, 5)}}),
                 [](FakeExecution& x) {
                     x.statement(4, 5);
                     x.call(4, 35, value::Value::integer(0, {.bits = 32, .isSigned = true}));
                     return returned(0);
                 });
    EXPECT_EQ(result.verdict, Verdict::LeftWitness) << result.message;
}

TEST_F(MonitorTest, AvoidConstraintsOnKnownValuesPass) {
    program(std::string(twoIfsProgram));
    const ValidationResult result = validate(
        sequence({{avoid(WaypointType::Assumption, 6, 5, "b == 1"), follow(WaypointType::Assumption, 7, 5, "1")},
                  {target(7, 5)}}),
        runTwoIfs);
    EXPECT_EQ(result.verdict, Verdict::LeftWitness) << result.message;
}

// ---- Cascading ----

TEST_F(MonitorTest, AssumptionsAtOneStatementCompleteSeveralSegments) {
    program(std::string(twoIfsProgram));
    const ValidationResult result = validate(sequence({{follow(WaypointType::Assumption, 6, 5, "1")},
                                                       {follow(WaypointType::Assumption, 6, 5, "b == 1")},
                                                       {follow(WaypointType::Branching, 6, 5, "false")},
                                                       {target(6, 34)}}),
                                             runTwoIfs);
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message;
    EXPECT_TRUE(log().contains("segment 2 of 4 passed at 6:5 (assumption)")) << log();
}

TEST_F(MonitorTest, ACascadedAssumptionThatFailsWaitsForTheNextExecutionOfTheStatement) {
    program(std::string(loopProgram));
    const ValidationResult result = validate(sequence({{follow(WaypointType::Assumption, 6, 9, "i == 0")},
                                                       {follow(WaypointType::Assumption, 6, 9, "i == 1")},
                                                       {follow(WaypointType::Assumption, 6, 9, "i == 2")},
                                                       {follow(WaypointType::Branching, 5, 5, "false")},
                                                       {follow(WaypointType::Branching, 8, 5, "true")},
                                                       {target(8, 17)}}),
                                             runLoop);
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message;
}

TEST_F(MonitorTest, OtherWaypointTypesDoNotCascade) {
    program(std::string(loopProgram));
    const ValidationResult result = validate(sequence({{follow(WaypointType::Branching, 5, 5, "true")},
                                                       {follow(WaypointType::Branching, 5, 5, "true")},
                                                       {follow(WaypointType::Branching, 5, 5, "true")},
                                                       {follow(WaypointType::Branching, 5, 5, "false")},
                                                       {follow(WaypointType::Branching, 8, 5, "true")},
                                                       {target(8, 17)}}),
                                             runLoop);
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message;
}

TEST_F(MonitorTest, AvoidWaypointsAreNotEvaluatedWhereTheirSegmentBecameCurrent) {
    program(std::string(twoIfsProgram));
    const ValidationResult result = validate(
        sequence({{follow(WaypointType::Assumption, 6, 5, "1")},
                  {avoid(WaypointType::Assumption, 6, 5, "b == 1"), follow(WaypointType::Branching, 6, 5, "false")},
                  {target(6, 34)}}),
        runTwoIfs);
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message;
}

// ---- Branching values ----

TEST_F(MonitorTest, TheDefaultBranchOfASwitch) {
    program(std::string(switchProgram));
    const ValidationResult result =
        validate(sequence({{follow(WaypointType::Branching, 5, 5, "default")}, {target(12, 9)}}),
                 [this](FakeExecution& x) { return runSwitch(*this, x); });
    EXPECT_EQ(result.verdict, Verdict::TargetWithoutError) << result.message;
    EXPECT_EQ(result.message, "the target segment was reached, but main returned 2 without calling 'reach_error'");
}

TEST_F(MonitorTest, ACaseValueOfAKnownValueMustMatch) {
    program(std::string(switchProgram));
    const ValidationResult result = validate(sequence({{follow(WaypointType::FunctionReturn, 4, 35, "\\result == 1")},
                                                       {follow(WaypointType::Branching, 5, 5, "2")},
                                                       {target(9, 9)}}),
                                             [this](FakeExecution& x) { return runSwitch(*this, x); });
    EXPECT_EQ(result.verdict, Verdict::LeftWitness) << result.message;
}

TEST_F(MonitorTest, AnAvoidedCaseOfAnUnfixedInputDoesNotPass) {
    program(std::string(switchProgram));
    const ValidationResult result = validate(
        sequence({{avoid(WaypointType::Branching, 5, 5, "default"), follow(WaypointType::Branching, 5, 5, "default")},
                  {target(12, 9)}}),
        [this](FakeExecution& x) { return runSwitch(*this, x); });
    EXPECT_EQ(result.verdict, Verdict::TargetWithoutError) << result.message;
}

TEST_F(MonitorTest, AnAvoidedCaseOfAKnownValuePasses) {
    program(std::string(switchProgram));
    const ValidationResult result =
        validate(sequence({{follow(WaypointType::FunctionReturn, 4, 35, "\\result == 1")},
                           {avoid(WaypointType::Branching, 5, 5, "1"), follow(WaypointType::Branching, 5, 5, "1")},
                           {target(7, 9)}}),
                 [this](FakeExecution& x) { return runSwitch(*this, x); });
    EXPECT_EQ(result.verdict, Verdict::LeftWitness) << result.message;
}

TEST_F(MonitorTest, OutcomesDoNotFitASwitch) {
    program(std::string(switchProgram));
    const ValidationResult result =
        validate(sequence({{follow(WaypointType::Branching, 5, 5, "true")}, {target(9, 9)}}),
                 [this](FakeExecution& x) { return runSwitch(*this, x); });
    EXPECT_EQ(result.verdict, Verdict::Unsupported) << result.message;
    EXPECT_EQ(exitCode(result), 4);
    EXPECT_EQ(result.message,
              "the follow waypoint of segment 1 (branching at 5:5): the constraint 'true' does not fit a branching at "
              "'switch'");
}

TEST_F(MonitorTest, AConditionalExpressionIsLocatedAtItsQuestionMark) {
    program(R"(extern int __VERIFIER_nondet_int(void);
void reach_error(void);
int main(void) {
    int x = __VERIFIER_nondet_int();
    int y = x == 0 ? 1 : 2; if (y == 1) reach_error();
    return 0;
}
)");
    const auto script = [](FakeExecution& x) {
        x.statement(4, 5);
        x.assign("x", x.nondet(4, 35));
        x.statement(5, 5);
        x.assign("y", x.number("y", x.branch(5, 20) ? 1 : 2));
        x.statement(5, 29);
        if (x.branch(5, 29)) {
            x.statement(5, 41);
            x.call(5, 53);
        }
        return returned(0);
    };
    const ValidationResult result = validate(sequence({{follow(WaypointType::Branching, 5, 20, "true")},
                                                       {follow(WaypointType::Branching, 5, 29, "true")},
                                                       {target(5, 41)}}),
                                             script);
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message;
}

TEST_F(MonitorTest, CaseValuesDoNotFitAnIf) {
    program(std::string(inputsProgram));
    const ValidationResult result = validate(sequence({{follow(WaypointType::Branching, 6, 5, "1")}, {target(7, 9)}}),
                                             [](FakeExecution& x) { return runInputs(x); });
    EXPECT_EQ(result.verdict, Verdict::Unsupported) << result.message;
}

// ---- Constraints ----

TEST_F(MonitorTest, ConstraintsThatDoNotCompileAreUnsupported) {
    program(std::string(inputsProgram));
    const ValidationResult result =
        validate(sequence({{follow(WaypointType::Assumption, 6, 5, "nosuch == 1")}, {target(7, 9)}}),
                 [](FakeExecution& x) { return runInputs(x); });
    EXPECT_EQ(result.verdict, Verdict::Unsupported) << result.message;
    EXPECT_EQ(result.message, "the follow waypoint of segment 1 (assumption at 6:5): <constraint>:1:1: error: use of "
                              "undeclared identifier 'nosuch'");
    EXPECT_EQ(firstError(), "<constraint>:1:1: error: use of undeclared identifier 'nosuch'");
}

TEST_F(MonitorTest, AssumptionsNeedAConstraint) {
    program(std::string(inputsProgram));
    const ValidationResult result = validate(sequence({{follow(WaypointType::Assumption, 6, 5)}, {target(7, 9)}}),
                                             [](FakeExecution& x) { return runInputs(x); });
    EXPECT_EQ(result.verdict, Verdict::Unsupported) << result.message;
    EXPECT_EQ(result.message, "the follow waypoint of segment 1 (assumption at 6:5): it has no constraint");
}

TEST_F(MonitorTest, AConstraintThatCannotBeEvaluatedFails) {
    program(std::string(inputsProgram));
    const ValidationResult result =
        validate(sequence({{follow(WaypointType::Assumption, 6, 5, "a / 0 == 1")}, {target(7, 9)}}),
                 [](FakeExecution& x) { return runInputs(x); });
    EXPECT_EQ(result.verdict, Verdict::LeftWitness) << result.message;
    EXPECT_TRUE(result.message.ends_with("6:5: the constraint cannot be evaluated: division by zero"))
        << result.message;
}

TEST_F(MonitorTest, ConstraintsSeeTheVariablesOfTheEvent) {
    program(std::string(twoIfsProgram));
    const ValidationResult result =
        validate(sequence({{follow(WaypointType::Assumption, 6, 23, "b == 1 && a == 0")}, {target(6, 34)}}), runTwoIfs);
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message;
    EXPECT_EQ(result.runs, 1u);
}

} // namespace
} // namespace nitwit::validate::test
