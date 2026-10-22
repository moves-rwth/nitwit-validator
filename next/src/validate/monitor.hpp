#pragma once

// The monitor of a format 2.x witness (validate/README.md, "Follow, avoid and progress"): it follows the execution
// through the segments of the violation sequence, evaluates the waypoints of the current segment at their evaluation
// points, gives the oracle the hints of function_return waypoints, and stops the run once its outcome is decided.

#include <cstdint>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "interp/observer.hpp"
#include "validate/constraints.hpp"
#include "validate/location.hpp"
#include "validate/oracle.hpp"
#include "validate/validator.hpp"
#include "validate/values.hpp"
#include "witness/model.hpp"

namespace nitwit::validate {

/// An input of the current run to fix for the next run, so that a failed follow waypoint passes or a passing avoid
/// waypoint fails.
struct Fix {
    value::InputId input{};
    value::Value value; // of the input's type
};

class SegmentMonitor final : public interp::ExecutionObserver {
public:
    enum class State : std::uint8_t {
        Following,   // the run goes on
        Rejected,    // a follow waypoint failed or an avoid waypoint passed
        ErrorCalled, // the error function was called, see isAtTarget()
        Restart,     // a follow waypoint failed or an avoid waypoint passed; fixing fixes() may change that
        Unsupported, // a constraint cannot be compiled, or does not fit its waypoint
        Unknown,     // an avoid constraint has no value (undefined behaviour, an invalid read)
    };

    /// `witness` must have passed unsupportedFeature(). The monitor keeps references to all arguments.
    SegmentMonitor(const witness::ViolationSequence& witness, const ProgramInput& program,
                   const std::string& errorFunction, ConstraintCompiler& constraints, WitnessOracle& oracle,
                   std::ostream* log);

    /// Why the witness cannot be followed in this program, found when the monitor was built (validate/README.md,
    /// "Evaluation points"): a waypoint other than the target without evaluation point of its type, or in another
    /// function than its location.function; a #line directive in a program that went through the preprocessor.
    [[nodiscard]] const std::optional<std::string>& inconsistency() const { return inconsistency_; }

    /// Starts following the witness from its first segment; `run` numbers the runs for the log.
    void beginRun(std::uint32_t run);

    interp::Directive beforeStatement(const interp::StatementEvent& event, interp::EvaluationContext& context) override;
    interp::Directive afterCondition(const interp::ConditionEvent& event, interp::EvaluationContext& context) override;
    interp::Directive afterArguments(const interp::CallEvent& event, interp::EvaluationContext& context) override;
    interp::Directive afterReturn(const interp::ReturnEvent& event, interp::EvaluationContext& context) override;
    interp::Directive onUndefinedBehavior(value::UndefinedBehavior kind, const ast::Expr* expr,
                                          interp::EvaluationContext& context) override;

    [[nodiscard]] State state() const { return state_; }
    /// Why the state is not Following.
    [[nodiscard]] const std::string& reason() const { return reason_; }
    /// The number of segments whose follow waypoint passed in this run; the index of the current segment.
    [[nodiscard]] std::uint32_t segmentsPassed() const { return current_; }
    [[nodiscard]] std::uint32_t segmentCount() const { return static_cast<std::uint32_t>(segments_.size()); }
    [[nodiscard]] bool isTargetSegmentCurrent() const { return current_ + 1 == segmentCount(); }
    /// For State::ErrorCalled: whether the call was at the target's location in the target segment.
    [[nodiscard]] bool isAtTarget() const { return isAtTarget_; }
    /// For State::Restart: the inputs to fix, at most one fix per input.
    [[nodiscard]] const std::vector<Fix>& fixes() const { return fixes_; }
    [[nodiscard]] const LocationMap& locations() const { return locations_; }

private:
    /// An evaluation point: the waypoint type evaluated there and the location the witness refers to.
    struct Event {
        witness::WaypointType type = witness::WaypointType::Assumption;
        SourceLocation location;
        const interp::ConditionEvent* condition = nullptr; // for Branching
        const ast::CallExpr* call = nullptr;               // for FunctionEnter and FunctionReturn
    };

