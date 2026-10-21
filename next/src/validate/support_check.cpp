#include "validate/support_check.hpp"

#include <cstddef>
#include <format>

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
