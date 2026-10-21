// Values from the witness (validate/README.md, "Values"): hints of function_return waypoints, and restarts that fix
// the inputs a failed follow constraint asks for (restart.hpp).

#include "validate/restart.hpp"

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

TEST_F(RestartTest, NoRestartsAtAll) {
    program(std::string(inputsProgram));
    options_.maxRestarts = 0;
    const ValidationResult result =
        validate(sequence({{follow(WaypointType::Assumption, 6, 5, "a == 5")}, {target(7, 9)}}),
                 [](FakeExecution& x) { return runInputs(x); });
    EXPECT_EQ(result.verdict, Verdict::Unknown) << result.message;
    EXPECT_EQ(result.runs, 1u);
}

} // namespace
} // namespace nitwit::validate::test
