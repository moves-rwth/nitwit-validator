// validate() with the interpreter: the events of real runs at the locations producers write.

#include <string>

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

} // namespace
} // namespace nitwit::validate::test
