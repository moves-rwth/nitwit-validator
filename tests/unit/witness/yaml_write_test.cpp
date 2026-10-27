#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "witness/parse.hpp"
#include "witness/write.hpp"

namespace nitwit::witness {
namespace {

using testing::HasSubstr;

Waypoint waypointOf(WaypointType type, WaypointAction action, Location location,
                    std::optional<Constraint> constraint = std::nullopt) {
    Waypoint waypoint;
    waypoint.type = type;
    waypoint.action = action;
    waypoint.location = std::move(location);
    waypoint.constraint = std::move(constraint);
    return waypoint;
}

ViolationSequence sequenceOf(std::vector<Segment> segments) {
    ViolationSequence sequence;
    sequence.metadata = {.formatVersion = "2.0",
                         .producer = {.name = "NITWIT", .version = "4.2.0"},
                         .task = {.inputFiles = {"dir/a.c"},
                                  .specification = "CHECK( init(main()), LTL(G ! call(reach_error())) )",
                                  .dataModel = "ILP32",
                                  .language = "C"}};
    sequence.segments = std::move(segments);
    return sequence;
}

const Provenance provenance{.uuid = "1b4e28ba-2fa1-81d2-883f-0016d3cca427",
                            .creationTime = "2026-10-27T12:00:00Z",
                            .inputFileHashes = {std::string(64, 'a')}};

/// The one violation sequence of the written witness, read back; fails the test if it does not parse.
ViolationSequence readBack(const std::string& text) {
    auto witness = parseYamlWitness(text);
    EXPECT_TRUE(witness.has_value()) << (witness ? "" : witness.error().message) << "\n" << text;
    if (!witness || witness->violationSequences.size() != 1) {
        return {};
    }
    return std::move(witness->violationSequences.front());
}

TEST(YamlWriteTest, EveryWaypointTypeAndActionReadsBack) {
    const ViolationSequence written = sequenceOf({
        {.waypoints = {waypointOf(WaypointType::Assumption, WaypointAction::Avoid,
                                  {.fileName = "dir/a.c", .line = 3, .column = 5, .function = "main"},
                                  Constraint{.value = "x == 1", .format = "c_expression"}),
                       waypointOf(WaypointType::Branching, WaypointAction::Follow,
                                  {.fileName = "dir/a.c", .line = 4, .column = std::nullopt, .function = ""},
                                  Constraint{.value = "true", .format = ""})}},
        {.waypoints = {waypointOf(WaypointType::FunctionEnter, WaypointAction::Follow,
                                  {.fileName = "dir/a.c", .line = 5, .column = 12, .function = "f"})}},
        {.waypoints = {waypointOf(WaypointType::FunctionReturn, WaypointAction::Follow,
                                  {.fileName = "dir/a.c", .line = 6, .column = 35, .function = "main"},
                                  Constraint{.value = "\\result == -5", .format = "acsl_expression"})}},
        {.waypoints = {waypointOf(WaypointType::Target, WaypointAction::Follow,
                                  {.fileName = "dir/a.c", .line = 7, .column = 9, .function = "main"})}},
    });
    const ViolationSequence read = readBack(writeYamlWitness(written, provenance));
    ASSERT_EQ(read.segments.size(), written.segments.size());
    for (std::size_t s = 0; s < written.segments.size(); ++s) {
        ASSERT_EQ(read.segments[s].waypoints.size(), written.segments[s].waypoints.size());
        for (std::size_t w = 0; w < written.segments[s].waypoints.size(); ++w) {
            const Waypoint& expected = written.segments[s].waypoints[w];
            const Waypoint& actual = read.segments[s].waypoints[w];
            EXPECT_EQ(actual.type, expected.type);
            EXPECT_EQ(actual.action, expected.action);
            EXPECT_EQ(actual.location.fileName, expected.location.fileName);
            EXPECT_EQ(actual.location.line, expected.location.line);
            EXPECT_EQ(actual.location.column, expected.location.column);
            EXPECT_EQ(actual.location.function, expected.location.function);
            ASSERT_EQ(actual.constraint.has_value(), expected.constraint.has_value());
            if (expected.constraint) {
                EXPECT_EQ(actual.constraint->value, expected.constraint->value);
                EXPECT_EQ(actual.constraint->format, expected.constraint->format);
            }
        }
    }
}

TEST(YamlWriteTest, MetadataAndProvenanceAreWritten) {
    const std::string text = writeYamlWitness(sequenceOf({}), provenance);
    const ViolationSequence read = readBack(text);
    EXPECT_EQ(read.metadata.formatVersion, "2.0");
    EXPECT_EQ(read.metadata.producer.name, "NITWIT");
    EXPECT_EQ(read.metadata.producer.version, "4.2.0");
    EXPECT_EQ(read.metadata.task.inputFiles, std::vector<std::string>{"dir/a.c"});
    EXPECT_EQ(read.metadata.task.specification, "CHECK( init(main()), LTL(G ! call(reach_error())) )");
    EXPECT_EQ(read.metadata.task.dataModel, "ILP32");
    EXPECT_EQ(read.metadata.task.language, "C");
    // The model does not hold these; the schema of format 2.0 requires them.
    EXPECT_THAT(text, HasSubstr("    uuid: '1b4e28ba-2fa1-81d2-883f-0016d3cca427'\n"));
    EXPECT_THAT(text, HasSubstr("    creation_time: '2026-10-27T12:00:00Z'\n"));
    EXPECT_THAT(text, HasSubstr("      input_file_hashes:\n        'dir/a.c': '" + std::string(64, 'a') + "'\n"));
}

TEST(YamlWriteTest, StringsWithQuotesBackslashesAndControlCharactersReadBack) {
    const std::vector<std::string> values = {
        "\\result == 'a'", "x == \"y\" # no comment", "a: b, {c}", "tab\there", "line\nbreak", "bell\a", " blanks "};
    for (const std::string& value : values) {
        // The parser rejects control characters other than tab and newline in constraint values.
        const std::string constraint = value.find('\a') == std::string::npos ? value : "1";
        const ViolationSequence written =
            sequenceOf({{.waypoints = {waypointOf(WaypointType::Assumption, WaypointAction::Follow,
                                                  {.fileName = value, .line = 1, .column = 1, .function = value},
                                                  Constraint{.value = constraint, .format = ""})}}});
        const std::string text = writeYamlWitness(written, provenance);
        auto witness = parseYamlWitness(text);
        ASSERT_TRUE(witness.has_value()) << witness.error().message << "\n" << text;
        const Waypoint& read = witness->violationSequences.at(0).segments.at(0).waypoints.at(0);
        EXPECT_EQ(read.location.fileName, value);
        EXPECT_EQ(read.location.function, value);
        EXPECT_EQ(read.constraint->value, constraint);
    }
}

TEST(YamlWriteTest, TheBackslashOfResultNeedsNoEscape) {
    const ViolationSequence written =
        sequenceOf({{.waypoints = {waypointOf(WaypointType::FunctionReturn, WaypointAction::Follow,
                                              {.fileName = "a.c", .line = 6, .column = 35, .function = "main"},
                                              Constraint{.value = "\\result == 5", .format = "acsl_expression"})}}});
    EXPECT_THAT(writeYamlWitness(written, provenance),
                HasSubstr("        constraint: {value: '\\result == 5', format: 'acsl_expression'}\n"));
}

} // namespace
} // namespace nitwit::witness
