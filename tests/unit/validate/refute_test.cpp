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
    EXPECT_THAT(result.message, StartsWith("no run of the program calls 'reach_error': "));
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
