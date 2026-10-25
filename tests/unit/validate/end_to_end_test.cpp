// validate() with the interpreter: the events of real runs at the locations producers write.

#include <string>
#include <string_view>
#include <utility>

#include <gtest/gtest.h>

#include "interp/interpreter.hpp"
#include "validate_test_support.hpp"

namespace nitwit::validate::test {
namespace {

class EndToEndTest : public ValidateTest {
protected:
    /// Analyzes and lowers the program; fails the test if that fails.
    void build(std::string text, DataModel model = DataModel::ILP32) {
        program(std::move(text), model);
        auto built = interp::buildProgram(unit(), context());
        ASSERT_TRUE(built.has_value()) << built.error().message;
        input_->program = *built;
    }

    ValidationResult validate(const witness::YamlWitness& witness) {
        ValidationOptions options = options_;
        options.log = &log_;
        // Checks every term against its value, also the strict terms of the constraints the monitor evaluates: a
        // mismatch ends the run as unsupported, which no test expects.
        options.run.checkTerms = &interp::termMismatch;
        return nitwit::validate::validate(witness, *input_, options, diagnostics_);
    }
};

TEST_F(EndToEndTest, AssumptionsFixInputsOfEveryType) {
    // testfiles/sv-comp-2019/06.nondet.c, as CPAchecker writes the witness.
    build(R"(extern void __VERIFIER_error(void);
extern unsigned short __VERIFIER_nondet_ushort(void);
extern char __VERIFIER_nondet_char(void);
extern double __VERIFIER_nondet_double(void);
int main(void) {
    unsigned short a = __VERIFIER_nondet_ushort();
    char b = __VERIFIER_nondet_char();
    double c = __VERIFIER_nondet_double();
    if (a == 1 && b == (char)-1 && c == 1.5)
        __VERIFIER_error();
    return 0;
}
)");
    options_.errorFunction = "__VERIFIER_error";
    const ValidationResult result = validate(sequence({{follow(WaypointType::Assumption, 7, 5, "(a == 1U)")},
                                                       {follow(WaypointType::Assumption, 8, 5, "(b == -1)")},
                                                       {follow(WaypointType::Assumption, 9, 5, "(c == 1.5)")},
                                                       {follow(WaypointType::Branching, 9, 5, "true")},
                                                       {target(10, 9)}}));
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message << "\n" << log();
    EXPECT_EQ(result.runs, 4u);
}

TEST_F(EndToEndTest, HintsAnswerTheCallsAtTheirParentheses) {
    build(R"(extern int __VERIFIER_nondet_int(void);
void reach_error(void) {}
int main(void) {
    int a = __VERIFIER_nondet_int(); int b = __VERIFIER_nondet_int();
    if (b == 7 && a != 7) {
        reach_error();
    }
    return 0;
}
)");
    const ValidationResult result = validate(sequence({{follow(WaypointType::FunctionReturn, 4, 68, "\\result == 7")},
                                                       {follow(WaypointType::Branching, 5, 5, "true")},
                                                       {target(6, 9)}}));
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message;
    EXPECT_EQ(result.runs, 1u);
}

TEST_F(EndToEndTest, TheTargetIsTheCallInsideALabeledBlock) {
    // The usual __VERIFIER_assert of sv-benchmarks; the interpreter reports the label and the block's statements.
    build(R"(extern int __VERIFIER_nondet_int(void);
void reach_error(void);
void abort(void);
void __VERIFIER_assert(int cond) { if (!(cond)) { ERROR: {reach_error();abort();} } }
int main(void) {
    int x = __VERIFIER_nondet_int();
    __VERIFIER_assert(x != 3);
    return 0;
}
)");
    const ValidationResult result = validate(sequence({{follow(WaypointType::FunctionReturn, 6, 35, "\\result == 3")},
                                                       {follow(WaypointType::Branching, 4, 36, "true")},
                                                       {target(4, 59)}}));
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message << "\n" << log();
}

TEST_F(EndToEndTest, ConditionalExpressionsAndSwitches) {
    build(R"(extern int __VERIFIER_nondet_int(void);
void reach_error(void);
int main(void) {
    int a = __VERIFIER_nondet_int() > 0 ? 1 : 0;
    int b = __VERIFIER_nondet_int();
    switch (b) {
    case 4:
        if (a) reach_error();
        break;
    default:
        break;
    }
    return 0;
}
)");
    const ValidationResult result = validate(sequence({{follow(WaypointType::FunctionReturn, 4, 35, "\\result > 0")},
                                                       {follow(WaypointType::Branching, 4, 41, "true")},
                                                       {follow(WaypointType::Branching, 6, 5, "4")},
                                                       {follow(WaypointType::Branching, 8, 9, "true")},
                                                       {target(8, 16)}}));
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message << "\n" << log();
    EXPECT_EQ(result.runs, 2u); // the case value fixes b
}

TEST_F(EndToEndTest, LoopConditionsAreEvaluatedOncePerIteration) {
    build(R"(void reach_error(void);
int main(void) {
    int i = 0;
    while (i < 2) {
        i++;
    }
    if (i == 2) reach_error();
    return 0;
}
)");
    const auto loop = [](std::string_view outcome) {
        return follow(WaypointType::Branching, 4, 5, outcome);
    };
    EXPECT_EQ(validate(sequence({{loop("true")}, {loop("true")}, {loop("false")}, {target(7, 17)}})).verdict,
              Verdict::Confirmed);
    EXPECT_EQ(validate(sequence({{loop("true")}, {loop("false")}, {target(7, 17)}})).verdict, Verdict::LeftWitness);
}

TEST_F(EndToEndTest, UninitializedVariablesAreInputs) {
    build(R"(void reach_error(void);
int main(void) {
    int x;
    if (x == 4) reach_error();
    return 0;
}
)");
    const ValidationResult result = validate(sequence({{follow(WaypointType::Assumption, 4, 5, "x == 4")},
                                                       {follow(WaypointType::Branching, 4, 5, "true")},
                                                       {target(4, 17)}}));
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message << "\n" << log();
    EXPECT_EQ(result.runs, 2u);
}

TEST_F(EndToEndTest, HowRunsEnd) {
    build(R"(extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_assume(int);
void exit(int);
void abort(void);
void reach_error(void);
int main(void) {
    int a = __VERIFIER_nondet_int();
    if (a == 1) exit(3);
    if (a == 2) abort();
    __VERIFIER_assume(a != 3);
    return 0;
}
)");
    const auto ending = [&](int value) {
        return validate(sequence(
            {{follow(WaypointType::FunctionReturn, 7, 35, std::format("\\result == {}", value))}, {target(8, 17)}}));
    };
    EXPECT_EQ(ending(1).verdict, Verdict::TargetWithoutError);
    EXPECT_EQ(ending(2).verdict, Verdict::Unknown);
    EXPECT_EQ(ending(3).verdict, Verdict::Unknown);
    EXPECT_EQ(ending(3).message, "__VERIFIER_assume failed at test.c:10:5, the execution is infeasible in segment 2 "
                                 "of 2");
    EXPECT_EQ(ending(4).verdict, Verdict::TargetWithoutError);
}

// ---- The search (validate/README.md, "Search") ----

TEST_F(EndToEndTest, ABranchingOnAnInputGetsItsOutcome) {
    build(R"(extern int __VERIFIER_nondet_int(void);
void reach_error(void);
int main(void) {
    int tmp = __VERIFIER_nondet_int();
    if (tmp) reach_error();
    return 0;
}
)");
    const ValidationResult result =
        validate(sequence({{follow(WaypointType::Branching, 5, 5, "true")}, {target(5, 14)}}));
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message << "\n" << log();
    EXPECT_EQ(result.runs, 2u);
}

TEST_F(EndToEndTest, ABranchingOnABoolInputChangesItThroughItsConversionToBool) {
    // The input of b has a conversion to _Bool over it, which the search inverts.
    build(R"(void reach_error(void);
int main(void) {
    _Bool b;
    if (b) reach_error();
    return 0;
}
)");
    const ValidationResult result =
        validate(sequence({{follow(WaypointType::Branching, 4, 5, "true")}, {target(4, 12)}}));
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message << "\n" << log();
    EXPECT_EQ(result.runs, 2u);
}

TEST_F(EndToEndTest, AConstraintOnAComputedValueIsSolvedThroughTheComputation) {
    // As validation-crafted/while.3A1: `a == 10` after `a = a - n`.
    build(R"(extern int __VERIFIER_nondet_int(void);
void reach_error(void);
int main(void) {
    int a = __VERIFIER_nondet_int();
    int n = 3;
    a = a - n;
    if (a == 10) reach_error();
    return 0;
}
)");
    const ValidationResult result = validate(sequence({{follow(WaypointType::Assumption, 7, 5, "a == 10")},
                                                       {follow(WaypointType::Branching, 7, 5, "true")},
                                                       {target(7, 18)}}));
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message << "\n" << log();
    EXPECT_EQ(result.runs, 2u);
    EXPECT_TRUE(log().contains("to 13 (inversion)")) << log();
}

TEST_F(EndToEndTest, TheSearchChangesTheLastInputWithATermOfARunWithMoreInputs) {
    // With maxTerms 4, the table holds 8 terms: the opaque term and inputs 0 to 6. Input 6 is the last input with a
    // term, which the oracle must keep (it keeps the first 8); the others have the opaque term.
    build(R"(extern int __VERIFIER_nondet_int(void);
void reach_error(void);
int main(void) {
    int c = 0;
    for (int i = 0; i < 100; i++) {
        int x = __VERIFIER_nondet_int();
        if (i == 6) c = x;
    }
    if (c) reach_error();
    return 0;
}
)");
    options_.run.terms.maxTerms = 4;
    const ValidationResult result =
        validate(sequence({{follow(WaypointType::Branching, 9, 5, "true")}, {target(9, 12)}}));
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message << "\n" << log();
    EXPECT_EQ(result.runs, 2u);
    EXPECT_TRUE(log().contains("fixing input 6 (the call of __VERIFIER_nondet_int at 6:39, occurrence 6) to 1"))
        << log();
}

TEST_F(EndToEndTest, AnEndlessLoopWithoutEffectEndsTheRunAsInfeasible) {
    // ldv_assume of the ldv-linux-4.0-rc1-mav programs, with an extern object that no file defines (zero here).
    build(R"(void reach_error(void);
void ldv_assume(int expression) {
  if (expression == 0) {
    ldv_assume_label: ;
    goto ldv_assume_label;
  }
}
int nr_cpu_ids;
int main(void) {
  ldv_assume(nr_cpu_ids > 0);
  reach_error();
  return 0;
}
)");
    const ValidationResult result = validate(sequence({{target(11, 3)}}));
    EXPECT_EQ(result.verdict, Verdict::Unknown) << result.message << "\n" << log();
    EXPECT_EQ(exitCode(result), 4);
    EXPECT_EQ(result.message, "the program entered an endless loop without effect at test.c:4:5, the execution is "
                              "infeasible in segment 1 of 1");
}

TEST_F(EndToEndTest, ARunThatEndsInAnEndlessLoopIsRetriedWithOtherDefaults) {
    // assume(x != 0) as an endless loop: with x = 0 the run is infeasible, with x = 1 it reaches the error.
    build(R"(extern int __VERIFIER_nondet_int(void);
void reach_error(void);
void assume(int c) {
  if (!c) {
    L: ;
    goto L;
  }
}
int main(void) {
  int x = __VERIFIER_nondet_int();
  assume(x != 0);
  reach_error();
  return 0;
}
)");
    options_.retryDefaultInput = 1;
    const ValidationResult result = validate(sequence({{target(12, 3)}}));
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message << "\n" << log();
    EXPECT_EQ(result.runs, 2u);
    EXPECT_TRUE(log().contains("run 1: no waypoint failed; one more run with inputs 1 by default")) << log();
}

TEST_F(EndToEndTest, ALoopCounterBoundedByAnInput) {
    build(R"(extern int __VERIFIER_nondet_int(void);
void reach_error(void);
int main(void) {
    int a = __VERIFIER_nondet_int();
    int i = 0;
    while (i <= a) {
        i++;
    }
    reach_error();
    return 0;
}
)");
    options_.defaultInput = 100;
    std::vector<std::vector<witness::Waypoint>> segments(33, {follow(WaypointType::Branching, 6, 5, "true")});
    segments.push_back({follow(WaypointType::Branching, 6, 5, "false")});
    segments.push_back({target(9, 5)});
    const ValidationResult result = validate(sequence(std::move(segments)));
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message << "\n" << log();
    EXPECT_EQ(result.runs, 2u);
}

TEST_F(EndToEndTest, TheInputCheckOfRersAndThenTheInput) {
    build(R"(extern int __VERIFIER_nondet_int(void);
void reach_error(void);
int main(void) {
    int input = __VERIFIER_nondet_int();
    if ((input != 1) && (input != 2) && (input != 3)) return -2;
    if (input == 3) reach_error();
    return 0;
}
)");
    const ValidationResult result = validate(sequence({{follow(WaypointType::Branching, 5, 5, "false")},
                                                       {follow(WaypointType::Branching, 6, 5, "true")},
                                                       {target(6, 21)}}));
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message << "\n" << log();
    EXPECT_EQ(result.runs, 3u);
}

TEST_F(EndToEndTest, TheResultOfADefinedFunctionIsSolvedThroughItsBody) {
    // 2 * x + 1 == 7 is inverted through the factor 2, which divides 6.
    build(R"(extern int __VERIFIER_nondet_int(void);
void reach_error(void);
int twice(int x) { return 2 * x + 1; }
int main(void) {
    int y = twice(__VERIFIER_nondet_int());
    if (y == 7) reach_error();
    return 0;
}
)");
    const ValidationResult result = validate(sequence({{follow(WaypointType::FunctionReturn, 5, 42, "\\result == 7")},
                                                       {follow(WaypointType::Branching, 6, 5, "true")},
                                                       {target(6, 17)}}));
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message << "\n" << log();
    EXPECT_EQ(result.runs, 2u);
    EXPECT_TRUE(log().contains("to 3 (inversion)")) << log();
}

TEST_F(EndToEndTest, AComparisonOfAnInputWithItselfFailsWithNan) {
    // As sv-benchmarks' nan_double: only NaN compares unequal to itself.
    build(R"(extern double __VERIFIER_nondet_double(void);
void reach_error(void);
int main(void) {
    double x = __VERIFIER_nondet_double();
    if (x == x) return 0;
    reach_error();
    return 0;
}
)");
    const ValidationResult result =
        validate(sequence({{follow(WaypointType::Branching, 5, 5, "false")}, {target(6, 5)}}));
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message << "\n" << log();
    EXPECT_EQ(result.runs, 2u);
    EXPECT_TRUE(log().contains("to nan (candidate)")) << log();
}

TEST_F(EndToEndTest, TruthValuesCombinedWithBitwiseAndAreSolvedForEachOperand) {
    // As btor2c writes a property: 1-bit values in unsigned char, combined with &.
    build(R"(extern unsigned char __VERIFIER_nondet_uchar(void);
void reach_error(void);
void abort(void);
void __VERIFIER_assert(int cond) { if (!(cond)) { ERROR: {reach_error();abort();} } }
int main(void) {
    unsigned char a = __VERIFIER_nondet_uchar();
    unsigned char b = __VERIFIER_nondet_uchar();
    unsigned char c = a == 3;
    unsigned char d = b == 5;
    unsigned char bad = c & d;
    __VERIFIER_assert(!(bad));
    return 0;
}
)");
    const ValidationResult result =
        validate(sequence({{follow(WaypointType::Branching, 4, 36, "true")}, {target(4, 59)}}));
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message << "\n" << log();
    EXPECT_EQ(result.runs, 2u);
    EXPECT_TRUE(log().contains("input 0 := 3 (inversion), input 1 := 5 (inversion)")) << log();
}

// ---- Invalid witnesses that an earlier version confirmed ----

constexpr std::string_view avoidProgram = R"(extern int __VERIFIER_nondet_int(void);
extern void reach_error(void);
int main(void) {
  int x = __VERIFIER_nondet_int();
  if (x == 0) {
    reach_error();
  }
  return 0;
}
)";

// The witnesses of the next three tests avoid the condition of the error, so no run may confirm them. The default
// input passes the avoid waypoint, the search changes it so that the waypoint fails, and the run ends without error.

constexpr std::string_view avoidedCaseProgram = R"(extern int __VERIFIER_nondet_int(void);
extern void reach_error(void);
int main(void) {
  int x = __VERIFIER_nondet_int();
  switch (x) {
  case 0: reach_error(); break;
  default: break;
  }
  return 0;
}
)";

