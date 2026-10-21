#include <algorithm>
#include <format>
#include <iterator>
#include <optional>
#include <string>
#include <utility>

#include <yaml-cpp/yaml.h>

#include "witness/number.hpp"
#include "witness/parse.hpp"

namespace nitwit::witness {

namespace {

// A violation of the format; parseYamlWitness turns it into an Error.
struct FormatError {
    std::string message;
};

/// `where` names the part of the witness, e.g. "entry 1, segment 2, waypoint 3, location".
[[noreturn]] void fail(std::string_view where, std::string_view problem) {
    throw FormatError{std::format("{}: {}", where, problem)};
}

std::string_view kindName(const YAML::Node& node) {
    switch (node.Type()) {
    case YAML::NodeType::Map:
        return "a mapping";
    case YAML::NodeType::Sequence:
        return "a sequence";
    case YAML::NodeType::Scalar:
        return "a scalar";
    case YAML::NodeType::Null:
    case YAML::NodeType::Undefined:
        break;
    }
    return "an empty value";
}

void expectMap(const YAML::Node& node, std::string_view where) {
    if (!node.IsMap()) {
        fail(where, std::format("expected a mapping, found {}", kindName(node)));
    }
}

void expectSequence(const YAML::Node& node, std::string_view where, std::string_view key) {
    if (!node.IsSequence()) {
        fail(where, std::format("'{}' must be a sequence, not {}", key, kindName(node)));
    }
}

/// The value of `key` in the mapping `map`. A key with a null value counts as absent.
std::optional<YAML::Node> find(const YAML::Node& map, const std::string& key) {
    YAML::Node value = map[key];
    if (!value.IsDefined() || value.IsNull()) {
        return std::nullopt;
    }
    return value;
}

YAML::Node require(const YAML::Node& map, const std::string& key, std::string_view where) {
    auto value = find(map, key);
    if (!value) {
        fail(where, std::format("missing key '{}'", key));
    }
    return *value;
}

/// The text of a scalar, whatever its YAML type: `true` and `5` give "true" and "5".
std::string scalarText(const YAML::Node& node, std::string_view where, std::string_view key) {
    if (!node.IsScalar()) {
        fail(where, std::format("'{}' must be a scalar, not {}", key, kindName(node)));
    }
    return node.Scalar();
}

std::string requiredText(const YAML::Node& map, const std::string& key, std::string_view where) {
    return scalarText(require(map, key, where), where, key);
}

/// The text of `key`, or "" if it is absent.
std::string optionalText(const YAML::Node& map, const std::string& key, std::string_view where) {
    const auto value = find(map, key);
    return value ? scalarText(*value, where, key) : std::string{};
}

/// A YAML integer or a string holding one; the schemas give lines and columns a minimum of 1, thread ids of 0.
std::uint32_t readNumber(const YAML::Node& node, std::string_view where, std::string_view key, std::uint32_t minimum) {
    const std::string text = scalarText(node, where, key);
    const auto value = parseUnsigned(text);
    if (!value || *value < minimum) {
        fail(where, std::format("'{}' must be an integer of at least {}, not '{}'", key, minimum, text));
    }
    return *value;
}

template <typename Enum, std::size_t size>
Enum lookup(const std::pair<std::string_view, Enum> (&table)[size], std::string_view text, std::string_view where,
            std::string_view what) {
    const auto* found = std::ranges::find(table, text, &std::pair<std::string_view, Enum>::first);
    if (found == std::end(table)) {
        fail(where, std::format("unknown {} '{}'", what, text));
    }
    return found->second;
}

constexpr std::pair<std::string_view, WaypointType> waypointTypes[] = {
    {"assumption", WaypointType::Assumption},
    {"branching", WaypointType::Branching},
    {"function_enter", WaypointType::FunctionEnter},
    {"function_return", WaypointType::FunctionReturn},
    {"target", WaypointType::Target},
};

constexpr std::pair<std::string_view, WaypointAction> waypointActions[] = {
    {"follow", WaypointAction::Follow},
    {"avoid", WaypointAction::Avoid},
    {"cycle", WaypointAction::Cycle},
};

void replaceAll(std::string& text, std::string_view from, std::string_view to) {
    for (std::size_t pos = text.find(from); pos != std::string::npos; pos = text.find(from, pos + to.size())) {
        text.replace(pos, from.size(), to);
    }
}

/// Some producers write "\result" and "\at(" into double-quoted YAML strings, where "\r" and "\a" are the escape
/// sequences of a carriage return and a bell character (YAML 1.2, section 5.7).
std::string repairEscapes(std::string value) {
    replaceAll(value, "\result", "\\result");
    replaceAll(value, "\at(", "\\at(");
    return value;
}

Location readLocation(const YAML::Node& node, std::string_view where) {
    expectMap(node, where);
    Location location;
    location.fileName = requiredText(node, "file_name", where);
    location.line = readNumber(require(node, "line", where), where, "line", 1);
    if (const auto column = find(node, "column")) {
        location.column = readNumber(*column, where, "column", 1);
    }
    location.function = optionalText(node, "function", where);
    return location;
}

Constraint readConstraint(const YAML::Node& node, std::string_view where) {
    expectMap(node, where);
    Constraint constraint;
    constraint.value = repairEscapes(requiredText(node, "value", where));
    if (const auto format = find(node, "format")) {
        constraint.format = scalarText(*format, where, "format");
    }
    return constraint;
}

Waypoint readWaypoint(const YAML::Node& node, const std::string& where) {
    expectMap(node, where);
    Waypoint waypoint;
    waypoint.type = lookup(waypointTypes, requiredText(node, "type", where), where, "waypoint type");
    waypoint.action = lookup(waypointActions, requiredText(node, "action", where), where, "waypoint action");
    waypoint.location = readLocation(require(node, "location", where), where + ", location");
    if (const auto constraint = find(node, "constraint")) {
        waypoint.constraint = readConstraint(*constraint, where + ", constraint");
    }
    if (const auto threadId = find(node, "thread_id")) {
        waypoint.threadId = readNumber(*threadId, where, "thread_id", 0);
    }
    return waypoint;
}

Segment readSegment(const YAML::Node& node, const std::string& where) {
    expectMap(node, where);
    const YAML::Node waypoints = require(node, "segment", where);
    expectSequence(waypoints, where, "segment");
    Segment segment;
    for (const YAML::Node& item : waypoints) {
        const std::string waypointWhere = std::format("{}, waypoint {}", where, segment.waypoints.size() + 1);
        expectMap(item, waypointWhere);
        segment.waypoints.push_back(readWaypoint(require(item, "waypoint", waypointWhere), waypointWhere));
    }
    return segment;
}

Metadata readMetadata(const YAML::Node& node, const std::string& where) {
    expectMap(node, where);
    Metadata metadata;
    metadata.formatVersion = optionalText(node, "format_version", where);
    if (const auto producer = find(node, "producer")) {
        const std::string producerWhere = where + ", producer";
        expectMap(*producer, producerWhere);
        metadata.producer.name = optionalText(*producer, "name", producerWhere);
        metadata.producer.version = optionalText(*producer, "version", producerWhere);
    }
    if (const auto task = find(node, "task")) {
        const std::string taskWhere = where + ", task";
        expectMap(*task, taskWhere);
        if (const auto inputFiles = find(*task, "input_files")) {
            expectSequence(*inputFiles, taskWhere, "input_files");
            for (const YAML::Node& file : *inputFiles) {
                metadata.task.inputFiles.push_back(scalarText(file, taskWhere, "input_files"));
            }
        }
        metadata.task.specification = optionalText(*task, "specification", taskWhere);
        metadata.task.dataModel = optionalText(*task, "data_model", taskWhere);
        metadata.task.language = optionalText(*task, "language", taskWhere);
    }
    return metadata;
}

ViolationSequence readViolationSequence(const YAML::Node& entry, const std::string& where) {
    ViolationSequence sequence;
    if (const auto metadata = find(entry, "metadata")) {
        sequence.metadata = readMetadata(*metadata, where + ", metadata");
    }
    const YAML::Node content = require(entry, "content", where);
    expectSequence(content, where, "content");
    for (const YAML::Node& item : content) {
        sequence.segments.push_back(
            readSegment(item, std::format("{}, segment {}", where, sequence.segments.size() + 1)));
    }
    return sequence;
}

YamlWitness readWitness(const YAML::Node& root) {
    if (root.IsNull()) {
        throw FormatError{"the witness is empty"};
    }
    if (!root.IsSequence()) {
        throw FormatError{std::format("the witness must be a sequence of entries, not {}", kindName(root))};
    }
    YamlWitness witness;
    std::size_t entryNumber = 0;
    for (const YAML::Node& entry : root) {
        const std::string where = std::format("entry {}", ++entryNumber);
        expectMap(entry, where);
        std::string type = requiredText(entry, "entry_type", where);
        if (type == "violation_sequence") {
            witness.violationSequences.push_back(readViolationSequence(entry, where));
        } else {
            witness.otherEntryTypes.push_back(std::move(type));
        }
    }
    return witness;
}

} // namespace

std::expected<YamlWitness, Error> parseYamlWitness(std::string_view contents) {
    try {
        return readWitness(YAML::Load(std::string(contents)));
    } catch (const FormatError& error) {
        return std::unexpected(Error{error.message});
    } catch (const YAML::Exception& exception) {
        if (exception.mark.is_null()) {
            return std::unexpected(Error{std::format("invalid YAML: {}", exception.msg)});
        }
        return std::unexpected(Error{std::format("invalid YAML at line {}, column {}: {}", exception.mark.line + 1,
                                                 exception.mark.column + 1, exception.msg)});
    }
}

} // namespace nitwit::witness
