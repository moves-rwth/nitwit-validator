// The witness features NITWIT rejects before running (support_check.hpp; validate/README.md, "Structure").

#include "validate/support_check.hpp"

#include <gtest/gtest.h>

#include "validate_test_support.hpp"

namespace nitwit::validate::test {
namespace {

witness::YamlWitness wellFormed() {
    return sequence({{avoid(WaypointType::Assumption, 3, 5, "x == 1"), follow(WaypointType::Branching, 4, 5, "true")},
                     {target(5, 9)}});
}

TEST(SupportCheckTest, AcceptsAWellFormedSequence) {
    EXPECT_EQ(unsupportedFeature(wellFormed()), std::nullopt);
}

TEST(SupportCheckTest, RejectsWitnessesWithoutViolationSequence) {
    EXPECT_EQ(unsupportedFeature(witness::YamlWitness{}), "witnesses without a violation_sequence entry");
}

TEST(SupportCheckTest, RejectsMoreThanOneViolationSequence) {
    witness::YamlWitness witness = wellFormed();
    witness.violationSequences.push_back(witness.violationSequences.front());
    EXPECT_EQ(unsupportedFeature(witness), "witnesses with more than one violation_sequence entry");
}

TEST(SupportCheckTest, RejectsAnEmptySequenceAndEmptySegments) {
    EXPECT_EQ(unsupportedFeature(sequence({})), "a violation sequence without segments");
    EXPECT_EQ(unsupportedFeature(sequence({{}, {target(5, 9)}})), "the empty segment 1");
}

TEST(SupportCheckTest, RejectsCycleWaypoints) {
    witness::YamlWitness witness = wellFormed();
    witness.violationSequences[0].segments[0].waypoints[0].action = WaypointAction::Cycle;
    EXPECT_EQ(unsupportedFeature(witness), "cycle waypoints (non-termination) in segment 1");
}

TEST(SupportCheckTest, AcceptsThreadZeroOnly) {
    witness::YamlWitness witness = wellFormed();
    witness.violationSequences[0].segments[1].waypoints[0].threadId = 0;
    EXPECT_EQ(unsupportedFeature(witness), std::nullopt);
    witness.violationSequences[0].segments[1].waypoints[0].threadId = 1;
    EXPECT_EQ(unsupportedFeature(witness), "waypoints of threads other than thread 0 in segment 2");
}

TEST(SupportCheckTest, RejectsSegmentsWithoutExactlyOneFollowWaypointAtTheEnd) {
    const witness::Waypoint condition = follow(WaypointType::Branching, 4, 5, "true");
    const witness::Waypoint assumption = avoid(WaypointType::Assumption, 3, 5, "x == 1");
    const std::string message = "segment 1, which does not end with its only follow waypoint";
    EXPECT_EQ(unsupportedFeature(sequence({{assumption}, {target(5, 9)}})), message);
    EXPECT_EQ(unsupportedFeature(sequence({{condition, assumption}, {target(5, 9)}})), message);
    EXPECT_EQ(unsupportedFeature(sequence({{condition, condition}, {target(5, 9)}})), message);
}

TEST(SupportCheckTest, RejectsATargetOutsideTheLastSegment) {
    EXPECT_EQ(unsupportedFeature(sequence({{target(5, 9)}, {target(5, 9)}})),
              "a target waypoint in segment 1, which is not the last segment");
    EXPECT_EQ(unsupportedFeature(sequence({{follow(WaypointType::Branching, 4, 5, "true")}})),
              "a last segment that does not end with the target waypoint");
}

TEST(SupportCheckTest, RejectsTargetsWithActionAvoid) {
    EXPECT_EQ(
        unsupportedFeature(sequence({{waypoint(WaypointType::Target, WaypointAction::Avoid, 5, 9), target(5, 9)}})),
        "a target waypoint with action avoid in segment 1");
}

TEST(SupportCheckTest, AcceptsTheConstraintFormatsWhoseTextIsC) {
    for (const std::string format : {"", "c_expression", "acsl_expression", "ext_c_expression"}) {
        witness::YamlWitness witness = wellFormed();
        witness.violationSequences[0].segments[0].waypoints[0].constraint->format = format;
        EXPECT_EQ(unsupportedFeature(witness), std::nullopt) << format;
    }
}

TEST(SupportCheckTest, RejectsOtherConstraintFormats) {
    witness::YamlWitness witness = wellFormed();
    witness.violationSequences[0].segments[0].waypoints[0].constraint->format = "smtlib";
    EXPECT_EQ(unsupportedFeature(witness), "constraints of format 'smtlib' in segment 1");
}

class UnsupportedWitnessTest : public ValidateTest {};

TEST_F(UnsupportedWitnessTest, IsUnknownWithoutRunningTheProgram) {
    program("int main(void) { return 0; }\n");
    const ValidationResult result =
        validate(sequence({{target(1, 18)}, {target(1, 18)}}), [](FakeExecution&) { return returned(0); });
    EXPECT_EQ(result.verdict, Verdict::Unsupported);
    EXPECT_EQ(exitCode(result), 4);
    EXPECT_EQ(result.message, "NITWIT does not support a target waypoint in segment 1, which is not the last segment");
    EXPECT_EQ(result.runs, 0u);
    EXPECT_EQ(scriptRuns_, 0);
}

} // namespace
} // namespace nitwit::validate::test
