// Verdicts and exit codes (verdict.hpp; validate/README.md, "Verdict"; docs/exit-codes.md).

#include "validate/verdict.hpp"

#include <string>

#include <gtest/gtest.h>

#include "test_programs.hpp"

namespace nitwit::validate::test {
namespace {

TEST(ExitCodeTest, ExitCodesOfNitwit3) {
    const auto code = [](Verdict verdict) {
        return exitCode({.verdict = verdict, .message = {}, .runs = 1, .segmentsReached = 0});
    };
    EXPECT_EQ(code(Verdict::Confirmed), 0);
    EXPECT_EQ(code(Verdict::LeftWitness), 241);
    EXPECT_EQ(code(Verdict::ProgramFinished), 242);
    // 252 is reserved until BenchExec maps it to UNKNOWN (docs/exit-codes.md).
    EXPECT_EQ(code(Verdict::ErrorBeforeTarget), 241);
    EXPECT_EQ(code(Verdict::TargetWithoutError), 250);
    EXPECT_EQ(code(Verdict::Unsupported), 4);
    EXPECT_EQ(code(Verdict::Unknown), 4);
    EXPECT_EQ(code(Verdict::OutOfMemory), 251);
}

class VerdictTest : public ValidateTest {
protected:
    void SetUp() override { program(std::string(inputsProgram)); }

    /// The result of a run that ends with `end` after the first two statements, in segment 1 of 2 or, if
    /// `inTarget`, in the target segment.
    ValidationResult endingWith(interp::RunResult end, bool inTarget = false) {
        const witness::YamlWitness witness =
            inTarget ? sequence({{follow(WaypointType::Assumption, 5, 5, "1")}, {target(7, 9)}})
                     : sequence({{follow(WaypointType::Assumption, 9, 5, "1")}, {target(7, 9)}});
        return validate(witness, [&](FakeExecution& x) {
            x.statement(4, 5);
            x.statement(5, 5);
            return end;
        });
    }
};

TEST_F(VerdictTest, ExitEndsTheProgram) {
    interp::RunResult end = endedBy(interp::Termination::Exit);
    end.exitStatus = 3;
    const ValidationResult result = endingWith(end);
    EXPECT_EQ(result.verdict, Verdict::ProgramFinished);
    EXPECT_EQ(result.message, "the program called exit(3) in segment 1 of 2 without calling 'reach_error'");
}

TEST_F(VerdictTest, ExitInTheTargetSegment) {
    const ValidationResult result = endingWith(endedBy(interp::Termination::Exit), true);
    EXPECT_EQ(result.verdict, Verdict::TargetWithoutError);
    EXPECT_EQ(result.segmentsReached, 1u);
}

TEST_F(VerdictTest, AbortIsUnknownAlsoInTheTargetSegment) {
    const ValidationResult result = endingWith(endedBy(interp::Termination::Abort));
    EXPECT_EQ(result.verdict, Verdict::Unknown);
    EXPECT_EQ(result.message, "the program aborted in segment 1 of 2");
    EXPECT_EQ(endingWith(endedBy(interp::Termination::Abort), true).verdict, Verdict::Unknown);
}

TEST_F(VerdictTest, InfeasibleRunsAreUnknown) {
    const ValidationResult result = endingWith(endedBy(interp::Termination::AssumeFailed));
    EXPECT_EQ(result.verdict, Verdict::Unknown);
    EXPECT_EQ(result.message, "__VERIFIER_assume failed, the execution is infeasible in segment 1 of 2");
}

TEST_F(VerdictTest, UndefinedBehaviourIsUnknown) {
    interp::RunResult end = endedBy(interp::Termination::UndefinedBehavior, "division by zero");
    end.location = *sources_.location(file_, {5, 5});
    const ValidationResult result = endingWith(end);
    EXPECT_EQ(result.verdict, Verdict::Unknown);
    EXPECT_EQ(result.message, "undefined behaviour at test.c:5:5: division by zero in segment 1 of 2");
}

TEST_F(VerdictTest, LimitsAreUnknown) {
    const ValidationResult result = endingWith(endedBy(interp::Termination::LimitReached, "more than 10 steps"));
    EXPECT_EQ(result.verdict, Verdict::Unknown);
    EXPECT_EQ(exitCode(result), 4);
    EXPECT_EQ(result.message, "limit reached: more than 10 steps in segment 1 of 2");
}

TEST_F(VerdictTest, UnsupportedProgramsAreUnsupported) {
    const ValidationResult result = endingWith(endedBy(interp::Termination::Unsupported, "pthread_create"));
    EXPECT_EQ(result.verdict, Verdict::Unsupported);
    EXPECT_EQ(exitCode(result), 4);
}

TEST_F(VerdictTest, OutOfMemory) {
    const ValidationResult result = endingWith(endedBy(interp::Termination::OutOfMemory, "4 GiB"));
    EXPECT_EQ(result.verdict, Verdict::OutOfMemory);
    EXPECT_EQ(exitCode(result), 251);
}

} // namespace
} // namespace nitwit::validate::test
