#include "validate/support_check.hpp"

#include <cstddef>
#include <format>
#include <string_view>

namespace nitwit::validate {

namespace {

// Witness format 2.x, "violation_sequence": each segment is a list of avoid waypoints ended by one follow waypoint, and
// the follow waypoint of the last segment is the target.
std::optional<std::string> unsupportedSegment(const witness::Segment& segment, std::size_t number, bool isLast) {
    std::size_t follows = 0;
    for (const witness::Waypoint& waypoint : segment.waypoints) {
        if (waypoint.action == witness::WaypointAction::Cycle) {
            return std::format("cycle waypoints (non-termination) in segment {}", number);
        }
        if (waypoint.threadId.value_or(0) != 0) {
            return std::format("waypoints of threads other than thread 0 in segment {}", number);
        }
        if (waypoint.type == witness::WaypointType::Target && waypoint.action == witness::WaypointAction::Avoid) {
            return std::format("a target waypoint with action avoid in segment {}", number);
        }
        if (waypoint.constraint && !isSupportedConstraintFormat(waypoint.constraint->format)) {
            return std::format("constraints of format '{}' in segment {}", waypoint.constraint->format, number);
        }
        // The format has no constraints for these types; no witness of SV-COMP 2026 writes one.
        if (waypoint.constraint
            && (waypoint.type == witness::WaypointType::FunctionEnter
                || waypoint.type == witness::WaypointType::Target)) {
            return std::format("a constraint on a {} waypoint, which the format does not allow, in segment {}",
                               waypoint.type == witness::WaypointType::Target ? "target" : "function_enter", number);
        }
        if (waypoint.action == witness::WaypointAction::Follow) {
            ++follows;
        }
    }
    if (follows != 1 || segment.waypoints.back().action != witness::WaypointAction::Follow) {
        return std::format("segment {}, which does not end with its only follow waypoint", number);
    }
    const bool endsWithTarget = segment.waypoints.back().type == witness::WaypointType::Target;
    if (endsWithTarget != isLast) {
        return isLast ? std::string("a last segment that does not end with the target waypoint")
                      : std::format("a target waypoint in segment {}, which is not the last segment", number);
    }
    return std::nullopt;
}

} // namespace

std::optional<std::string> waypointInOtherFile(const witness::ViolationSequence& sequence, std::string_view program) {
    const auto lastComponent = [](std::string_view path) {
        return path.substr(path.rfind('/') + 1);
    };
    const std::string_view name = lastComponent(program);
    for (std::size_t i = 0; i < sequence.segments.size(); ++i) {
        for (const witness::Waypoint& waypoint : sequence.segments[i].waypoints) {
            if (lastComponent(waypoint.location.fileName) != name) {
                return std::format("a waypoint in '{}', not in the program '{}' (segment {})",
                                   waypoint.location.fileName, name, i + 1);
            }
        }
    }
    return std::nullopt;
}

bool isSupportedConstraintFormat(const std::string& format) {
    return format.empty() || format == "c_expression" || format == "acsl_expression" || format == "ext_c_expression";
}

std::optional<std::string> unsupportedFeature(const witness::YamlWitness& witness) {
    if (witness.violationSequences.size() != 1) {
        return witness.violationSequences.empty() ? "witnesses without a violation_sequence entry"
                                                  : "witnesses with more than one violation_sequence entry";
    }
    const witness::ViolationSequence& sequence = witness.violationSequences.front();
    if (sequence.segments.empty()) {
        return "a violation sequence without segments";
    }
    for (std::size_t i = 0; i < sequence.segments.size(); ++i) {
        const witness::Segment& segment = sequence.segments[i];
        if (segment.waypoints.empty()) {
            return std::format("the empty segment {}", i + 1);
        }
        if (std::optional<std::string> reason = unsupportedSegment(segment, i + 1, i + 1 == sequence.segments.size())) {
            return reason;
        }
    }
    return std::nullopt;
}

} // namespace nitwit::validate