constexpr std::string_view narrowingProgram = R"(extern int __VERIFIER_nondet_int(void);
extern void reach_error(void);
int main(void) {
  int x = __VERIFIER_nondet_int();
  unsigned char c = x;
  if (c == 0) reach_error();
  return 0;
}
)";

TEST_F(EndToEndTest, AnAvoidWaypointOnTheDefaultOfAnInputIsEvaluatedOnIt) {
    // The error needs x == 0, which the default 0 gives and the witness avoids.
    build(std::string(avoidProgram));
    for (const auto& [line, column] : {std::pair{5u, 3u}, std::pair{6u, 5u}}) {
        const ValidationResult result =
            validate(sequence({{avoid(WaypointType::Assumption, line, column, "x == 0"), target(6, 5)}}));
        EXPECT_EQ(result.verdict, Verdict::TargetWithoutError) << result.message << "\n" << log();
        EXPECT_EQ(result.runs, 2u);
    }
}

TEST_F(EndToEndTest, AnAvoidedCaseOfTheDefaultOfAnInputIsEvaluatedOnIt) {
    build(std::string(avoidedCaseProgram));
    const ValidationResult result = validate(sequence({{avoid(WaypointType::Branching, 5, 3, "0"), target(6, 11)}}));
    EXPECT_EQ(result.verdict, Verdict::TargetWithoutError) << result.message << "\n" << log();
    EXPECT_EQ(result.runs, 2u);
}

