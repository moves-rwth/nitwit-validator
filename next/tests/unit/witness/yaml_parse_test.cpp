#include <format>
#include <string>
#include <string_view>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "test_data.hpp"
#include "witness/parse.hpp"

namespace nitwit::witness {
namespace {

using testing::ElementsAre;
using testing::HasSubstr;

/// A witness with one violation_sequence entry whose only segment holds `waypoint`, a YAML flow mapping.
std::string witnessWithWaypoint(std::string_view waypoint) {
    return std::format("- entry_type: violation_sequence\n"
                       "  content:\n"
                       "  - segment:\n"
                       "    - waypoint: {}\n",
                       waypoint);
}

/// The waypoint of witnessWithWaypoint(waypoint), or the parse error.
std::expected<Waypoint, Error> parseWaypoint(std::string_view waypoint) {
    auto witness = parseYamlWitness(witnessWithWaypoint(waypoint));
    if (!witness) {
        return std::unexpected(witness.error());
    }
    return witness->violationSequences.at(0).segments.at(0).waypoints.at(0);
}

/// The error message of parsing `contents`; fails the test if it parses.
std::string errorOf(std::string_view contents) {
    const auto witness = parseYamlWitness(contents);
    EXPECT_FALSE(witness.has_value()) << "parsed: " << contents;
    return witness ? std::string{} : witness.error().message;
}

std::string waypointErrorOf(std::string_view waypoint) {
    return errorOf(witnessWithWaypoint(waypoint));
}

/// The constraint value of an assumption waypoint whose constraint is written as `value` in a YAML block mapping.
std::string constraintValue(std::string_view value) {
    const std::string contents = std::format("- entry_type: violation_sequence\n"
                                             "  content:\n"
                                             "  - segment:\n"
                                             "    - waypoint:\n"
                                             "        type: assumption\n"
                                             "        action: follow\n"
                                             "        location: {{file_name: a.c, line: 3}}\n"
                                             "        constraint:\n"
                                             "          value: {}\n",
                                             value);
    const auto witness = parseYamlWitness(contents);
    if (!witness) {
        ADD_FAILURE() << witness.error().message;
        return {};
    }
    return witness->violationSequences.at(0).segments.at(0).waypoints.at(0).constraint.value().value;
}

// ---- Complete witnesses ----

TEST(YamlParseTest, ReadsTheMetadata) {
    const auto witness = parseYamlWitness(readTestFile("violation-2.0.yml"));
    ASSERT_TRUE(witness.has_value()) << witness.error().message;
    ASSERT_EQ(witness->violationSequences.size(), 1u);
    EXPECT_TRUE(witness->otherEntryTypes.empty());
    const Metadata& metadata = witness->violationSequences[0].metadata;
    EXPECT_EQ(metadata.formatVersion, "2.0");
    EXPECT_EQ(metadata.producer.name, "CPAchecker");
    EXPECT_EQ(metadata.producer.version, "4.0");
    EXPECT_THAT(metadata.task.inputFiles, ElementsAre("program.c"));
    EXPECT_EQ(metadata.task.specification, "G ! call(reach_error())");
    EXPECT_EQ(metadata.task.dataModel, "LP64");
    EXPECT_EQ(metadata.task.language, "C");
}

TEST(YamlParseTest, ReadsSegmentsAndWaypointsInOrder) {
    const auto witness = parseYamlWitness(readTestFile("violation-2.0.yml"));
    ASSERT_TRUE(witness.has_value()) << witness.error().message;
    const std::vector<Segment>& segments = witness->violationSequences.at(0).segments;
    ASSERT_EQ(segments.size(), 4u);
    ASSERT_EQ(segments[0].waypoints.size(), 1u);
    ASSERT_EQ(segments[1].waypoints.size(), 2u);
    ASSERT_EQ(segments[2].waypoints.size(), 1u);
    ASSERT_EQ(segments[3].waypoints.size(), 1u);

    const Waypoint& assumption = segments[0].waypoints[0];
    EXPECT_EQ(assumption.type, WaypointType::Assumption);
    EXPECT_EQ(assumption.action, WaypointAction::Follow);
    EXPECT_EQ(assumption.location.fileName, "program.c");
    EXPECT_EQ(assumption.location.line, 10u);
    EXPECT_EQ(assumption.location.column, 5u);
    EXPECT_EQ(assumption.location.function, "main");
    ASSERT_TRUE(assumption.constraint.has_value());
    EXPECT_EQ(assumption.constraint->value, "x == 1");
    EXPECT_EQ(assumption.constraint->format, "c_expression");
    EXPECT_EQ(assumption.threadId, std::nullopt);

    const Waypoint& enter = segments[1].waypoints[0];
    EXPECT_EQ(enter.type, WaypointType::FunctionEnter);
    EXPECT_EQ(enter.action, WaypointAction::Avoid);
    EXPECT_EQ(enter.constraint, std::nullopt);

    EXPECT_EQ(segments[1].waypoints[1].type, WaypointType::Branching);
    EXPECT_EQ(segments[1].waypoints[1].constraint->value, "true");

    const Waypoint& functionReturn = segments[2].waypoints[0];
    EXPECT_EQ(functionReturn.type, WaypointType::FunctionReturn);
    EXPECT_EQ(functionReturn.constraint->value, "\\result == 7");
    EXPECT_EQ(functionReturn.constraint->format, "acsl_expression");

    const Waypoint& target = segments[3].waypoints[0];
    EXPECT_EQ(target.type, WaypointType::Target);
    EXPECT_EQ(target.location.line, 15u);
    EXPECT_EQ(target.location.column, 9u);
}

TEST(YamlParseTest, ReadsFormat21InFlowStyle) {
    const auto witness = parseYamlWitness(readTestFile("violation-2.1.yml"));
    ASSERT_TRUE(witness.has_value()) << witness.error().message;
    const ViolationSequence& sequence = witness->violationSequences.at(0);
    EXPECT_EQ(sequence.metadata.formatVersion, "2.1");
    EXPECT_EQ(sequence.metadata.producer.name, "UAutomizer");
    EXPECT_THAT(sequence.metadata.task.inputFiles, ElementsAre("loop.c"));
    EXPECT_EQ(sequence.metadata.task.dataModel, "ILP32");
    ASSERT_EQ(sequence.segments.size(), 2u);

    const Waypoint& assumption = sequence.segments[0].waypoints.at(0);
    EXPECT_EQ(assumption.location.line, 4u);
    EXPECT_EQ(assumption.location.column, 3u);
    EXPECT_EQ(assumption.constraint->value, "n >= 0");
    EXPECT_EQ(assumption.threadId, 0u);

    const Waypoint& cycle = sequence.segments[1].waypoints.at(0);
    EXPECT_EQ(cycle.type, WaypointType::Branching);
    EXPECT_EQ(cycle.action, WaypointAction::Cycle);
    EXPECT_EQ(cycle.location.function, "main");
    EXPECT_EQ(cycle.constraint->value, "true");
}

// ---- Waypoints ----

TEST(YamlParseTest, ReadsEveryWaypointType) {
    const std::pair<std::string_view, WaypointType> types[] = {
        {"assumption", WaypointType::Assumption},
        {"branching", WaypointType::Branching},
        {"function_enter", WaypointType::FunctionEnter},
        {"function_return", WaypointType::FunctionReturn},
        {"target", WaypointType::Target},
    };
    for (const auto& [text, type] : types) {
        const auto waypoint =
            parseWaypoint(std::format("{{type: {}, action: follow, location: {{file_name: a.c, line: 3}}}}", text));
        ASSERT_TRUE(waypoint.has_value()) << waypoint.error().message;
        EXPECT_EQ(waypoint->type, type) << text;
    }
}

TEST(YamlParseTest, ReadsEveryWaypointAction) {
    const std::pair<std::string_view, WaypointAction> actions[] = {
        {"follow", WaypointAction::Follow},
        {"avoid", WaypointAction::Avoid},
        {"cycle", WaypointAction::Cycle},
    };
    for (const auto& [text, action] : actions) {
        const auto waypoint =
            parseWaypoint(std::format("{{type: assumption, action: {}, location: {{file_name: a.c, line: 3}}}}", text));
        ASSERT_TRUE(waypoint.has_value()) << waypoint.error().message;
        EXPECT_EQ(waypoint->action, action) << text;
    }
}

TEST(YamlParseTest, UnknownWaypointTypeIsAnError) {
    EXPECT_THAT(waypointErrorOf("{type: loop_head, action: follow, location: {file_name: a.c, line: 3}}"),
                HasSubstr("unknown waypoint type 'loop_head'"));
    // The names are case-sensitive.
    EXPECT_THAT(waypointErrorOf("{type: Target, action: follow, location: {file_name: a.c, line: 3}}"),
                HasSubstr("unknown waypoint type 'Target'"));
}

TEST(YamlParseTest, UnknownWaypointActionIsAnError) {
    EXPECT_THAT(waypointErrorOf("{type: target, action: pass, location: {file_name: a.c, line: 3}}"),
                HasSubstr("unknown waypoint action 'pass'"));
}

TEST(YamlParseTest, MissingTypeActionOrLocationIsAnError) {
    EXPECT_THAT(waypointErrorOf("{action: follow, location: {file_name: a.c, line: 3}}"),
                HasSubstr("missing key 'type'"));
    EXPECT_THAT(waypointErrorOf("{type: target, location: {file_name: a.c, line: 3}}"),
                HasSubstr("missing key 'action'"));
    EXPECT_THAT(waypointErrorOf("{type: target, action: follow}"), HasSubstr("missing key 'location'"));
}

TEST(YamlParseTest, WaypointFieldsMustHaveTheRightKind) {
    EXPECT_THAT(waypointErrorOf("{type: [target], action: follow, location: {file_name: a.c, line: 3}}"),
                HasSubstr("'type' must be a scalar, not a sequence"));
    EXPECT_THAT(waypointErrorOf("{type: target, action: follow, location: [a.c, 3]}"),
                HasSubstr("location: expected a mapping, found a sequence"));
    EXPECT_THAT(waypointErrorOf("{type: target, action: follow, location: {file_name: {a: b}, line: 3}}"),
                HasSubstr("'file_name' must be a scalar, not a mapping"));
}

// ---- Locations ----

TEST(YamlParseTest, ColumnAndFunctionAreOptional) {
    const auto waypoint = parseWaypoint("{type: target, action: follow, location: {file_name: a.c, line: 3}}");
    ASSERT_TRUE(waypoint.has_value()) << waypoint.error().message;
    EXPECT_EQ(waypoint->location.fileName, "a.c");
    EXPECT_EQ(waypoint->location.line, 3u);
    EXPECT_EQ(waypoint->location.column, std::nullopt);
    EXPECT_EQ(waypoint->location.function, "");
}

TEST(YamlParseTest, NullValuesCountAsMissing) {
    const auto waypoint =
        parseWaypoint("{type: target, action: follow, location: {file_name: a.c, line: 3, column: null, function: ~}, "
                      "constraint: , thread_id: null}");
    ASSERT_TRUE(waypoint.has_value()) << waypoint.error().message;
    EXPECT_EQ(waypoint->location.column, std::nullopt);
    EXPECT_EQ(waypoint->location.function, "");
    EXPECT_EQ(waypoint->constraint, std::nullopt);
    EXPECT_EQ(waypoint->threadId, std::nullopt);

    EXPECT_THAT(waypointErrorOf("{type: target, action: follow, location: {file_name: a.c, line: null}}"),
                HasSubstr("missing key 'line'"));
}

TEST(YamlParseTest, LineAndColumnMayBeStrings) {
    const auto waypoint =
        parseWaypoint("{type: target, action: follow, location: {file_name: a.c, line: '12', column: \"7\"}}");
    ASSERT_TRUE(waypoint.has_value()) << waypoint.error().message;
    EXPECT_EQ(waypoint->location.line, 12u);
    EXPECT_EQ(waypoint->location.column, 7u);
}

TEST(YamlParseTest, MissingFileNameOrLineIsAnError) {
    EXPECT_THAT(waypointErrorOf("{type: target, action: follow, location: {line: 3}}"),
                HasSubstr("location: missing key 'file_name'"));
    EXPECT_THAT(waypointErrorOf("{type: target, action: follow, location: {file_name: a.c, column: 3}}"),
                HasSubstr("location: missing key 'line'"));
}

TEST(YamlParseTest, LineMustBePositive) {
    EXPECT_THAT(waypointErrorOf("{type: target, action: follow, location: {file_name: a.c, line: 0}}"),
                HasSubstr("'line' must be an integer of at least 1, not '0'"));
    EXPECT_THAT(waypointErrorOf("{type: target, action: follow, location: {file_name: a.c, line: -4}}"),
                HasSubstr("'line' must be an integer of at least 1, not '-4'"));
}

TEST(YamlParseTest, LineMustBeAnInteger) {
    for (const char* line : {"abc", "1.5", "'3 '", "99999999999"}) {
        EXPECT_THAT(waypointErrorOf(
                        std::format("{{type: target, action: follow, location: {{file_name: a.c, line: {}}}}}", line)),
                    HasSubstr("'line' must be an integer of at least 1"))
            << line;
    }
    EXPECT_THAT(waypointErrorOf("{type: target, action: follow, location: {file_name: a.c, line: [3]}}"),
                HasSubstr("'line' must be a scalar, not a sequence"));
}

TEST(YamlParseTest, ColumnMustBePositive) {
    EXPECT_THAT(waypointErrorOf("{type: target, action: follow, location: {file_name: a.c, line: 3, column: 0}}"),
                HasSubstr("'column' must be an integer of at least 1, not '0'"));
    EXPECT_THAT(waypointErrorOf("{type: target, action: follow, location: {file_name: a.c, line: 3, column: -1}}"),
                HasSubstr("'column' must be an integer of at least 1, not '-1'"));
}

// ---- Constraints ----

TEST(YamlParseTest, ConstraintValueKeepsTheTextOfAnyScalar) {
    EXPECT_EQ(constraintValue("true"), "true");
    EXPECT_EQ(constraintValue("5"), "5");
    EXPECT_EQ(constraintValue("0x1F"), "0x1F");
    EXPECT_EQ(constraintValue("x == 1"), "x == 1");
    EXPECT_EQ(constraintValue("'x == 1'"), "x == 1");
    EXPECT_EQ(constraintValue("\"x == 'a'\""), "x == 'a'");
    EXPECT_EQ(constraintValue("''"), "");
}

TEST(YamlParseTest, ConstraintFormatIsKeptAndDefaultsToCExpression) {
    const auto withFormat =
        parseWaypoint("{type: function_return, action: follow, location: {file_name: a.c, line: 3}, "
                      "constraint: {value: '\\result == 1', format: ext_c_expression}}");
    ASSERT_TRUE(withFormat.has_value()) << withFormat.error().message;
    EXPECT_EQ(withFormat->constraint->format, "ext_c_expression");

    const auto withoutFormat = parseWaypoint(
        "{type: branching, action: follow, location: {file_name: a.c, line: 3}, constraint: {value: false}}");
    ASSERT_TRUE(withoutFormat.has_value()) << withoutFormat.error().message;
    EXPECT_EQ(withoutFormat->constraint->format, "c_expression");
}

TEST(YamlParseTest, ConstraintWithoutValueIsAnError) {
    EXPECT_THAT(waypointErrorOf("{type: assumption, action: follow, location: {file_name: a.c, line: 3}, "
                                "constraint: {format: c_expression}}"),
                HasSubstr("constraint: missing key 'value'"));
    EXPECT_THAT(waypointErrorOf("{type: assumption, action: follow, location: {file_name: a.c, line: 3}, "
                                "constraint: {value: null}}"),
                HasSubstr("constraint: missing key 'value'"));
}

TEST(YamlParseTest, ConstraintMustBeAMappingWithAScalarValue) {
    EXPECT_THAT(waypointErrorOf("{type: assumption, action: follow, location: {file_name: a.c, line: 3}, "
                                "constraint: x == 1}"),
                HasSubstr("constraint: expected a mapping, found a scalar"));
    EXPECT_THAT(waypointErrorOf("{type: assumption, action: follow, location: {file_name: a.c, line: 3}, "
                                "constraint: {value: [x == 1]}}"),
                HasSubstr("'value' must be a scalar, not a sequence"));
}

// witness format 2.x: "\r" and "\a" are escape sequences in double-quoted YAML strings only (YAML 1.2, 5.7).
TEST(YamlParseTest, RepairsResultMangledByDoubleQuotes) {
    EXPECT_EQ(constraintValue(R"("\result == 7")"), "\\result == 7");
    EXPECT_EQ(constraintValue(R"("\result > 0 && \result < 9")"), "\\result > 0 && \\result < 9");
}

TEST(YamlParseTest, RepairsAtMangledByDoubleQuotes) {
    EXPECT_EQ(constraintValue(R"("\at(x, Pre) == 1")"), "\\at(x, Pre) == 1");
}

TEST(YamlParseTest, CorrectlyWrittenBackslashesStayUnchanged) {
    EXPECT_EQ(constraintValue(R"("\\result == 7")"), "\\result == 7");
    EXPECT_EQ(constraintValue(R"('\result == 7')"), "\\result == 7");
    EXPECT_EQ(constraintValue(R"(\result == 7)"), "\\result == 7");
    EXPECT_EQ(constraintValue(R"("\\at(x, Pre) == 1")"), "\\at(x, Pre) == 1");
}

TEST(YamlParseTest, OtherCarriageReturnsAndBellsStay) {
    EXPECT_EQ(constraintValue(R"("x\r == 1")"), "x\r == 1");
    EXPECT_EQ(constraintValue(R"("\a == 1")"), "\a == 1");
}

// ---- Thread ids (format 2.1) ----

TEST(YamlParseTest, ThreadIdIsRead) {
    const auto number =
        parseWaypoint("{type: target, action: follow, location: {file_name: a.c, line: 3}, thread_id: 2}");
    ASSERT_TRUE(number.has_value()) << number.error().message;
    EXPECT_EQ(number->threadId, 2u);

    const auto string =
        parseWaypoint("{type: target, action: follow, location: {file_name: a.c, line: 3}, thread_id: '0'}");
    ASSERT_TRUE(string.has_value()) << string.error().message;
    EXPECT_EQ(string->threadId, 0u);
}

TEST(YamlParseTest, NegativeThreadIdIsAnError) {
    EXPECT_THAT(waypointErrorOf("{type: target, action: follow, location: {file_name: a.c, line: 3}, thread_id: -1}"),
                HasSubstr("'thread_id' must be an integer of at least 0, not '-1'"));
    EXPECT_THAT(waypointErrorOf("{type: target, action: follow, location: {file_name: a.c, line: 3}, thread_id: t1}"),
                HasSubstr("'thread_id' must be an integer of at least 0, not 't1'"));
}

// ---- Entries ----

TEST(YamlParseTest, ReadsSeveralViolationSequencesInOrder) {
    const auto witness = parseYamlWitness("- entry_type: violation_sequence\n"
                                          "  metadata: {producer: {name: first}}\n"
                                          "  content: []\n"
                                          "- entry_type: invariant_set\n"
                                          "- entry_type: violation_sequence\n"
                                          "  metadata: {producer: {name: second}}\n"
                                          "  content: []\n");
    ASSERT_TRUE(witness.has_value()) << witness.error().message;
    ASSERT_EQ(witness->violationSequences.size(), 2u);
    EXPECT_EQ(witness->violationSequences[0].metadata.producer.name, "first");
    EXPECT_EQ(witness->violationSequences[1].metadata.producer.name, "second");
    EXPECT_THAT(witness->otherEntryTypes, ElementsAre("invariant_set"));
}

TEST(YamlParseTest, OtherEntryTypesAreListedButNotChecked) {
    const auto witness = parseYamlWitness("- entry_type: invariant_set\n"
                                          "  content: [{invariant: {type: loop_invariant, value: x > 0}}]\n"
                                          "- entry_type: ghost_instrumentation\n"
                                          "  content: 5\n");
    ASSERT_TRUE(witness.has_value()) << witness.error().message;
    EXPECT_TRUE(witness->violationSequences.empty());
    EXPECT_THAT(witness->otherEntryTypes, ElementsAre("invariant_set", "ghost_instrumentation"));
}

TEST(YamlParseTest, EntryWithoutEntryTypeIsAnError) {
    EXPECT_THAT(errorOf("- entry_type: violation_sequence\n  content: []\n- metadata: {}\n"),
                HasSubstr("entry 2: missing key 'entry_type'"));
}

TEST(YamlParseTest, EntryMustBeAMapping) {
    EXPECT_THAT(errorOf("- violation_sequence\n"), HasSubstr("entry 1: expected a mapping, found a scalar"));
}

TEST(YamlParseTest, MetadataIsOptional) {
    const auto witness = parseYamlWitness(witnessWithWaypoint("{type: target, action: follow, "
                                                              "location: {file_name: a.c, line: 3}}"));
    ASSERT_TRUE(witness.has_value()) << witness.error().message;
    const Metadata& metadata = witness->violationSequences.at(0).metadata;
    EXPECT_EQ(metadata.formatVersion, "");
    EXPECT_EQ(metadata.producer.name, "");
    EXPECT_TRUE(metadata.task.inputFiles.empty());
    EXPECT_EQ(metadata.task.dataModel, "");
}

TEST(YamlParseTest, MetadataFieldsAreOptional) {
    const auto witness = parseYamlWitness("- entry_type: violation_sequence\n"
                                          "  metadata: {format_version: '2.0', task: {data_model: ILP32}}\n"
                                          "  content: []\n");
    ASSERT_TRUE(witness.has_value()) << witness.error().message;
    const Metadata& metadata = witness->violationSequences.at(0).metadata;
    EXPECT_EQ(metadata.formatVersion, "2.0");
    EXPECT_EQ(metadata.producer.version, "");
    EXPECT_EQ(metadata.task.specification, "");
    EXPECT_EQ(metadata.task.dataModel, "ILP32");
}

TEST(YamlParseTest, MetadataFieldsMustHaveTheRightKind) {
    const auto withMetadata = [](std::string_view metadata) {
        return std::format("- entry_type: violation_sequence\n  metadata: {}\n  content: []\n", metadata);
    };
    EXPECT_THAT(errorOf(withMetadata("[2.0]")), HasSubstr("entry 1, metadata: expected a mapping, found a sequence"));
    EXPECT_THAT(errorOf(withMetadata("{producer: CPAchecker}")),
                HasSubstr("entry 1, metadata, producer: expected a mapping, found a scalar"));
    EXPECT_THAT(errorOf(withMetadata("{task: {input_files: a.c}}")),
                HasSubstr("entry 1, metadata, task: 'input_files' must be a sequence, not a scalar"));
    EXPECT_THAT(errorOf(withMetadata("{task: {input_files: [[a.c]]}}")),
                HasSubstr("'input_files' must be a scalar, not a sequence"));
    EXPECT_THAT(errorOf(withMetadata("{format_version: {major: 2}}")),
                HasSubstr("'format_version' must be a scalar, not a mapping"));
}

// ---- Document and sequence structure ----

TEST(YamlParseTest, WitnessMustBeASequenceOfEntries) {
    EXPECT_THAT(errorOf("entry_type: violation_sequence\n"),
                HasSubstr("the witness must be a sequence of entries, not a mapping"));
    EXPECT_THAT(errorOf("witness\n"), HasSubstr("the witness must be a sequence of entries, not a scalar"));
}

TEST(YamlParseTest, EmptyWitnessIsAnError) {
    EXPECT_EQ(errorOf(""), "the witness is empty");
    EXPECT_EQ(errorOf("# only a comment\n"), "the witness is empty");
}

TEST(YamlParseTest, EmptySequenceHasNoEntries) {
    const auto witness = parseYamlWitness("[]");
    ASSERT_TRUE(witness.has_value()) << witness.error().message;
    EXPECT_TRUE(witness->violationSequences.empty());
    EXPECT_TRUE(witness->otherEntryTypes.empty());
}

TEST(YamlParseTest, MalformedYamlIsAnErrorWithItsPosition) {
    EXPECT_THAT(errorOf("- entry_type: violation_sequence\n  content: [a, b\n"),
                HasSubstr("invalid YAML at line 3, column 1"));
    EXPECT_THAT(errorOf("- value: \"\\q\"\n"), HasSubstr("invalid YAML at line 1"));
}

TEST(YamlParseTest, DeepNestingIsAnErrorNotACrash) {
    EXPECT_THAT(errorOf("- " + std::string(10'000, '[')), HasSubstr("invalid YAML"));
}

TEST(YamlParseTest, ViolationSequenceNeedsContent) {
    EXPECT_THAT(errorOf("- entry_type: violation_sequence\n"), HasSubstr("entry 1: missing key 'content'"));
    EXPECT_THAT(errorOf("- entry_type: violation_sequence\n  content: {segment: []}\n"),
                HasSubstr("entry 1: 'content' must be a sequence, not a mapping"));
}

TEST(YamlParseTest, SegmentItemsNeedTheKeySegment) {
    EXPECT_THAT(errorOf("- entry_type: violation_sequence\n  content: [{waypoints: []}]\n"),
                HasSubstr("entry 1, segment 1: missing key 'segment'"));
    EXPECT_THAT(errorOf("- entry_type: violation_sequence\n  content: [segment]\n"),
                HasSubstr("entry 1, segment 1: expected a mapping, found a scalar"));
    EXPECT_THAT(errorOf("- entry_type: violation_sequence\n  content: [~]\n"),
                HasSubstr("entry 1, segment 1: expected a mapping, found an empty value"));
    EXPECT_THAT(errorOf("- entry_type: violation_sequence\n  content: [{segment: {waypoint: {}}}]\n"),
                HasSubstr("entry 1, segment 1: 'segment' must be a sequence, not a mapping"));
}

TEST(YamlParseTest, WaypointItemsNeedTheKeyWaypoint) {
    EXPECT_THAT(errorOf("- entry_type: violation_sequence\n  content: [{segment: [{type: target}]}]\n"),
                HasSubstr("entry 1, segment 1, waypoint 1: missing key 'waypoint'"));
    EXPECT_THAT(errorOf("- entry_type: violation_sequence\n  content: [{segment: [{waypoint: target}]}]\n"),
                HasSubstr("entry 1, segment 1, waypoint 1: expected a mapping, found a scalar"));
}

TEST(YamlParseTest, ErrorsNameTheEntrySegmentAndWaypoint) {
    const std::string waypoint = "{waypoint: {type: target, action: follow, location: {file_name: a.c, line: 3}}}";
    const std::string contents = std::format("- entry_type: invariant_set\n"
                                             "- entry_type: violation_sequence\n"
                                             "  content:\n"
                                             "  - segment: [{0}]\n"
                                             "  - segment: [{0}, {0}, {{waypoint: {{type: jump}}}}]\n",
                                             waypoint);
    EXPECT_EQ(errorOf(contents), "entry 2, segment 2, waypoint 3: unknown waypoint type 'jump'");
}

TEST(YamlParseTest, UnknownKeysAreIgnored) {
    const auto witness =
        parseYamlWitness("- entry_type: violation_sequence\n"
                         "  comment: generated\n"
                         "  metadata: {producer: {name: x, flags: [a]}, extra: 1}\n"
                         "  content:\n"
                         "  - segment:\n"
                         "    - waypoint: {type: target, action: follow, location: {file_name: a.c, line: 3, "
                         "offset: 7}, note: here}\n");
    ASSERT_TRUE(witness.has_value()) << witness.error().message;
    EXPECT_EQ(witness->violationSequences.at(0).segments.at(0).waypoints.at(0).location.line, 3u);
}

} // namespace
} // namespace nitwit::witness
