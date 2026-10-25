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
    EXPECT_EQ(exitCode(result), 241);
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

TEST_F(MonitorTest, AFollowWaypointIsEvaluatedAtItsFirstEvaluationPoint) {
    program(std::string(twoIfsProgram));
    // The first if in line 6 is false, the second true: the waypoint fails at the first, and the search makes a > 0.
    const ValidationResult result =
        validate(sequence({{follow(WaypointType::Branching, 6, std::nullopt, "true")}, {target(6, 34)}}), runTwoIfs);
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message;
    EXPECT_EQ(result.runs, 2u);
    EXPECT_TRUE(log().contains("run 1: the follow waypoint of segment 1 (branching at 6) failed at 6:5")) << log();
}

TEST_F(MonitorTest, AFollowWaypointThatFailsAtItsFirstEvaluationPointRejects) {
    program(std::string(twoIfsProgram));
    // The outcome has no term, so no input can change it.
    const ValidationResult result = validate(
        sequence({{follow(WaypointType::Branching, 6, std::nullopt, "true")}, {target(6, 34)}}), [](FakeExecution& x) {
            x.statement(6, 5);
            x.branch(6, 5, false);
            return returned(0);
        });
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

TEST_F(MonitorTest, AWaypointAtNoEvaluationPointDoesNotFitTheProgram) {
    program(std::string(inputsProgram));
    // 6:6 is inside the if; an avoid waypoint there would never be evaluated.
    const ValidationResult result = validate(sequence({{avoid(WaypointType::Branching, 6, 6, "true"), target(7, 9)}}),
                                             [](FakeExecution& x) { return runInputs(x); });
    EXPECT_EQ(result.verdict, Verdict::Unsupported) << result.message;
    EXPECT_EQ(exitCode(result), 4);
    EXPECT_EQ(result.message, "the witness does not fit the program: the avoid waypoint of segment 1 (branching at "
                              "6:6) is at no evaluation point of its type");
    EXPECT_EQ(scriptRuns_, 0);
    // A line that does not exist.
    EXPECT_EQ(validate(sequence({{follow(WaypointType::Assumption, 99, 1, "1")}, {target(7, 9)}}),
                       [](FakeExecution& x) { return runInputs(x); })
                  .verdict,
              Verdict::Unsupported);
}

TEST_F(MonitorTest, TheFunctionOfALocationMustContainIt) {
    program(std::string(inputsProgram));
    witness::YamlWitness witness = sequence({{follow(WaypointType::Branching, 6, 5, "true")}, {target(7, 9)}});
    witness.violationSequences[0].segments[0].waypoints[0].location.function = "main";
    EXPECT_EQ(validate(witness, [](FakeExecution& x) { return runInputs(x, true); }).verdict, Verdict::Confirmed);
    witness.violationSequences[0].segments[0].waypoints[0].location.function = "reach_error";
    const ValidationResult result = validate(witness, [](FakeExecution& x) { return runInputs(x, true); });
    EXPECT_EQ(result.verdict, Verdict::Unsupported) << result.message;
    EXPECT_EQ(result.message, "the witness does not fit the program: the follow waypoint of segment 1 (branching at "
                              "6:5) is in function 'main', not in 'reach_error' as the witness says");
}

// ---- Avoid waypoints ----

TEST_F(MonitorTest, AnAvoidConstraintWithoutValueIsUnknown) {
    program(std::string(inputsProgram));
    const ValidationResult result = validate(
        sequence({{avoid(WaypointType::Assumption, 6, 5, "a / 0 == 1"), follow(WaypointType::Branching, 6, 5, "false")},
                  {target(9, 5)}}),
        [](FakeExecution& x) { return runInputs(x); });
    EXPECT_EQ(result.verdict, Verdict::Unknown) << result.message;
    EXPECT_EQ(exitCode(result), 4);
    EXPECT_EQ(result.message,
              "the avoid waypoint of segment 1 (assumption at 6:5) has no value at 6:5: division by zero");
    // A false conjunct before it ends the evaluation, as && does.
    EXPECT_EQ(validate(sequence({{avoid(WaypointType::Assumption, 6, 5, "a == 1 && a / 0 == 1"),
                                  follow(WaypointType::Branching, 6, 5, "false")},
                                 {target(9, 5)}}),
                       [](FakeExecution& x) { return runInputs(x); })
                  .verdict,
              Verdict::TargetWithoutError);
}

TEST_F(MonitorTest, APassingAvoidWaypointRejectsTheExecution) {
    program(std::string(inputsProgram));
    // The outcome has no term, so no input can change it.
    const ValidationResult result =
        validate(sequence({{avoid(WaypointType::Branching, 6, 5, "false"), follow(WaypointType::Assumption, 9, 5, "1")},
                           {target(9, 5)}}),
                 [](FakeExecution& x) { return runInputs(x, false); });
    EXPECT_EQ(result.verdict, Verdict::LeftWitness) << result.message;
    EXPECT_EQ(result.message,
              "the execution left the witness: the avoid waypoint of segment 1 (branching at 6:5) passed at 6:5");
}

TEST_F(MonitorTest, APassingAvoidBranchingChangesTheInputsOfItsCondition) {
    program(std::string(inputsProgram));
    // `a == 5` is false with a = 0, which the avoid waypoint excludes.
    const ValidationResult result =
        validate(sequence({{avoid(WaypointType::Branching, 6, 5, "false"), follow(WaypointType::Assumption, 7, 9, "1")},
                           {target(7, 9)}}),
                 [](FakeExecution& x) { return runInputs(x); });
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message;
    EXPECT_EQ(result.runs, 2u);
    EXPECT_TRUE(log().contains("run 1: the avoid waypoint of segment 1 (branching at 6:5) needs `in0 == 5`; input 0 "
                               ":= 5 (inversion)"))
        << log();
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

TEST_F(MonitorTest, APassingAvoidConstraintOnAnUnfixedInputFixesItToAnotherValue) {
    program(std::string(inputsProgram));
    // a has its default 0, so the avoid waypoint passes; with a fixed to 1, it does not.
    const ValidationResult result = validate(
        sequence({{avoid(WaypointType::Assumption, 6, 5, "a == 0"), follow(WaypointType::Branching, 6, 5, "false")},
                  {target(9, 5)}}),
        [](FakeExecution& x) { return runInputs(x); });
    EXPECT_EQ(result.verdict, Verdict::TargetWithoutError) << result.message;
    EXPECT_EQ(result.runs, 2u);
    EXPECT_TRUE(log().contains("run 1: the avoid waypoint of segment 1 (assumption at 6:5) passed at 6:5")) << log();
    EXPECT_TRUE(log().contains("run 1: fixing input 0 (the call of __VERIFIER_nondet_int at 4:35, occurrence 0) to 1"))
        << log();
}

TEST_F(MonitorTest, AnAvoidConstraintWithoutRelationFixesItsValueToZero) {
    program(std::string(inputsProgram));
    options_.defaultInput = 1;
    // The constant 1 has no term; b is an input and gets 0.
    const ValidationResult result = validate(
        sequence({{avoid(WaypointType::Assumption, 6, 5, "1 && b"), follow(WaypointType::Branching, 6, 5, "false")},
                  {target(9, 5)}}),
        [](FakeExecution& x) { return runInputs(x); });
    EXPECT_EQ(result.verdict, Verdict::TargetWithoutError) << result.message;
    EXPECT_EQ(result.runs, 2u);
    EXPECT_TRUE(log().contains("fixing input 1 (the call of __VERIFIER_nondet_int at 5:35, occurrence 0) to 0"))
        << log();
}

TEST_F(MonitorTest, AFollowConstraintWithoutRelationFixesItsValueToOne) {
    program(std::string(inputsProgram));
    const ValidationResult result = validate(sequence({{follow(WaypointType::Assumption, 6, 5, "b")}, {target(9, 5)}}),
                                             [](FakeExecution& x) { return runInputs(x); });
    EXPECT_EQ(result.verdict, Verdict::TargetWithoutError) << result.message;
    EXPECT_EQ(result.runs, 2u);
    EXPECT_TRUE(log().contains("fixing input 1 (the call of __VERIFIER_nondet_int at 5:35, occurrence 0) to 1"))
        << log();
}

TEST_F(MonitorTest, AnAvoidConstraintOnAFixedInputRejects) {
    program(std::string(inputsProgram));
    // The first run fixes a to 5, so that the avoid waypoint passes in the second run.
    const ValidationResult result =
        validate(sequence({{follow(WaypointType::Assumption, 6, 5, "a == 5")},
                           {avoid(WaypointType::Assumption, 7, 9, "a == 5"), target(7, 9)}}),
                 [](FakeExecution& x) { return runInputs(x); });
    EXPECT_EQ(result.verdict, Verdict::LeftWitness) << result.message;
    EXPECT_EQ(result.runs, 2u);
    EXPECT_EQ(result.message,
              "the execution left the witness: the avoid waypoint of segment 2 (assumption at 7:9) passed at 7:9");
}

TEST_F(MonitorTest, TheFollowWaypointIsEvaluatedBeforeTheAvoidWaypoints) {
    program(std::string(inputsProgram));
    // The follow waypoint fixes a to 5 first; then the avoid waypoint at the same event fails.
    const ValidationResult result = validate(
        sequence({{avoid(WaypointType::Assumption, 6, 5, "a == 0"), follow(WaypointType::Assumption, 6, 5, "a == 5")},
                  {target(7, 9)}}),
        [](FakeExecution& x) { return runInputs(x); });
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message;
    EXPECT_EQ(result.runs, 2u);
    EXPECT_TRUE(log().contains("run 1: the follow waypoint of segment 1 (assumption at 6:5) failed at 6:5")) << log();
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

TEST_F(MonitorTest, ACascadedAssumptionMustPassAtTheSameExecutionOfTheStatement) {
    program(std::string(loopProgram));
    // i == 1 only holds at the second execution of line 6.
    const ValidationResult result = validate(sequence({{follow(WaypointType::Assumption, 6, 9, "i == 0")},
                                                       {follow(WaypointType::Assumption, 6, 9, "i == 1")},
                                                       {follow(WaypointType::Assumption, 6, 9, "i == 2")},
                                                       {follow(WaypointType::Branching, 5, 5, "false")},
                                                       {follow(WaypointType::Branching, 8, 5, "true")},
                                                       {target(8, 17)}}),
                                             runLoop);
    EXPECT_EQ(result.verdict, Verdict::LeftWitness) << result.message;
    EXPECT_EQ(result.message, "the execution left the witness: the follow waypoint of segment 2 (assumption at 6:9) "
                              "did not pass at its first evaluation point, 6:9");
}

TEST_F(MonitorTest, AssumptionsAtOneStatementInDifferentIterationsAreSeparatedByOtherWaypoints) {
    program(std::string(loopProgram));
    const ValidationResult result = validate(sequence({{follow(WaypointType::Assumption, 6, 9, "i == 0")},
                                                       {follow(WaypointType::Branching, 5, 5, "true")},
                                                       {follow(WaypointType::Assumption, 6, 9, "i == 1")},
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

TEST_F(MonitorTest, AvoidWaypointsAreEvaluatedWhereTheirSegmentBecameCurrent) {
    program(std::string(twoIfsProgram));
    const ValidationResult result = validate(
        sequence({{follow(WaypointType::Assumption, 6, 5, "1")},
                  {avoid(WaypointType::Assumption, 6, 5, "b == 1"), follow(WaypointType::Branching, 6, 5, "false")},
                  {target(6, 34)}}),
        runTwoIfs);
    EXPECT_EQ(result.verdict, Verdict::LeftWitness) << result.message;
    EXPECT_EQ(result.message,
              "the execution left the witness: the avoid waypoint of segment 2 (assumption at 6:5) passed at 6:5");
}

TEST_F(MonitorTest, TheAvoidWaypointsOfTheTargetSegmentAreEvaluatedWhereItBecameCurrent) {
    program(std::string(twoIfsProgram));
    const ValidationResult result =
        validate(sequence({{follow(WaypointType::Assumption, 6, 34, "1")},
                           {avoid(WaypointType::Assumption, 6, 34, "b == 1"), target(6, 34)}}),
                 runTwoIfs);
    EXPECT_EQ(result.verdict, Verdict::LeftWitness) << result.message;
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

TEST_F(MonitorTest, AnAvoidedDefaultOfAnUnfixedInputFixesItToACaseValue) {
    program(std::string(switchProgram));
    // a is 0 by default and takes the default branch; fixed to the first case value, it returns 1.
    const ValidationResult result = validate(
        sequence({{avoid(WaypointType::Branching, 5, 5, "default"), follow(WaypointType::Assumption, 9, 9, "1")},
                  {target(9, 9)}}),
        [this](FakeExecution& x) { return runSwitch(*this, x); });
    EXPECT_EQ(result.verdict, Verdict::ProgramFinished) << result.message;
    EXPECT_EQ(result.runs, 2u);
    EXPECT_TRUE(log().contains("fixing input 0 (the call of __VERIFIER_nondet_int at 4:35, occurrence 0) to 1"))
        << log();
}

TEST_F(MonitorTest, AnAvoidedCaseValueOfAnUnfixedInputFixesItToAnotherValue) {
    program(std::string(switchProgram));
    // a is 0 by default, the avoided value; fixed to 1, it takes case 1.
    const ValidationResult result =
        validate(sequence({{avoid(WaypointType::Branching, 5, 5, "0"), follow(WaypointType::Assumption, 6, 5, "1")},
                           {target(9, 9)}}),
                 [this](FakeExecution& x) { return runSwitch(*this, x); });
    EXPECT_EQ(result.verdict, Verdict::TargetWithoutError) << result.message;
    EXPECT_EQ(result.runs, 2u);
    EXPECT_TRUE(log().contains("fixing input 0 (the call of __VERIFIER_nondet_int at 4:35, occurrence 0) to 1"))
        << log();
}

TEST_F(MonitorTest, TheFixOfAnAvoidedSwitchAvoidsAllAvoidedLabels) {
    program(std::string(switchProgram));
    // Default and case 1 are avoided: a goes to 2, not to 1, which the first case value would be.
    const ValidationResult result =
        validate(sequence({{avoid(WaypointType::Branching, 5, 5, "1"), avoid(WaypointType::Branching, 5, 5, "default"),
                            target(9, 9)}}),
                 [this](FakeExecution& x) { return runSwitch(*this, x); });
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message;
    EXPECT_EQ(result.runs, 2u);
    EXPECT_TRUE(log().contains("fixing input 0 (the call of __VERIFIER_nondet_int at 4:35, occurrence 0) to 2"))
        << log();
}

TEST_F(MonitorTest, AFollowedDefaultThatIsAlsoAvoidedRejects) {
    program(std::string(switchProgram));
    // The follow waypoint passes and keeps a != 1 and a != 2, so no value lets the avoid waypoint fail.
    const ValidationResult result = validate(
        sequence({{avoid(WaypointType::Branching, 5, 5, "default"), follow(WaypointType::Branching, 5, 5, "default")},
                  {target(12, 9)}}),
        [this](FakeExecution& x) { return runSwitch(*this, x); });
    EXPECT_EQ(result.verdict, Verdict::LeftWitness) << result.message;
    EXPECT_EQ(result.runs, 1u);
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

// ---- The search ----

TEST_F(MonitorTest, ABranchingOnAnInputGetsItsOutcome) {
    // README: `if (tmp)` after `tmp = __VERIFIER_nondet_int()`.
    program(R"(extern int __VERIFIER_nondet_int(void);
void reach_error(void);
int main(void) {
    int tmp = __VERIFIER_nondet_int();
    if (tmp) reach_error();
    return 0;
}
)");
    const auto script = [](FakeExecution& x) {
        x.statement(4, 5);
        x.assign("tmp", x.nondet(4, 37));
        x.statement(5, 5);
        if (x.branch(5, 5)) {
            x.statement(5, 14);
            x.call(5, 26);
        }
        x.statement(6, 5);
        return returned(0);
    };
    const ValidationResult result =
        validate(sequence({{follow(WaypointType::Branching, 5, 5, "true")}, {target(5, 14)}}), script);
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message;
    EXPECT_EQ(result.runs, 2u);
    EXPECT_TRUE(log().contains("run 1: the follow waypoint of segment 1 (branching at 5:5) failed at 5:5\n"
                               "run 1: the follow waypoint of segment 1 (branching at 5:5) needs `in0 != 0`; input 0 "
                               ":= 1 (inversion)\n"
                               "run 1: fixing input 0 (the call of __VERIFIER_nondet_int at 4:37, occurrence 0) to 1 "
                               "(inversion)\n"))
        << log();
}

TEST_F(MonitorTest, AComputedConditionIsSolvedThroughTheComputation) {
    program(R"(extern int __VERIFIER_nondet_int(void);
void reach_error(void);
int main(void) {
    int a = __VERIFIER_nondet_int();
    int b = a - 10;
    if (b == 5) reach_error();
    return 0;
}
)");
    const auto script = [](FakeExecution& x) {
        x.statement(4, 5);
        x.assign("a", x.nondet(4, 35));
        x.execute(5, 5);
        x.statement(6, 5);
        if (x.branch(6, 5)) {
            x.statement(6, 17);
            x.call(6, 29);
        }
        x.statement(7, 5);
        return returned(0);
    };
    const ValidationResult result =
        validate(sequence({{follow(WaypointType::Branching, 6, 5, "true")}, {target(6, 17)}}), script);
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message;
    EXPECT_EQ(result.runs, 2u);
    EXPECT_TRUE(log().contains("needs `(in0 - 10) == 5`; input 0 := 15 (inversion)")) << log();
}

constexpr std::string_view countingProgram = R"(extern int __VERIFIER_nondet_int(void);
void reach_error(void);
int main(void) {
    int a = __VERIFIER_nondet_int();
    int i = 0;
    while (i <= a) {
        i = i + 1;
    }
    reach_error();
    return 0;
}
)";

interp::RunResult runCounting(FakeExecution& x) {
    x.statement(4, 5);
    x.assign("a", x.nondet(4, 35));
    x.execute(5, 5);
    x.statement(6, 5);
    while (x.branch(6, 5)) {
        x.execute(7, 9);
    }
    x.statement(9, 5);
    x.call(9, 17);
    x.statement(10, 5);
    return returned(0);
}

/// A witness that passes the loop condition `iterations` times and then leaves the loop.
witness::YamlWitness countingWitness(int iterations) {
    std::vector<std::vector<witness::Waypoint>> segments;
    for (int i = 0; i < iterations; ++i) {
        segments.push_back({follow(WaypointType::Branching, 6, 5, "true")});
    }
    segments.push_back({follow(WaypointType::Branching, 6, 5, "false")});
    segments.push_back({target(9, 5)});
    return sequence(std::move(segments));
}

TEST_F(MonitorTest, ALoopCounterBoundedByAnInputKeepsTheIterationsItPassed) {
    // README: with a = 100, the run passes `i <= a` for i = 0 to 32 and keeps a >= 0 ... a >= 32; then a < 33.
    program(std::string(countingProgram));
    options_.defaultInput = 100;
    const ValidationResult result = validate(countingWitness(33), runCounting);
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message;
    EXPECT_EQ(result.runs, 2u);
    EXPECT_TRUE(log().contains("needs `!(33 <= in0)`; input 0 := 32 (inversion)")) << log();
}

TEST_F(MonitorTest, ALoopCounterNeedsARestartPerIterationThatFails) {
    program(std::string(countingProgram));
    const ValidationResult result = validate(countingWitness(3), runCounting);
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message;
    EXPECT_EQ(result.runs, 3u); // a becomes 1, then 2
}

TEST_F(MonitorTest, ARunKeepsClausesUpToALimit) {
    // With a = 100, the loop passes `i <= a` 4 times; one atom is kept, which the log says once.
    program(std::string(countingProgram));
    options_.defaultInput = 100;
    options_.maxKeptAtoms = 1;
    const ValidationResult result = validate(countingWitness(4), runCounting);
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message;
    EXPECT_EQ(result.runs, 2u);
    const std::string message = "run 1: the kept clauses reach the limit of their atoms; no more in this run";
    EXPECT_TRUE(log().contains(message)) << log();
    EXPECT_EQ(log().find(message), log().rfind(message)) << log();
}

TEST_F(MonitorTest, TheInputCheckOfRersAndThenTheInput) {
    program(R"(extern int __VERIFIER_nondet_int(void);
void reach_error(void);
int main(void) {
    int input = __VERIFIER_nondet_int();
    if ((input != 1) && (input != 2) && (input != 3)) return -2;
    if (input == 3) reach_error();
    return 0;
}
)");
    const auto script = [](FakeExecution& x) {
        x.statement(4, 5);
        x.assign("input", x.nondet(4, 39));
        x.statement(5, 5);
        if (x.branch(5, 5)) {
            x.statement(5, 55);
            return returned(-2);
        }
        x.statement(6, 5);
        if (x.branch(6, 5)) {
            x.statement(6, 21);
            x.call(6, 33);
        }
        x.statement(7, 5);
        return returned(0);
    };
    const ValidationResult result = validate(sequence({{follow(WaypointType::Branching, 5, 5, "false")},
                                                       {follow(WaypointType::Branching, 6, 5, "true")},
                                                       {target(6, 21)}}),
                                             script);
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message;
    EXPECT_EQ(result.runs, 3u);
    EXPECT_TRUE(log().contains("run 1: fixing input 0 (the call of __VERIFIER_nondet_int at 4:39, occurrence 0) to 1 "
                               "(inversion)"))
        << log();
    EXPECT_TRUE(log().contains("run 2: fixing input 0 (the call of __VERIFIER_nondet_int at 4:39, occurrence 0) to 3 "
                               "(inversion)"))
        << log();
}

TEST_F(MonitorTest, AFixedInputGetsAnotherValueThatKeepsThePassedWaypoints) {
    program(R"(extern int __VERIFIER_nondet_int(void);
void reach_error(void);
int main(void) {
    int x = __VERIFIER_nondet_int();
    if (x > 5) {
        if (x != 6) reach_error();
    }
    return 0;
}
)");
    const auto script = [](FakeExecution& x) {
        x.statement(4, 5);
        x.assign("x", x.nondet(4, 35));
        x.statement(5, 5);
        if (x.branch(5, 5)) {
            x.statement(6, 9);
            if (x.branch(6, 9)) {
                x.statement(6, 21);
                x.call(6, 33);
            }
        }
        x.statement(8, 5);
        return returned(0);
    };
    const ValidationResult result = validate(sequence({{follow(WaypointType::Branching, 5, 5, "true")},
                                                       {follow(WaypointType::Branching, 6, 9, "true")},
                                                       {target(6, 21)}}),
                                             script);
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message;
    EXPECT_EQ(result.runs, 3u);
    // x != 6 alone would give 0, which x > 5 excludes.
    EXPECT_TRUE(log().contains("run 2: fixing input 0 (the call of __VERIFIER_nondet_int at 4:35, occurrence 0) to 7 "
                               "(candidate)"))
        << log();
}

constexpr std::string_view overflowProgram = R"(extern int __VERIFIER_nondet_int(void);
void reach_error(void);
int main(void) {
    int a = __VERIFIER_nondet_int();
    if (a + 1 < -2147483647) reach_error();
    return 0;
}
)";

interp::RunResult runOverflow(FakeExecution& x) {
    x.statement(4, 5);
    x.assign("a", x.nondet(4, 35));
    x.statement(5, 5);
    if (x.branch(5, 5)) {
        x.statement(5, 30);
        x.call(5, 42);
    }
    x.statement(6, 5);
    return returned(0);
}

TEST_F(MonitorTest, TheProgramContinuesAfterSignedOverflowButAConstraintDoesNot) {
    program(std::string(overflowProgram));
    // In the program, INT_MAX + 1 wraps around to INT_MIN.
    const ValidationResult branching =
        validate(sequence({{follow(WaypointType::Branching, 5, 5, "true")}, {target(5, 30)}}), runOverflow);
    EXPECT_EQ(branching.verdict, Verdict::Confirmed) << branching.message;
    EXPECT_EQ(branching.runs, 2u);
    // In a constraint, it has no value.
    const ValidationResult assumption = validate(
        sequence({{follow(WaypointType::Assumption, 5, 5, "a + 1 < -2147483647")}, {target(5, 30)}}), runOverflow);
    EXPECT_EQ(assumption.verdict, Verdict::LeftWitness) << assumption.message;
    EXPECT_EQ(assumption.runs, 1u);
}

TEST_F(MonitorTest, AnInputIsNotChangedBackAndForthBetweenTwoWaypoints) {
    // x == 1 || x == 5, then x != 1: x = 1 passes the first, x = 0 the second, and the search must not return to 1.
    program(R"(extern int __VERIFIER_nondet_int(void);
void reach_error(void);
int main(void) {
    int x = __VERIFIER_nondet_int();
    if (x == 1 || x == 5) {
        if (x != 1) reach_error();
    }
    return 0;
}
)");
    const auto script = [](FakeExecution& x) {
        x.statement(4, 5);
        x.assign("x", x.nondet(4, 35));
        x.statement(5, 5);
        if (x.branch(5, 5)) {
            x.statement(6, 9);
            if (x.branch(6, 9)) {
                x.statement(6, 21);
                x.call(6, 33);
            }
        }
        x.statement(8, 5);
        return returned(0);
    };
    const ValidationResult result = validate(sequence({{follow(WaypointType::Branching, 5, 5, "true")},
                                                       {follow(WaypointType::Branching, 6, 9, "true")},
                                                       {target(6, 21)}}),
                                             script);
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message << "\n" << log();
    EXPECT_EQ(result.runs, 4u); // x becomes 1, 0, then 5
}

TEST_F(MonitorTest, ARunWithTheInputsOfAnEarlierRunIsNotMade) {
    // y is hinted 0; x == 1 || y == 1 needs x == 1, which x != 1 excludes. x becomes 1 between other values the search
    // tries for x != 1: 0, 2, -1, the extremes of int and their neighbours, then 0 again, which is no new value. The
    // run with x = 1 after it would be run 16 again, and so would the search after it.
    program(R"(extern int __VERIFIER_nondet_int(void);
void reach_error(void);
int main(void) {
    int x = __VERIFIER_nondet_int();
    int y = __VERIFIER_nondet_int();
    if (x == 1 || y == 1) {
        if (x != 1) reach_error();
    }
    return 0;
}
)");
    const auto script = [](FakeExecution& x) {
        x.statement(4, 5);
        x.assign("x", x.nondet(4, 35));
        x.statement(5, 5);
        x.assign("y", x.nondet(5, 35));
        x.statement(6, 5);
        if (x.branch(6, 5)) {
            x.statement(7, 9);
            if (x.branch(7, 9)) {
                x.statement(7, 21);
                x.call(7, 33);
            }
        }
        x.statement(9, 5);
        return returned(0);
    };
    const ValidationResult result = validate(sequence({{follow(WaypointType::FunctionReturn, 5, 35, "\\result == 0")},
                                                       {follow(WaypointType::Branching, 6, 5, "true")},
                                                       {follow(WaypointType::Branching, 7, 9, "true")},
                                                       {target(7, 21)}}),
                                             script);
    EXPECT_EQ(result.verdict, Verdict::LeftWitness) << result.message << "\n" << log();
    EXPECT_EQ(result.runs, 17u) << log();
    EXPECT_EQ(result.message, "the execution left the witness: the follow waypoint of segment 2 (branching at 6:5) "
                              "failed at 6:5; the search proposes the inputs of run 16 again");
}

TEST_F(MonitorTest, ARunWithTheInputsOfAnEarlierRunIsMadeIfValuesWereTriedSince) {
    // ok depends on x through control flow only, so x = 2 is no prediction, but the third candidate of the search
    // once 1 and 0 were tried. x becomes 1, 0, 1 (as in run 2, but with 0 tried since), then 2.
    program(R"(extern int __VERIFIER_nondet_int(void);
void reach_error(void);
int main(void) {
    int x = __VERIFIER_nondet_int();
    int ok = 0;
    if (x == 2) ok = 1;
    if (x == 1 || ok) {
        if (x != 1) reach_error();
    }
    return 0;
}
)");
    const auto script = [](FakeExecution& x) {
        x.statement(4, 5);
        x.assign("x", x.nondet(4, 35));
        x.statement(5, 5);
        x.assign("ok", x.number("ok", 0));
        x.statement(6, 5);
        if (x.branch(6, 5)) {
            x.statement(6, 17);
            x.assign("ok", x.number("ok", 1));
        }
        x.statement(7, 5);
        if (x.branch(7, 5)) {
            x.statement(8, 9);
            if (x.branch(8, 9)) {
                x.statement(8, 21);
                x.call(8, 33);
            }
        }
        x.statement(10, 5);
        return returned(0);
    };
    const ValidationResult result = validate(sequence({{follow(WaypointType::Branching, 7, 5, "true")},
                                                       {follow(WaypointType::Branching, 8, 9, "true")},
                                                       {target(8, 21)}}),
                                             script);
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message << "\n" << log();
    EXPECT_EQ(result.runs, 5u);
}

TEST_F(MonitorTest, ABlockingKeptClauseIsRepairedThroughAnotherInput) {
    // x + y == 10 keeps y = 10 from the first run; y == 3 then needs x = 7.
    program(R"(extern int __VERIFIER_nondet_int(void);
void reach_error(void);
int main(void) {
    int x = __VERIFIER_nondet_int();
    int y = __VERIFIER_nondet_int();
    if (x + y == 10) {
        if (y == 3) reach_error();
    }
    return 0;
}
)");
    const auto script = [](FakeExecution& x) {
        x.statement(4, 5);
        x.assign("x", x.nondet(4, 35));
        x.statement(5, 5);
        x.assign("y", x.nondet(5, 35));
        x.statement(6, 5);
        if (x.branch(6, 5)) {
            x.statement(7, 9);
            if (x.branch(7, 9)) {
                x.statement(7, 21);
                x.call(7, 33);
            }
        }
        x.statement(9, 5);
        return returned(0);
    };
    const ValidationResult result = validate(sequence({{follow(WaypointType::Branching, 6, 5, "true")},
                                                       {follow(WaypointType::Branching, 7, 9, "true")},
                                                       {target(7, 21)}}),
                                             script);
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message << "\n" << log();
    EXPECT_EQ(result.runs, 3u);
    EXPECT_TRUE(log().contains("needs `in1 == 3`; input 1 := 3 (inversion), input 0 := 7 (inversion)")) << log();
}

constexpr std::string_view rangeProgram = R"(extern int __VERIFIER_nondet_int(void);
void reach_error(void);
int main(void) {
    int x = __VERIFIER_nondet_int();
    switch (x) {
    case 1 ... 5:
        break;
    default:
        return 0;
    }
    if (x == 4) reach_error();
    return 0;
}
)";

interp::RunResult runRange(const ValidateTest& fixture, FakeExecution& x) {
    x.statement(4, 5);
    x.assign("x", x.nondet(4, 35));
    x.statement(5, 5);
    if (fixture.lineOf(x.switchOn(5, 5)) == 8) {
        x.statement(9, 9);
        return returned(0);
    }
    x.statement(11, 5);
    if (x.branch(11, 5)) {
        x.statement(11, 17);
        x.call(11, 29);
    }
    x.statement(12, 5);
    return returned(0);
}

TEST_F(MonitorTest, AnAvoidedDefaultKeepsTheRangeTheRunTook) {
    // The first run takes the default; x = 1 takes the range, which any value in it keeps, such as 4.
    program(std::string(rangeProgram));
    const ValidationResult result = validate(
        sequence({{avoid(WaypointType::Branching, 5, 5, "default"), follow(WaypointType::Branching, 11, 5, "true")},
                  {target(11, 17)}}),
        [this](FakeExecution& x) { return runRange(*this, x); });
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message << "\n" << log();
    EXPECT_EQ(result.runs, 3u);
}

TEST_F(MonitorTest, ACaseValueInARangeTakesTheRangesLabel) {
    // Case value 3 is the label `case 1 ... 5`, which x = 1 takes too.
    program(std::string(rangeProgram));
    const ValidationResult result = validate(sequence({{follow(WaypointType::Branching, 5, 5, "3")},
                                                       {follow(WaypointType::Branching, 11, 5, "true")},
                                                       {target(11, 17)}}),
                                             [this](FakeExecution& x) { return runRange(*this, x); });
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message << "\n" << log();
    EXPECT_EQ(result.runs, 3u);
    EXPECT_TRUE(log().contains("run 2: segment 1 of 3 passed at 5:5 (branching)")) << log();
}

TEST_F(MonitorTest, AnAvoidedCaseValueInARangeAvoidsTheRange) {
    // x is hinted 3, which takes the label of the avoided case value 2.
    program(std::string(rangeProgram));
    const ValidationResult result =
        validate(sequence({{follow(WaypointType::FunctionReturn, 4, 35, "\\result == 3")},
                           {avoid(WaypointType::Branching, 5, 5, "2"), follow(WaypointType::Branching, 11, 5, "true")},
                           {target(11, 17)}}),
                 [this](FakeExecution& x) { return runRange(*this, x); });
    EXPECT_EQ(result.verdict, Verdict::LeftWitness) << result.message << "\n" << log();
    EXPECT_EQ(result.message,
              "the execution left the witness: the avoid waypoint of segment 2 (branching at 5:5) passed at 5:5");
}

constexpr std::string_view guardedSwitchProgram = R"(extern int __VERIFIER_nondet_int(void);
void reach_error(void);
int main(void) {
    int a = __VERIFIER_nondet_int();
    if (a < 0) return 0;
    switch (a) {
    case 0: return 1;
    case 1: return 2;
    default: reach_error();
    }
    return 0;
}
)";

interp::RunResult runGuardedSwitch(const ValidateTest& fixture, FakeExecution& x) {
    x.statement(4, 5);
    x.assign("a", x.nondet(4, 35));
    x.statement(5, 5);
    if (x.branch(5, 5)) {
        x.statement(5, 16);
        return returned(0);
    }
    x.statement(6, 5);
    const std::uint32_t line = fixture.lineOf(x.switchOn(6, 5));
    if (line == 7 || line == 8) {
        return returned(line == 7 ? 1 : 2);
    }
    x.statement(9, 14);
    x.call(9, 26);
    x.statement(11, 5);
    return returned(0);
}

TEST_F(MonitorTest, AvoidedCasesGiveTheSmallestNonNegativeValueOfNoCase) {
    // Negative values do not reach the switch. Case 1 is avoided before case 0, whose value a has at first.
    program(std::string(guardedSwitchProgram));
    const ValidationResult result = validate(sequence({{avoid(WaypointType::Branching, 6, 5, "1"),
                                                        avoid(WaypointType::Branching, 6, 5, "0"), target(9, 14)}}),
                                             [this](FakeExecution& x) { return runGuardedSwitch(*this, x); });
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message << "\n" << log();
    EXPECT_EQ(result.runs, 2u);
    EXPECT_TRUE(log().contains("to 2 (candidate)")) << log();
}

TEST_F(MonitorTest, AFollowedDefaultGetsTheSmallestNonNegativeValueOfNoCase) {
    program(std::string(guardedSwitchProgram));
    const ValidationResult result =
        validate(sequence({{follow(WaypointType::Branching, 6, 5, "default")}, {target(9, 14)}}),
                 [this](FakeExecution& x) { return runGuardedSwitch(*this, x); });
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message << "\n" << log();
    EXPECT_EQ(result.runs, 2u);
}

// ---- Guards ----

constexpr std::string_view abortGuardProgram = R"(extern int __VERIFIER_nondet_int(void);
extern void abort(void);
void reach_error(void);
int main(void) {
    int a = __VERIFIER_nondet_int();
    if (!(a > 0)) abort();
    if (a != 1) reach_error();
    return 0;
}
)";

interp::RunResult runAbortGuard(FakeExecution& x) {
    x.statement(5, 5);
    x.assign("a", x.nondet(5, 35));
    x.statement(6, 5);
    if (x.branch(6, 5)) {
        x.statement(6, 19);
        x.call(6, 25);
        return endedBy(interp::Termination::Abort, "abort was called");
    }
    x.statement(7, 5);
    if (x.branch(7, 5)) {
        x.statement(7, 17);
        x.call(7, 29);
    }
    x.statement(8, 5);
    return returned(0);
}

TEST_F(MonitorTest, AGuardThatWouldEndTheRunRestartsWithInputsThatPassIt) {
    // With a = 0 the run would abort; with a = 1 it returns in the target segment.
    program(std::string(abortGuardProgram));
    const ValidationResult result = validate(sequence({{target(7, 17)}}), runAbortGuard);
    EXPECT_EQ(result.verdict, Verdict::TargetWithoutError) << result.message << "\n" << log();
    EXPECT_EQ(result.runs, 2u);
    EXPECT_TRUE(log().contains("run 1: the guard at 6:5 (abort) would end the run")) << log();
    EXPECT_TRUE(log().contains("run 1: the guard at 6:5 (abort) needs `!(!(in0 > 0))`; input 0 := 1 (inversion)"))
        << log();
}

TEST_F(MonitorTest, TheRequirementOfAPassedGuardIsKept) {
    // Run 2 passes the guard with a = 1, and the branching then needs a != 1: 0 would abort, so a := 2.
    program(std::string(abortGuardProgram));
    const ValidationResult result =
        validate(sequence({{follow(WaypointType::Branching, 7, 5, "true")}, {target(7, 17)}}), runAbortGuard);
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message << "\n" << log();
    EXPECT_EQ(result.runs, 3u);
    EXPECT_TRUE(log().contains("input 0 := 2 (candidate)")) << log();
}

TEST_F(MonitorTest, AGuardThatTheSearchCannotPassLetsTheRunEnd) {
    // The assumption keeps a == 0, so no input passes the guard, and the run aborts as without it.
    program(std::string(abortGuardProgram));
    const ValidationResult result =
        validate(sequence({{follow(WaypointType::Assumption, 6, 5, "a == 0")}, {target(7, 17)}}), runAbortGuard);
    EXPECT_EQ(result.verdict, Verdict::Unknown) << result.message << "\n" << log();
    EXPECT_EQ(result.message, "the program aborted in segment 2 of 2");
    EXPECT_EQ(result.runs, 1u);
    EXPECT_TRUE(log().contains("run 1: the guard at 6:5 (abort) needs `!(!(in0 > 0))`: no input values found; the "
                               "run goes on"))
        << log();
}

TEST_F(MonitorTest, AWitnessWaypointAtAGuardWins) {
    // As Goblint writes: the follow waypoint takes the branch into abort. Run 2 takes it, and the guard does not search
    // against the witness.
    program(std::string(abortGuardProgram));
    options_.defaultInput = 5;
    const ValidationResult result =
        validate(sequence({{follow(WaypointType::Branching, 6, 5, "true")}, {target(7, 17)}}), runAbortGuard);
    EXPECT_EQ(result.verdict, Verdict::Unknown) << result.message << "\n" << log();
    EXPECT_EQ(result.message, "the program aborted in segment 2 of 2");
    EXPECT_EQ(result.runs, 2u);
    EXPECT_FALSE(log().contains("guard")) << log();
}

TEST_F(MonitorTest, AGuardRestartAtTheRestartLimitIsRetried) {
    // The run stopped at the guard had no failed waypoint.
    program(std::string(abortGuardProgram));
    options_.maxRestarts = 0;
    options_.retryDefaultInput = 1;
    const ValidationResult result = validate(sequence({{target(7, 17)}}), runAbortGuard);
    EXPECT_EQ(result.verdict, Verdict::Unknown) << result.message << "\n" << log();
    EXPECT_EQ(result.message, "the limit of 0 restarts was reached: the guard at 6:5 (abort) would end the run");
    EXPECT_EQ(result.runs, 2u);
    EXPECT_TRUE(log().contains("run 1: no waypoint failed; validating again with inputs 1 by default")) << log();
}

constexpr std::string_view alternatingGuardsProgram = R"(extern int __VERIFIER_nondet_int(void);
extern void abort(void);
void reach_error(void);
int main(void) {
    int a = __VERIFIER_nondet_int();
    if (a == 0) if (a != 1) abort();
    if (a != 0) abort();
    reach_error();
    return 0;
}
)";

TEST_F(MonitorTest, AGuardThatProposesTheInputsOfAnEarlierRunEndsTheValidation) {
    // The guards need a == 1 and a == 0, each on the path where the other one is not evaluated: the outer if is neither
    // a guard nor a waypoint, so the search does not know that the first guard needs a == 0 too. Each requirement has
    // one value, so once both were tried, run 4 (a = 1) proposes a = 0, the inputs of run 3, again.
    program(std::string(alternatingGuardsProgram));
    const ValidationResult result = validate(sequence({{target(8, 5)}}), [](FakeExecution& x) {
        x.statement(5, 5);
        x.assign("a", x.nondet(5, 35));
        x.statement(6, 5);
        if (x.branch(6, 5)) {
            x.statement(6, 17);
            if (x.branch(6, 17)) {
                x.statement(6, 29);
                x.call(6, 35);
                return endedBy(interp::Termination::Abort, "abort was called");
            }
        }
        x.statement(7, 5);
        if (x.branch(7, 5)) {
            x.statement(7, 17);
            x.call(7, 23);
            return endedBy(interp::Termination::Abort, "abort was called");
        }
        x.statement(8, 5);
        x.call(8, 17);
        return returned(0);
    });
    EXPECT_EQ(result.verdict, Verdict::Unknown) << result.message << "\n" << log();
    EXPECT_EQ(result.message,
              "the guard at 7:5 (abort) would end the run; the search proposes the inputs of run 3 again")
        << log();
    EXPECT_EQ(result.runs, 4u);
}

constexpr std::string_view assumeProgram = R"(extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_assume(int);
void reach_error(void);
int main(void) {
    int a = __VERIFIER_nondet_int();
    __VERIFIER_assume(a != 7);
    if (a > 6) reach_error();
    return 0;
}
)";

interp::RunResult runAssume(FakeExecution& x) {
    x.statement(5, 5);
    x.assign("a", x.nondet(5, 35));
    x.statement(6, 5);
    if (!x.assume(6, 29)) {
        return endedBy(interp::Termination::AssumeFailed, "__VERIFIER_assume with a false condition");
    }
    x.statement(7, 5);
    if (x.branch(7, 5)) {
        x.statement(7, 16);
        x.call(7, 28);
    }
    x.statement(8, 5);
    return returned(0);
}

TEST_F(MonitorTest, AFailedAssumeAsksTheSearch) {
    program(std::string(assumeProgram));
    options_.defaultInput = 7;
    const ValidationResult result = validate(sequence({{target(7, 16)}}), runAssume);
    EXPECT_EQ(result.verdict, Verdict::TargetWithoutError) << result.message << "\n" << log();
    EXPECT_EQ(result.runs, 2u);
    EXPECT_TRUE(
        log().contains("run 1: the guard at 6:5 (__VERIFIER_assume) needs `in0 != 7`; input 0 := 0 (inversion)"))
        << log();
}

TEST_F(MonitorTest, TheRequirementOfAPassedAssumeIsKept) {
    // The branching needs a > 6, which gives 7 but for the kept a != 7.
    program(std::string(assumeProgram));
    const ValidationResult result =
        validate(sequence({{follow(WaypointType::Branching, 7, 5, "true")}, {target(7, 16)}}), runAssume);
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message << "\n" << log();
    EXPECT_EQ(result.runs, 2u);
    EXPECT_TRUE(log().contains("input 0 := 8 (candidate)")) << log();
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
