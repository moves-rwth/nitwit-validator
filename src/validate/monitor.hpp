#pragma once

// The monitor of a format 2.x witness (validate/README.md, "Follow, avoid and progress"): it follows the execution
// through the segments of the violation sequence, evaluates the waypoints of the current segment at their evaluation
// points, gives the oracle the hints of function_return waypoints, and stops the run once its outcome is decided. A
// waypoint without the outcome the witness needs asks the search for inputs that give it ("Search"), and so does a
// guard of the program that would end the run ("Guards").

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
#include "validate/search.hpp"
#include "validate/validator.hpp"
#include "witness/model.hpp"

namespace nitwit::validate {

class SegmentMonitor final : public interp::ExecutionObserver {
public:
    enum class State : std::uint8_t {
        Following,   // the run goes on
        Rejected,    // a follow waypoint failed or an avoid waypoint passed
        ErrorCalled, // the error function was called, see isAtTarget()
        Restart,     // a follow waypoint failed or an avoid waypoint passed; the inputs of changes() may change that
        Unsupported, // a constraint cannot be compiled, or does not fit its waypoint
        Unknown,     // an avoid constraint has no value (undefined behaviour, an invalid read)
    };

    /// `witness` must have passed unsupportedFeature(). The monitor keeps references to all arguments. A run keeps
    /// clauses with at most `keptAtoms` atoms for the search.
    SegmentMonitor(const witness::ViolationSequence& witness, const ProgramInput& program,
                   const std::string& errorFunction, ConstraintCompiler& constraints, WitnessOracle& oracle,
                   std::ostream* log, std::size_t keptAtoms);

    /// Why the witness cannot be followed in this program, found when the monitor was built (validate/README.md,
    /// "Evaluation points"): a waypoint other than the target without evaluation point of its type, or in another
    /// function than its location.function; a #line directive in a program that went through the preprocessor.
    [[nodiscard]] const std::optional<std::string>& inconsistency() const { return inconsistency_; }

    /// Starts following the witness from its first segment; `run` numbers the runs for the log. The run stops as
    /// Unknown after `eventBudget` events (the statements, conditions, calls and returns the interpreter reports,
    /// interp/observer.hpp); none for no limit.
    void beginRun(std::uint32_t run, std::optional<std::uint64_t> eventBudget = std::nullopt);
    /// The number of the current run, as passed to beginRun().
    [[nodiscard]] std::uint32_t run() const { return run_; }
    /// The events of the current run so far.
    [[nodiscard]] std::uint64_t events() const { return events_; }

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
    /// For State::Restart: the new values of inputs, at most one per input.
    [[nodiscard]] const std::vector<Change>& changes() const { return changes_; }
    /// For State::Restart: whether a guard of the program asked for it (validate/README.md, "Guards"), not a waypoint.
    [[nodiscard]] bool isAtGuard() const { return isAtGuard_; }
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
        // What the waypoint needs ("Search"): a follow waypoint to pass, an avoid waypoint to fail.
        std::vector<Clause> requirement;
        std::string reason; // why it fails, has no value (Undefined: an avoid constraint), or what is unsupported
    };

    /// The first reason why the waypoints cannot be followed in this program; for the constructor.
    [[nodiscard]] std::optional<std::string> findInconsistency() const;

    interp::Directive handle(const Event& event, interp::EvaluationContext& context);
    Evaluation evaluate(WaypointId id, const Event& event, interp::EvaluationContext& context, bool isFollow);
    Evaluation evaluateConstraint(WaypointId id, const Event& event, interp::EvaluationContext& context, bool isFollow,
                                  ast::QualType resultType);
    Evaluation evaluateBranching(WaypointId id, const Event& event, interp::EvaluationContext& context, bool isFollow);
    /// The requirement of a branching at `default` of a switch; `passes`: whether the switch took it.
    [[nodiscard]] std::vector<Clause> defaultRequirement(const interp::ConditionEvent& condition, bool isFollow,
                                                         bool passes) const;
    /// What taking the case value `caseValue` of `label` (if any; `t` is the switched value's term) needs, or with
    /// `isTaken` false, not taking it.
    [[nodiscard]] static std::vector<Clause>
    caseRequirement(std::optional<value::TermId> term, const ast::CaseStmt* label, Int128 caseValue, bool isTaken);
    /// Stops the run after waypoint `id` did not have the outcome the witness needs (`reason`): Restart with the inputs
    /// the search finds for `requirement`, else Rejected, with `rejection` as the reason if it is not empty.
    interp::Directive searchOrReject(WaypointId id, const std::vector<Clause>& requirement,
                                     interp::EvaluationContext& context, std::string reason,
                                     std::string rejection = {});
    /// At a guard whose requirement is `atom` (validate/README.md, "Guards"): keeps it if the run continues past the
    /// guard (`continues`), else Restart with the inputs the search finds for it; without any, the run goes on to the
    /// end the guard gives it. `location` and `function` (the function that ends the run) describe the guard.
    interp::Directive passGuard(const Atom& atom, bool continues, SourceLocation location, std::string_view function,
                                interp::EvaluationContext& context);
    /// Keeps the clauses of a requirement that has its outcome, for the searches later in the run.
    void keep(const std::vector<Clause>& requirement, const value::TermTable& terms);
    /// Keeps one clause of such a requirement.
    void keep(std::span<const Atom> clause, const value::TermTable& terms);
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
    std::optional<std::uint64_t> eventBudget_;
    std::uint64_t events_ = 0;
    std::uint32_t current_ = 0;
    bool isAtTarget_ = false;
    State state_ = State::Following;
    std::string reason_;
    std::vector<Change> changes_;
    bool isAtGuard_ = false;
    KeptClauses kept_; // of the waypoints that had their outcome in this run
    bool isKeptFull_ = false;
};

/// The name of a waypoint type as the witness format spells it.
[[nodiscard]] std::string_view nameOf(witness::WaypointType type);

} // namespace nitwit::validate
