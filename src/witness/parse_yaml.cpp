#include <algorithm>
#include <format>
#include <iterator>
#include <new>
#include <string>
#include <string_view>
#include <utility>

#include "support/assert.hpp"
#include "witness/number.hpp"
#include "witness/parse.hpp"
#include "witness/yaml_tree.hpp"

namespace nitwit::witness {

namespace {

using Kind = YamlNode::Kind;

// A violation of the format; parseYamlWitness turns it into an Error.
struct FormatError {
    std::string message;
};

/// The part of the witness being read, for messages such as "entry 1, segment 2, waypoint 3, location".
struct Where {
    std::size_t entry = 0;
    std::size_t segment = 0;  // 0 outside of segments
    std::size_t waypoint = 0; // 0 outside of waypoints
    std::string_view part;    // e.g. "location"; empty for the entry, segment or waypoint itself

    [[nodiscard]] Where in(std::string_view name) const {
        Where where = *this;
        where.part = name;
        return where;
    }
};

[[noreturn]] void fail(const Where& where, const YamlNode& node, std::string_view problem) {
    std::string text = std::format("entry {}", where.entry);
    if (where.segment != 0) {
        text += std::format(", segment {}", where.segment);
    }
    if (where.waypoint != 0) {
        text += std::format(", waypoint {}", where.waypoint);
    }
    if (!where.part.empty()) {
        text += std::format(", {}", where.part);
    }
    throw FormatError{std::format("{} (line {}, column {}): {}", text, node.line, node.column, problem)};
}

std::string_view kindName(Kind kind) {
    switch (kind) {
    case Kind::Null:
        return "null";
    case Kind::Scalar:
        return "a scalar";
    case Kind::Sequence:
        return "a sequence";
    case Kind::Map:
        return "a mapping";
    }
    NITWIT_UNREACHABLE("unknown YAML node kind");
}

/// Checks the kind of an item of a sequence.
void expectItem(const YamlNode& node, Kind kind, const Where& where) {
    if (node.kind != kind) {
        fail(where, node, std::format("expected {}, found {}", kindName(kind), kindName(node.kind)));
    }
}

/// Checks the kind of the value of `key`.
void expectValue(const YamlNode& node, Kind kind, const Where& where, std::string_view key) {
    if (node.kind != kind) {
        fail(where, node, std::format("'{}' must be {}, not {}", key, kindName(kind), kindName(node.kind)));
    }
}

const YamlNode& require(const YamlNode& map, std::string_view key, const Where& where) {
    const YamlNode* value = map.find(key);
    if (value == nullptr) {
        fail(where, map, std::format("missing key '{}'", key));
    }
    return *value;
}

/// The value of an optional key, or nullptr if it is absent. A null value counts as absent.
const YamlNode* findOptional(const YamlNode& map, std::string_view key) {
    const YamlNode* value = map.find(key);
    return value != nullptr && value->kind != Kind::Null ? value : nullptr;
}

/// The text of a scalar, whatever its YAML type: `true` and `5` give "true" and "5".
const std::string& text(const YamlNode& node, const Where& where, std::string_view key) {
    expectValue(node, Kind::Scalar, where, key);
    return node.text;
}

const std::string& requiredText(const YamlNode& map, std::string_view key, const Where& where) {
    return text(require(map, key, where), where, key);
}

/// The text of an optional key, or "" if it is absent.
std::string optionalText(const YamlNode& map, std::string_view key, const Where& where) {
    const YamlNode* value = findOptional(map, key);
    return value != nullptr ? text(*value, where, key) : std::string{};
}

/// A YAML integer or a string holding one; the schemas give lines and columns a minimum of 1, thread ids of 0.
std::uint32_t readNumber(const YamlNode& node, const Where& where, std::string_view key, std::uint32_t minimum) {
    const std::string& digits = text(node, where, key);
    const auto value = parseUnsigned(digits);
    if (!value || *value < minimum) {
        fail(where, node, std::format("'{}' must be an integer of at least {}, not '{}'", key, minimum, digits));
    }
    return *value;
}

template <typename Enum, std::size_t size>
Enum lookup(const std::pair<std::string_view, Enum> (&table)[size], const YamlNode& map, std::string_view key,
            const Where& where) {
    const YamlNode& node = require(map, key, where);
    const std::string& name = text(node, where, key);
    const auto* found = std::ranges::find(table, name, &std::pair<std::string_view, Enum>::first);
    if (found == std::end(table)) {
        fail(where, node, std::format("unknown waypoint {} '{}'", key, name));
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
/// sequences of a carriage return and a bell character (YAML 1.2.2, section 5.7).
std::string repairEscapes(std::string value) {
    replaceAll(value, "\result", "\\result");
    replaceAll(value, "\at(", "\\at(");
    return value;
}

/// After the repair, other control characters most likely come from other backslashes in a double-quoted string as
/// well, e.g. "\valid" (a vertical tab) or "\forall" (a form feed). Tab and newline may be meant.
void checkControlCharacters(const std::string& value, const YamlNode& node, const Where& where) {
    const auto isControl = [](char c) {
        const auto byte = static_cast<unsigned char>(c);
        return (byte < 0x20 && c != '\t' && c != '\n') || byte == 0x7F;
    };
    if (const auto found = std::ranges::find_if(value, isControl); found != value.end()) {
        fail(where, node,
             std::format("'value' contains the control character 0x{:02X}; in a double-quoted YAML string, a "
                         "backslash starts an escape sequence, so write \\\\ or use single quotes",
                         static_cast<unsigned>(static_cast<unsigned char>(*found))));
    }
}

Location readLocation(const YamlNode& node, const Where& where) {
    Location location;
    location.fileName = requiredText(node, "file_name", where);
    location.line = readNumber(require(node, "line", where), where, "line", 1);
    if (const YamlNode* column = findOptional(node, "column")) {
        location.column = readNumber(*column, where, "column", 1);
    }
    location.function = optionalText(node, "function", where);
    return location;
}

Constraint readConstraint(const YamlNode& node, const Where& where) {
    Constraint constraint;
    const YamlNode& value = require(node, "value", where);
    constraint.value = repairEscapes(text(value, where, "value"));
    checkControlCharacters(constraint.value, value, where);
    constraint.format = optionalText(node, "format", where);
    return constraint;
}

Waypoint readWaypoint(const YamlNode& node, const Where& where) {
    Waypoint waypoint;
    waypoint.type = lookup(waypointTypes, node, "type", where);
    waypoint.action = lookup(waypointActions, node, "action", where);
    const YamlNode& location = require(node, "location", where);
    expectValue(location, Kind::Map, where, "location");
    waypoint.location = readLocation(location, where.in("location"));
    if (const YamlNode* constraint = findOptional(node, "constraint")) {
        expectValue(*constraint, Kind::Map, where, "constraint");
        waypoint.constraint = readConstraint(*constraint, where.in("constraint"));
    }
    if (const YamlNode* threadId = findOptional(node, "thread_id")) {
        waypoint.threadId = readNumber(*threadId, where, "thread_id", 0);
    }
    return waypoint;
}

Segment readSegment(const YamlNode& item, Where where) {
    expectItem(item, Kind::Map, where);
    const YamlNode& waypoints = require(item, "segment", where);
    expectValue(waypoints, Kind::Sequence, where, "segment");
    Segment segment;
    segment.waypoints.reserve(waypoints.children.size());
    for (const YamlNode& waypointItem : waypoints.children) {
        where.waypoint = segment.waypoints.size() + 1;
        expectItem(waypointItem, Kind::Map, where);
        const YamlNode& waypoint = require(waypointItem, "waypoint", where);
        expectValue(waypoint, Kind::Map, where, "waypoint");
        segment.waypoints.push_back(readWaypoint(waypoint, where));
    }
    return segment;
}

Producer readProducer(const YamlNode& node, const Where& where) {
    return {.name = optionalText(node, "name", where), .version = optionalText(node, "version", where)};
}

Task readTask(const YamlNode& node, const Where& where) {
    Task task;
    if (const YamlNode* inputFiles = findOptional(node, "input_files")) {
        expectValue(*inputFiles, Kind::Sequence, where, "input_files");
        for (const YamlNode& file : inputFiles->children) {
            if (file.kind != Kind::Scalar) {
                fail(where, file, std::format("items of 'input_files' must be scalars, not {}", kindName(file.kind)));
            }
            task.inputFiles.push_back(file.text);
        }
    }
    task.specification = optionalText(node, "specification", where);
    task.dataModel = optionalText(node, "data_model", where);
    task.language = optionalText(node, "language", where);
    return task;
}

Metadata readMetadata(const YamlNode& node, const Where& where) {
    Metadata metadata;
    metadata.formatVersion = optionalText(node, "format_version", where);
    if (const YamlNode* producer = findOptional(node, "producer")) {
        expectValue(*producer, Kind::Map, where, "producer");
        metadata.producer = readProducer(*producer, where.in("metadata, producer"));
    }
    if (const YamlNode* task = findOptional(node, "task")) {
        expectValue(*task, Kind::Map, where, "task");
        metadata.task = readTask(*task, where.in("metadata, task"));
    }
    return metadata;
}

ViolationSequence readViolationSequence(const YamlNode& entry, const Where& where) {
    ViolationSequence sequence;
    if (const YamlNode* metadata = findOptional(entry, "metadata")) {
        expectValue(*metadata, Kind::Map, where, "metadata");
        sequence.metadata = readMetadata(*metadata, where.in("metadata"));
    }
    const YamlNode& content = require(entry, "content", where);
    expectValue(content, Kind::Sequence, where, "content");
    sequence.segments.reserve(content.children.size());
    for (const YamlNode& item : content.children) {
        Where segmentWhere = where;
        segmentWhere.segment = sequence.segments.size() + 1;
        sequence.segments.push_back(readSegment(item, segmentWhere));
    }
    return sequence;
}

YamlWitness readWitness(const YamlNode& root) {
    if (root.kind == Kind::Null) {
        throw FormatError{"the witness is empty"};
    }
    if (root.kind != Kind::Sequence) {
        throw FormatError{std::format("the witness must be a sequence of entries, not {}", kindName(root.kind))};
    }
    YamlWitness witness;
    for (const YamlNode& entry : root.children) {
        const Where where{.entry = witness.violationSequences.size() + witness.otherEntryTypes.size() + 1,
                          .segment = 0,
                          .waypoint = 0,
                          .part = {}};
        expectItem(entry, Kind::Map, where);
        const std::string& type = requiredText(entry, "entry_type", where);
        if (type == "violation_sequence") {
            witness.violationSequences.push_back(readViolationSequence(entry, where));
        } else {
            witness.otherEntryTypes.push_back(type);
        }
    }
    return witness;
}

} // namespace

std::expected<YamlWitness, Error> parseYamlWitness(std::string_view contents) {
    try {
        const auto tree = readYamlTree(contents);
        if (!tree) {
            return std::unexpected(tree.error());
        }
        return readWitness(*tree);
    } catch (const FormatError& error) {
        return std::unexpected(Error{error.message});
    } catch (const std::bad_alloc&) {
        return std::unexpected(Error{"out of memory while reading the witness"});
    }
}

} // namespace nitwit::witness
