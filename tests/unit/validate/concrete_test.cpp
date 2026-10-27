// Concrete witnesses (concrete.hpp; validate/README.md, "Concrete witnesses") and strict inputs ("Strict inputs"), with
// the interpreter.

#include "validate/concrete.hpp"

#include <cmath>
#include <expected>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "interp/interpreter.hpp"
#include "validate/monitor.hpp"
#include "validate/values.hpp"
#include "validate_test_support.hpp"
#include "witness/parse.hpp"

namespace nitwit::validate::test {
namespace {

using testing::ElementsAre;
using testing::HasSubstr;

class ConcreteTest : public ValidateTest {
protected:
    /// Analyzes and lowers the program; fails the test if that fails.
    void build(std::string text, DataModel model = DataModel::ILP32) {
        program(std::move(text), model);
        auto built = interp::buildProgram(unit(), context());
        ASSERT_TRUE(built.has_value()) << built.error().message;
        input_->program = *built;
    }

    ValidationResult validate(const witness::YamlWitness& witness, ConfirmingRun* run = nullptr) {
        ValidationOptions options = options_;
        options.log = &log_;
        options.confirmingRun = run;
        options.run.checkTerms = &interp::termMismatch;
        return nitwit::validate::validate(witness, *input_, options, diagnostics_);
    }

    /// Validates `witness`, which must be confirmed, and writes the concrete witness of the run.
    std::expected<ConcreteWitness, std::string>
    concrete(const witness::YamlWitness& witness, std::size_t maxInputs = ConcreteTask{}.maxInputs, bool check = true) {
        ConfirmingRun run;
        const ValidationResult result = validate(witness, &run);
        EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message << "\n" << log();
        const ConcreteTask task{.programPath = "dir/test.c",
                                .specification = "CHECK( init(main()), LTL(G ! call(reach_error())) )",
                                .producerVersion = "4.2.0",
                                .creationTime = "2026-10-27T12:00:00Z",
                                .maxInputs = maxInputs,
                                .check = check};
        return writeConcreteWitness(run, *input_, task, options_);
    }