    struct Evaluation {
        enum class Outcome : std::uint8_t { Passes, Fails, Unsupported, Undefined };

        Outcome outcome = Outcome::Passes;
        // A failing follow waypoint: the fixes that may make it pass. A passing avoid waypoint: a fix that may make it
        // fail.
        std::vector<Fix> fixes;
        std::string reason; // why it fails, has no value (Undefined: an avoid constraint), or what is unsupported
    };

    /// The first reason why the waypoints cannot be followed in this program; for the constructor.
    [[nodiscard]] std::optional<std::string> findInconsistency() const;

    interp::Directive handle(const Event& event, interp::EvaluationContext& context);
    Evaluation evaluate(WaypointId id, const Event& event, interp::EvaluationContext& context, bool isFollow);
    Evaluation evaluateConstraint(WaypointId id, const Event& event, interp::EvaluationContext& context, bool isFollow,
                                  ast::QualType resultType);
    Evaluation evaluateBranching(WaypointId id, const Event& event, interp::EvaluationContext& context, bool isFollow);
    /// The fix that makes a conjunct true (`makeTrue`) or false: for `subject op constant`, a fix of the subject's
    /// origin for the relation or its negation; for another conjunct, whose value is `value`, a fix of the value's
    /// origin for `value != 0` or `value == 0`. None if that origin is no unfixed input.
    std::optional<Fix> resolve(const Conjunct& conjunct, const value::Value& value, bool makeTrue,
                               interp::EvaluationContext& context);
    /// The fix of the input that is the origin of `subject` (of type `subjectType`), so that the subject satisfies the
    /// bound; none if the origin is no unfixed input or no value of the input's type gives such a subject.
    std::optional<Fix> fixFor(const value::Value& subject, ast::QualType subjectType, const Bound& bound);
    /// For a switch event: the values of the switched value that no avoid waypoint of the current segment at the
    /// switch avoids, in the order fixes try them.
    [[nodiscard]] std::vector<value::Value> unavoidedValues(const Event& event, interp::EvaluationContext& context);
    /// Whether a value's origin is an input that is neither fixed nor hinted.
    [[nodiscard]] bool isUnfixedInput(const value::Value& value) const;
    /// Hands the oracle the value that the function_return waypoints of the current segment at this call ask for.
    void offerHint(const interp::CallEvent& event, interp::EvaluationContext& context);

    /// The waypoints of a segment that an event of this type at this location evaluates, in their order in the
    /// segment (the follow waypoint last).
    [[nodiscard]] std::span<const std::uint32_t> waypointsAt(std::uint32_t segment, witness::WaypointType type,
                                                             SourceLocation location) const;
    interp::Directive stop(State state, std::string reason);
    void advance(const Event& event);
    [[nodiscard]] const witness::Waypoint& waypoint(WaypointId id) const;
    /// "the follow waypoint of segment 2 (branching at 6:5)", for messages.
    [[nodiscard]] std::string describe(WaypointId id) const;

    const std::vector<witness::Segment>& segments_;
    const ProgramInput& program_;
    const std::string& errorFunction_;
    ConstraintCompiler& constraints_;
    WitnessOracle& oracle_;
    std::ostream* log_;
    LocationMap locations_;
    // Per segment: the indices of its waypoints by event type and offset, so that an event costs one lookup also for
    // witnesses with many waypoints. Target waypoints are not evaluated by events and have their own place.
    std::vector<std::unordered_map<std::uint64_t, std::vector<std::uint32_t>>> waypointIndex_;
    std::vector<std::vector<Place>> places_; // per segment and waypoint
    Place target_;
    std::optional<std::string> inconsistency_;

    std::uint32_t run_ = 0;
    std::uint32_t current_ = 0;
    bool isAtTarget_ = false;
    State state_ = State::Following;
    std::string reason_;
    std::vector<Fix> fixes_;
};

/// The name of a waypoint type as the witness format spells it.
[[nodiscard]] std::string_view nameOf(witness::WaypointType type);

} // namespace nitwit::validate