TEST_F(EndToEndTest, AnAvoidWaypointChangesAnInputThroughANarrowingConversion) {
    build(std::string(narrowingProgram));
    const ValidationResult result =
        validate(sequence({{avoid(WaypointType::Assumption, 6, 3, "c == 0"), target(6, 15)}}));
    EXPECT_EQ(result.verdict, Verdict::TargetWithoutError) << result.message << "\n" << log();
    EXPECT_EQ(result.runs, 2u);
}

TEST_F(EndToEndTest, CascadedWaypointsReferToTheSameExecutionOfTheStatement) {
    build(R"(extern void reach_error(void);
int main(void) {
  int b = 1;
  for (int i = 0; i < 2; i++) {
    i = i;
  }
  reach_error();
  return 0;
}
)");
    // The avoid waypoint of the target segment holds where the segment becomes current.
    ValidationResult result = validate(sequence({{follow(WaypointType::Assumption, 7, 3, "1")},
                                                 {avoid(WaypointType::Assumption, 7, 3, "b == 1"), target(7, 3)}}));
    EXPECT_EQ(result.verdict, Verdict::LeftWitness) << result.message << "\n" << log();
    // i == 1 holds only at the second execution of line 5.
    result = validate(sequence({{follow(WaypointType::Assumption, 5, 5, "1")},
                                {follow(WaypointType::Assumption, 5, 5, "i == 0")},
                                {follow(WaypointType::Assumption, 5, 5, "i == 1")},
                                {target(7, 3)}}));
    EXPECT_EQ(result.verdict, Verdict::LeftWitness) << result.message << "\n" << log();
    // Identical assumptions, as CPAchecker writes them, pass together.
    result = validate(sequence({{follow(WaypointType::Assumption, 5, 5, "1")},
                                {follow(WaypointType::Assumption, 5, 5, "1")},
                                {follow(WaypointType::Assumption, 5, 5, "i == 0")},
                                {target(7, 3)}}));
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message << "\n" << log();
}

