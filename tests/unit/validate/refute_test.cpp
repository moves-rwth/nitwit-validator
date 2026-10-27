// The refutation of correctness witnesses (refute.hpp; validate/README.md, "Correctness witnesses"), with the
// interpreter: runs that follow no witness, with defaults, the guard search and the retry with inputs 1.

#include "validate/refute.hpp"

#include <string>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "interp/interpreter.hpp"
#include "validate_test_support.hpp"

namespace nitwit::validate::test {
namespace {

using testing::StartsWith;

class RefuteTest : public ValidateTest {
protected:
    void build(std::string text) {
        program(std::move(text));
        auto built = interp::buildProgram(unit(), context());
        ASSERT_TRUE(built.has_value()) << built.error().message;
        input_->program = *built;
    }

    ValidationResult refute(ConfirmingRun* run = nullptr) {
        ValidationOptions options = options_;
        options.retryDefaultInput = 1;
        options.log = &log_;
        options.confirmingRun = run;
        options.run.checkTerms = &interp::termMismatch;
        return nitwit::validate::refute(*input_, options, diagnostics_);
    }
};

TEST_F(RefuteTest, ARunWithTheDefaultsThatCallsTheErrorFunctionRefutes) {
    build(R"(extern int __VERIFIER_nondet_int(void);
void reach_error(void);
int main(void) {
    int x = __VERIFIER_nondet_int();
    if (x == 0) reach_error();
    return 0;
}
)");
    const ValidationResult result = refute();
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message << "\n" << log();
    EXPECT_EQ(result.message, "a run of the program calls 'reach_error' at 5:17, so the program violates the property "
                              "and no correctness witness of it is valid");
    EXPECT_EQ(result.runs, 1u);
}

TEST_F(RefuteTest, TheGuardsOfTheProgramAreSearchedFor) {
    build(R"(extern int __VERIFIER_nondet_int(void);
void abort(void);
void reach_error(void);
int main(void) {
    int x = __VERIFIER_nondet_int();
    if (x != 5) abort();
    reach_error();
    return 0;
}
)");
    const ValidationResult result = refute();
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message << "\n" << log();
    EXPECT_EQ(result.runs, 2u);
}

TEST_F(RefuteTest, TheRetryGivesInputsOne) {
    build(R"(extern int __VERIFIER_nondet_int(void);
void reach_error(void);
int main(void) {
    if (__VERIFIER_nondet_int()) reach_error();
    return 0;
}
)");
    const ValidationResult result = refute();
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message << "\n" << log();
    EXPECT_EQ(result.runs, 2u);
}

TEST_F(RefuteTest, ACorrectProgramIsNotRefuted) {
    build(R"(extern int __VERIFIER_nondet_int(void);
void reach_error(void);
int main(void) {
    int x = __VERIFIER_nondet_int();
    if (x != x) reach_error();
    return 0;
}
)");
    const ValidationResult result = refute();
    EXPECT_NE(result.verdict, Verdict::Confirmed);
    EXPECT_EQ(result.message, "no run that NITWIT made called 'reach_error': main returned 0 without calling "
                              "'reach_error'");
}

TEST_F(RefuteTest, ACallAfterUndefinedBehaviourRefutesNothing) {
    // C leaves x + 1 open; NITWIT computes INT_MIN, as the processor does, and calls the error function.
    build(R"(void reach_error(void);
int main(void) {
    int x = 2147483647;
    if (x + 1 < x) reach_error();
    return 0;
}
)");
    const ValidationResult result = refute();
    EXPECT_EQ(result.verdict, Verdict::Unknown) << result.message << "\n" << log();
    EXPECT_EQ(result.message, "a run of the program calls 'reach_error' at 4:20, but after undefined behaviour (signed "
                              "integer overflow at 4:9), whose result C leaves open, so the run refutes nothing");
}

TEST_F(RefuteTest, RunningOutOfMemoryIsKept) {
    options_.run.memoryLimit = 1024;
    build(R"(void *malloc(unsigned long size);
void reach_error(void);
int main(void) {
    char *p = malloc(4096);
    p[0] = 1;
    reach_error();
    return 0;
}
)");
    const ValidationResult result = refute();
    EXPECT_EQ(result.verdict, Verdict::OutOfMemory) << result.message << "\n" << log();
    EXPECT_THAT(result.message, StartsWith("no run that NITWIT made called 'reach_error': out of memory"));
}

TEST_F(RefuteTest, WithStrictInputsAnInputThatNoHintGivesEndsTheRun) {
    // A correctness witness gives no input, so only a program without inputs can be refuted.
    options_.strictInputs = true;
    build(R"(extern int __VERIFIER_nondet_int(void);
void reach_error(void);
int main(void) {
    int x = __VERIFIER_nondet_int();
    if (x == 0) reach_error();
    return 0;
}
)");
    const ValidationResult result = refute();
    EXPECT_EQ(result.verdict, Verdict::Unsupported) << result.message << "\n" << log();
    EXPECT_THAT(result.message, StartsWith("no run that NITWIT made called 'reach_error': the witness does not give "
                                           "the value of input 0"));
}

TEST_F(RefuteTest, TheRunThatCallsTheErrorFunctionIsKept) {
    build(R"(extern int __VERIFIER_nondet_int(void);
void reach_error(void);
int main(void) {
    int x = __VERIFIER_nondet_int();
    if (x == 0) reach_error();
    return 0;
}
)");
    ConfirmingRun run;
    ASSERT_EQ(refute(&run).verdict, Verdict::Confirmed);
    ASSERT_EQ(run.inputs.size(), 1u);
    EXPECT_EQ(run.errorCall, callAt(5, 29));
}

} // namespace
} // namespace nitwit::validate::test