    /// The waypoints of a written witness as "type line:column constraint".
    static std::vector<std::string> waypointsOf(const std::string& text) {
        const auto witness = witness::parseYamlWitness(text);
        EXPECT_TRUE(witness.has_value()) << text;
        std::vector<std::string> waypoints;
        if (!witness) {
            return waypoints;
        }
        for (const witness::Segment& segment : witness->violationSequences.at(0).segments) {
            for (const witness::Waypoint& point : segment.waypoints) {
                waypoints.push_back(std::format("{} {}:{}{}", nameOf(point.type), point.location.line,
                                                point.location.column.value_or(0),
                                                point.constraint ? " " + point.constraint->value : ""));
            }
        }
        return waypoints;
    }
};

TEST_F(ConcreteTest, EveryInputIsGivenAtTheParenthesisOfItsCallInRequestOrder) {
    build(R"(extern int __VERIFIER_nondet_int(void);
extern char *__VERIFIER_nondet_pointer(void);
extern unsigned char external(int);
void reach_error(void);
int main(void) {
    int a = __VERIFIER_nondet_int(); int b = __VERIFIER_nondet_int();
    for (int i = 0; i < 2; i++) a += __VERIFIER_nondet_int();
    char *p = __VERIFIER_nondet_pointer();
    if (a == 3 && b < 0 && !p && external(a) == 0) reach_error();
    return 0;
}
)");
    const auto written = concrete(sequence({{follow(WaypointType::FunctionReturn, 6, 35, "\\result == 3")},
                                            {follow(WaypointType::FunctionReturn, 6, 68, "\\result == -1")},
                                            {target(9, 52)}}));
    ASSERT_TRUE(written.has_value()) << written.error();
    EXPECT_EQ(written->inputs, 6u);
    EXPECT_THAT(waypointsOf(written->text),
                ElementsAre("function_return 6:35 \\result == 3", "function_return 6:68 \\result == -1",
                            "function_return 7:60 \\result == 0", "function_return 7:60 \\result == 0",
                            "function_return 8:41 \\result == 0", "function_return 9:44 \\result == 0U",
                            "target 9:52"));
    EXPECT_THAT(written->text, HasSubstr("location: {file_name: 'dir/test.c', line: 6, column: 35, function: 'main'}"));
    EXPECT_THAT(written->text, HasSubstr("constraint: {value: '\\result == 3', format: 'ext_c_expression'}"));
    EXPECT_TRUE(written->undefinedBehavior.empty());
}

TEST_F(ConcreteTest, TheMetadataNameTheProgramWithItsHash) {
    build(R"(void reach_error(void);
int main(void) { reach_error(); return 0; }
)");
    const auto written = concrete(sequence({{target(2, 18)}}));
    ASSERT_TRUE(written.has_value()) << written.error();
    const auto witness = witness::parseYamlWitness(written->text);
    ASSERT_TRUE(witness.has_value());
    const witness::Metadata& metadata = witness->violationSequences.at(0).metadata;
    EXPECT_EQ(metadata.formatVersion, "2.2");
    EXPECT_EQ(metadata.producer.name, "NITWIT");
    EXPECT_EQ(metadata.producer.version, "4.2.0");
    EXPECT_EQ(metadata.task.inputFiles, std::vector<std::string>{"dir/test.c"});
    EXPECT_EQ(metadata.task.dataModel, "ILP32");
    // printf 'void reach_error(void);\nint main(void) { reach_error(); return 0; }\n' | sha256sum
    EXPECT_THAT(written->text,
                HasSubstr("'dir/test.c': '78b27985456ce2f1bf150595f89a50f9e46fa7af469e2685008b9d32373a8241'"));
    EXPECT_THAT(written->text, HasSubstr("creation_time: '2026-10-27T12:00:00Z'"));
}

TEST_F(ConcreteTest, TheSameValidationWritesTheSameWitness) {
    build(R"(extern int __VERIFIER_nondet_int(void);
void reach_error(void);
int main(void) { if (__VERIFIER_nondet_int() == 2) reach_error(); return 0; }
)");
    const witness::YamlWitness original =
        sequence({{follow(WaypointType::FunctionReturn, 3, 44, "\\result == 2")}, {target(3, 52)}});
    const auto first = concrete(original);
    const auto second = concrete(original);
    ASSERT_TRUE(first.has_value() && second.has_value());
    EXPECT_EQ(first->text, second->text);
    EXPECT_THAT(first->text, testing::ContainsRegex("uuid: '[0-9a-f]{8}-[0-9a-f]{4}-8[0-9a-f]{3}-[89ab][0-9a-f]{3}-"
                                                    "[0-9a-f]{12}'"));
}

TEST_F(ConcreteTest, AWitnessWrittenWithoutTheCheckIsTheSame) {
    build(R"(extern int __VERIFIER_nondet_int(void);
void reach_error(void);
int main(void) { if (__VERIFIER_nondet_int() == 2) reach_error(); return 0; }
)");
    const witness::YamlWitness original =
        sequence({{follow(WaypointType::FunctionReturn, 3, 44, "\\result == 2")}, {target(3, 52)}});
    const auto checked = concrete(original);
    const auto unchecked = concrete(original, ConcreteTask{}.maxInputs, false);
    ASSERT_TRUE(checked.has_value() && unchecked.has_value());
    EXPECT_EQ(checked->text, unchecked->text);
}

TEST_F(ConcreteTest, SpecialFloatingValuesAreGivenExactly) {
    build(R"(extern float __VERIFIER_nondet_float(void);
extern double __VERIFIER_nondet_double(void);
extern long double __VERIFIER_nondet_long_double(void);
void reach_error(void);
int main(void) {
    double a = __VERIFIER_nondet_double(); float b = __VERIFIER_nondet_float();
    long double c = __VERIFIER_nondet_long_double(); double z = __VERIFIER_nondet_double();
    float d = __VERIFIER_nondet_float();
    if (a == 1e-320 && b > 3.4e38f && c < -1.1e4932L && 1 / z < 0 && d == 0.1f) reach_error();
    return 0;
}
)",
          DataModel::LP64);
    const auto written =
        concrete(sequence({{follow(WaypointType::FunctionReturn, 6, 41, "\\result == 1e-320")},
                           {follow(WaypointType::FunctionReturn, 6, 78, "\\result > 0x1.fffffep+127f")},
                           {follow(WaypointType::FunctionReturn, 7, 51, "\\result < -0x1.fffffffffffffffep+16383L")},
                           {follow(WaypointType::FunctionReturn, 7, 90, "\\result == -0.0")},
                           {follow(WaypointType::FunctionReturn, 8, 39, "\\result == 0.1f")},
                           {target(9, 81)}}));
    ASSERT_TRUE(written.has_value()) << written.error();
    const std::vector<std::string> waypoints = waypointsOf(written->text);
    ASSERT_EQ(waypoints.size(), 6u);
    EXPECT_EQ(waypoints[0], "function_return 6:41 \\result == 0x1.fap-1064"); // subnormal: 2024 * 2^-1074
    EXPECT_EQ(waypoints[1], "function_return 6:78 \\result > 0x1.fffffep+127f");
    EXPECT_THAT(waypoints[2], testing::StartsWith("function_return 7:51 \\result < -0x"));
    EXPECT_EQ(waypoints[3], "function_return 7:90 \\result == -0x0p+0");
    EXPECT_EQ(waypoints[4], "function_return 8:39 \\result == 0x1.99999ap-4f");
}

TEST_F(ConcreteTest, AReadOfUninitializedMemoryGivesNoWitness) {
    build(R"(void reach_error(void);
int main(void) {
    int x;
    if (x == 4) reach_error();
    return 0;
}
)");
    const auto written = concrete(sequence({{follow(WaypointType::Assumption, 4, 5, "x == 4")}, {target(4, 17)}}));
    ASSERT_FALSE(written.has_value());
    EXPECT_THAT(written.error(), HasSubstr("reads uninitialized memory, whose value a witness cannot give"));
}

TEST_F(ConcreteTest, TheBytesOfNondetMemoryGiveNoWitness) {
    build(R"(extern void __VERIFIER_nondet_memory(void *, unsigned);
void reach_error(void);
int main(void) {
    char c[2];
    __VERIFIER_nondet_memory(c, 2);
    reach_error();
    return 0;
}
)");
    const auto written = concrete(sequence({{target(6, 5)}}));
    ASSERT_FALSE(written.has_value());
    EXPECT_THAT(written.error(), HasSubstr("the call of __VERIFIER_nondet_memory at 5:34, occurrence 0) is not the "
                                           "result of the call"));
}

TEST_F(ConcreteTest, MoreInputsThanTheLimitGiveNoWitness) {
    build(R"(extern int __VERIFIER_nondet_int(void);
void reach_error(void);
int main(void) {
    for (int i = 0; i < 3; i++) __VERIFIER_nondet_int();
    reach_error();
    return 0;
}
)");
    const auto written = concrete(sequence({{target(5, 5)}}), 2);
    ASSERT_FALSE(written.has_value());
    EXPECT_EQ(written.error(), "the run took 3 inputs, more than the 2 that a concrete witness gives");
}

TEST_F(ConcreteTest, UndefinedBehaviourOfTheRunIsReported) {
    build(R"(extern int __VERIFIER_nondet_int(void);
void reach_error(void);
int main(void) {
    int x = __VERIFIER_nondet_int();
    int y = x + 2147483647;
    if (y < 0) reach_error();
    return 0;
}
)");
    const auto written =
        concrete(sequence({{follow(WaypointType::FunctionReturn, 4, 35, "\\result == 1")}, {target(6, 16)}}));
    ASSERT_TRUE(written.has_value()) << written.error();
    EXPECT_EQ(written->undefinedBehavior, "signed integer overflow at 5:13");
}

TEST_F(ConcreteTest, ResultConstraintsGiveExactValues) {
    program("int main(void) { return 0; }\n", DataModel::LP64);
    ast::AstContext& c = context();
    const auto constraint = [&](Int128 number, ast::BuiltinKind kind) {
        const ast::QualType type = c.builtin(kind);
        return resultConstraint(c, valueOf(c, number, type), type).value_or("none");
    };
    EXPECT_EQ(constraint(-5, ast::BuiltinKind::Int), "\\result == -5");
    EXPECT_EQ(constraint(4294967295, ast::BuiltinKind::UnsignedInt), "\\result == 4294967295U");
    EXPECT_EQ(constraint(-1, ast::BuiltinKind::UnsignedLongLong), "\\result == 18446744073709551615U");
    EXPECT_EQ(constraint(std::numeric_limits<std::int64_t>::min(), ast::BuiltinKind::LongLong),
              "\\result == -9223372036854775807 - 1");
    EXPECT_EQ(constraint(-128, ast::BuiltinKind::Char), "\\result == -128");
    EXPECT_EQ(constraint(1, ast::BuiltinKind::Bool), "\\result == 1U");
    EXPECT_EQ(constraint(Int128{1} << 70, ast::BuiltinKind::Int128), "none");
    const ast::QualType pointer = c.pointerTo(c.builtin(ast::BuiltinKind::Char));
    EXPECT_EQ(resultConstraint(c, value::Value::integer(0, {.bits = 64, .isSigned = false}), pointer), "\\result == 0");
    EXPECT_EQ(resultConstraint(c, value::Value::integer(16, {.bits = 64, .isSigned = false}), pointer), std::nullopt);
    const ast::QualType doubleType = c.builtin(ast::BuiltinKind::Double);
    EXPECT_EQ(resultConstraint(c, value::Value::ofDouble(1.5), doubleType), "\\result == 0x1.8p+0");
    EXPECT_EQ(resultConstraint(c, value::Value::ofDouble(std::numeric_limits<double>::denorm_min()), doubleType),
              "\\result == 0x1p-1074");
    EXPECT_EQ(resultConstraint(c, value::Value::ofDouble(-0.0), doubleType), "\\result == -0x0p+0");
    EXPECT_EQ(resultConstraint(c, value::Value::ofDouble(-std::numeric_limits<double>::infinity()), doubleType),
              "\\result < -0x1.fffffffffffffp+1023");
    EXPECT_EQ(resultConstraint(c, value::Value::ofDouble(std::nan("")), doubleType), "\\result != \\result");
}

// validate/README.md, "Strict inputs".

TEST_F(ConcreteTest, WithStrictInputsAnInputWithoutWaypointIsUnsupported) {
    build(R"(extern int __VERIFIER_nondet_int(void);
void reach_error(void);
int main(void) {
    int a = __VERIFIER_nondet_int();
    int b = __VERIFIER_nondet_int();
    reach_error();
    return 0;
}
)");
    options_.strictInputs = true;
    const ValidationResult result =
        validate(sequence({{follow(WaypointType::FunctionReturn, 4, 35, "\\result == 1")}, {target(6, 5)}}));
    EXPECT_EQ(result.verdict, Verdict::Unsupported);
    EXPECT_EQ(result.message, "the witness does not give the value of input 1 (the call of __VERIFIER_nondet_int at "
                              "5:35, occurrence 0)");
}

TEST_F(ConcreteTest, WithStrictInputsNothingIsSearched) {
    build(R"(extern int __VERIFIER_nondet_int(void);
void reach_error(void);
int main(void) {
    int a = __VERIFIER_nondet_int();
    if (a == 3) reach_error();
    return 0;
}
)");
    options_.strictInputs = true;
    const ValidationResult result = validate(sequence({{follow(WaypointType::FunctionReturn, 4, 35, "\\result > 1")},
                                                       {follow(WaypointType::Branching, 5, 5, "true")},
                                                       {target(5, 17)}}));
    EXPECT_EQ(result.verdict, Verdict::LeftWitness) << result.message;
    EXPECT_EQ(result.runs, 1u);
}

TEST_F(ConcreteTest, WithStrictInputsAvoidWaypointsGiveNoInput) {
    build(R"(extern int __VERIFIER_nondet_int(void);
void reach_error(void);
int main(void) {
    int a = __VERIFIER_nondet_int();
    reach_error();
    return 0;
}
)");
    options_.strictInputs = true;
    const ValidationResult result =
        validate(sequence({{avoid(WaypointType::FunctionReturn, 4, 35, "\\result == 0"), target(5, 5)}}));
    EXPECT_EQ(result.verdict, Verdict::Unsupported) << result.message;
}

TEST_F(ConcreteTest, WithStrictInputsAReadJustBeforeTheEndIsUnsupported) {
    build(R"(void reach_error(void);
int main(void) {
    int x;
    return x;
}
)");
    options_.strictInputs = true;
    const ValidationResult result = validate(sequence({{target(4, 5)}}));
    EXPECT_EQ(result.verdict, Verdict::Unsupported) << result.message;
    EXPECT_THAT(result.message, HasSubstr("does not give the value of input 0 (the read at 4:12"));
}

TEST_F(ConcreteTest, ANanInputIsGivenAsUnequalToItself) {
    // The search finds NaN (end_to_end_test.cpp, AComparisonOfAnInputWithItselfFailsWithNan); the check confirms the
    // witness only if `\result != \result` hints NaN.
    build(R"(extern double __VERIFIER_nondet_double(void);
void reach_error(void);
int main(void) {
    double x = __VERIFIER_nondet_double();
    if (x == x) return 0;
    reach_error();
    return 0;
}
)");
    const auto written = concrete(sequence({{follow(WaypointType::Branching, 5, 5, "false")}, {target(6, 5)}}));
    ASSERT_TRUE(written.has_value()) << written.error();
    EXPECT_THAT(waypointsOf(written->text), ElementsAre("function_return 4:41 \\result != \\result", "target 6:5"));
}

TEST_F(ConcreteTest, ResultUnequalToItselfHintsANan) {
    build(R"(extern double __VERIFIER_nondet_double(void);
void reach_error(void);
int main(void) {
    double x = __VERIFIER_nondet_double();
    if (x == x) return 0;
    reach_error();
    return 0;
}
)");
    options_.strictInputs = true;
    const ValidationResult result =
        validate(sequence({{follow(WaypointType::FunctionReturn, 4, 41, "\\result != \\result")}, {target(6, 5)}}));
    EXPECT_EQ(result.verdict, Verdict::Confirmed) << result.message << "\n" << log();
    EXPECT_EQ(result.runs, 1u);
}

TEST_F(ConcreteTest, TheConfirmingRunIsRecorded) {
    build(R"(extern int __VERIFIER_nondet_int(void);
void reach_error(void);
int main(void) {
    if (__VERIFIER_nondet_int() == 2) reach_error();
    return 0;
}
)");
    ConfirmingRun run;
    const ValidationResult result =
        validate(sequence({{follow(WaypointType::Branching, 4, 5, "true")}, {target(4, 39)}}), &run);
    ASSERT_EQ(result.verdict, Verdict::Confirmed) << result.message;
    EXPECT_EQ(result.runs, 2u);
    ASSERT_EQ(run.inputs.size(), 1u);
    EXPECT_EQ(run.requests, 1u);
    EXPECT_EQ(run.inputs[0].value.signedBits(), 2);
    EXPECT_EQ(run.inputs[0].source, InputSource::Fixed);
    ASSERT_NE(run.errorCall, nullptr);
    EXPECT_EQ(run.errorCall, callAt(4, 51));
}

} // namespace
} // namespace nitwit::validate::test