TEST_F(EndToEndTest, ADoLoopIsBranchedAtItsDoKeyword) {
    build(R"(void reach_error(void);
int main(void) {
  int i = 0;
  do {
    i++;
  } while (i < 2);
  reach_error();
  return 0;
}
)");
    // The condition holds after the first iteration: an avoid waypoint at `do` or at `while` passes.
    for (const auto& [line, column] : {std::pair{4u, 3u}, std::pair{6u, 5u}}) {
        const ValidationResult result =
            validate(sequence({{avoid(WaypointType::Branching, line, column, "true"), target(7, 3)}}));
        EXPECT_EQ(result.verdict, Verdict::LeftWitness) << result.message << "\n" << log();
    }
}

TEST_F(EndToEndTest, BlocksWithoutTheirOwnEventDoNotFitAWaypoint) {
    build(R"(extern int __VERIFIER_nondet_int(void);
void reach_error(void);
int main(void) {
  int x = __VERIFIER_nondet_int();
  if (x == 0) {
  } else {
    goto L;
  }
  {
  L: x++;
  }
  switch (x) {
  case 1: return 0;
  default: reach_error();
  }
  return 0;
}
)");
    // An empty block, a block entered by goto, the body of a switch.
    for (const auto& [line, column] : {std::pair{5u, 15u}, std::pair{9u, 3u}, std::pair{12u, 14u}}) {
        const ValidationResult result =
            validate(sequence({{avoid(WaypointType::Assumption, line, column, "1"), target(14, 12)}}));
        EXPECT_EQ(result.verdict, Verdict::Unsupported) << line << ": " << result.message;
        EXPECT_TRUE(result.message.ends_with("is at no evaluation point of its type")) << result.message;
    }
}

TEST_F(EndToEndTest, AHugeColumnDoesNotWrapAroundToAnEarlierLine) {
    // Line 6 plus column 4294967283 would be the if at 5:3 in 32-bit offsets.
    build(std::string(avoidProgram));
    const ValidationResult result =
        validate(sequence({{follow(WaypointType::Branching, 6, 4294967283u, "true")}, {target(6, 5)}}));
    EXPECT_EQ(result.verdict, Verdict::Unsupported) << result.message << "\n" << log();
    EXPECT_EQ(result.message, "the witness does not fit the program: the follow waypoint of segment 1 (branching at "
                              "6:4294967283) is at no evaluation point of its type");
    EXPECT_EQ(result.runs, 0u);
}

} // namespace
} // namespace nitwit::validate::test
