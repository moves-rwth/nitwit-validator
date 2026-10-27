#include <algorithm>
#include <cstddef>
#include <format>
#include <iterator>
#include <string_view>
#include <utility>

#include "witness/write.hpp"

namespace nitwit::witness {

namespace {

bool isControl(char c) {
    const auto byte = static_cast<unsigned char>(c);
    return byte < 0x20 || byte == 0x7F;
}

// YAML 1.2.2, 7.3.2: a single-quoted scalar has no escapes but '' for a quote; 7.3.1: a double-quoted one escapes the
// characters that cannot appear in it as they are.
std::string quoted(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 2);
    if (std::ranges::none_of(text, isControl)) {
        out += '\'';
        for (const char c : text) {
            out += c;
            if (c == '\'') {
                out += '\'';
            }
        }
        out += '\'';
        return out;
    }
    out += '"';
    for (const char c : text) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\t':
            out += "\\t";
            break;
        case '\n':
            out += "\\n";
            break;
        default:
            if (isControl(c)) {
                std::format_to(std::back_inserter(out), "\\x{:02X}",
                               static_cast<unsigned>(static_cast<unsigned char>(c)));
            } else {
                out += c;
            }
        }
    }
    out += '"';
    return out;
}

std::string_view nameOf(WaypointType type) {
    switch (type) {
    case WaypointType::Assumption:
        return "assumption";
    case WaypointType::Branching:
        return "branching";
    case WaypointType::FunctionEnter:
        return "function_enter";
    case WaypointType::FunctionReturn:
        return "function_return";
    case WaypointType::Target:
        return "target";
    }
    std::unreachable();
}

std::string_view nameOf(WaypointAction action) {
    switch (action) {
    case WaypointAction::Follow:
        return "follow";
    case WaypointAction::Avoid:
        return "avoid";
    case WaypointAction::Cycle:
        return "cycle";
    }
    std::unreachable();
}

/// `key: 'value'` on its own line at `indent`, if the value is not empty.
void writeText(std::string& out, std::size_t indent, std::string_view key, std::string_view value) {
    if (!value.empty()) {
        std::format_to(std::back_inserter(out), "{:{}}{}: {}\n", "", indent, key, quoted(value));
    }
}

void writeMetadata(std::string& out, const Metadata& metadata, const Provenance& provenance) {
    out += "  metadata:\n";
    writeText(out, 4, "format_version", metadata.formatVersion);
    writeText(out, 4, "uuid", provenance.uuid);
    writeText(out, 4, "creation_time", provenance.creationTime);
    out += "    producer:\n";
    writeText(out, 6, "name", metadata.producer.name);
    writeText(out, 6, "version", metadata.producer.version);
    const Task& task = metadata.task;
    out += "    task:\n";
    if (!task.inputFiles.empty()) {
        out += "      input_files:\n";
        for (const std::string& file : task.inputFiles) {
            std::format_to(std::back_inserter(out), "      - {}\n", quoted(file));
        }
    }
    if (!provenance.inputFileHashes.empty()) {
        out += "      input_file_hashes:\n";
        for (std::size_t i = 0; i < task.inputFiles.size() && i < provenance.inputFileHashes.size(); ++i) {
            std::format_to(std::back_inserter(out), "        {}: {}\n", quoted(task.inputFiles[i]),
                           quoted(provenance.inputFileHashes[i]));
        }
    }
    writeText(out, 6, "specification", task.specification);
    writeText(out, 6, "data_model", task.dataModel);
    writeText(out, 6, "language", task.language);
}

// Location and constraint in flow style, one line each: a long witness has one per input of the run.
void writeWaypoint(std::string& out, const Waypoint& waypoint) {
    std::format_to(std::back_inserter(out), "    - waypoint:\n        type: {}\n        action: {}\n",
                   nameOf(waypoint.type), nameOf(waypoint.action));
    const Location& location = waypoint.location;
    std::format_to(std::back_inserter(out), "        location: {{file_name: {}, line: {}", quoted(location.fileName),
                   location.line);
    if (location.column) {
        std::format_to(std::back_inserter(out), ", column: {}", *location.column);
    }
    if (!location.function.empty()) {
        std::format_to(std::back_inserter(out), ", function: {}", quoted(location.function));
    }
    out += "}\n";
    if (waypoint.constraint) {
        std::format_to(std::back_inserter(out), "        constraint: {{value: {}", quoted(waypoint.constraint->value));
        if (!waypoint.constraint->format.empty()) {
            std::format_to(std::back_inserter(out), ", format: {}", quoted(waypoint.constraint->format));
        }
        out += "}\n";
    }
    if (waypoint.threadId) {
        std::format_to(std::back_inserter(out), "        thread_id: {}\n", *waypoint.threadId);
    }
}

} // namespace

std::string writeYamlWitness(const ViolationSequence& sequence, const Provenance& provenance) {
    std::string out = "- entry_type: violation_sequence\n";
    writeMetadata(out, sequence.metadata, provenance);
    out += sequence.segments.empty() ? "  content: []\n" : "  content:\n";
    for (const Segment& segment : sequence.segments) {
        out += "  - segment:\n";
        for (const Waypoint& waypoint : segment.waypoints) {
            writeWaypoint(out, waypoint);
        }
    }
    return out;
}

} // namespace nitwit::witness
