#include "validate/monitor.hpp"

#include <algorithm>
#include <format>
#include <print>
#include <utility>

#include "ast/casting.hpp"
#include "ast/stmt.hpp"
#include "support/assert.hpp"
#include "validate/values.hpp"

namespace nitwit::validate {

namespace {

bool isTrue(const value::Value& value) {
    return value::toBool(value).bits() != 0;
}

/// The case label of a switch that `value` takes, if any.
const ast::CaseStmt* labelOf(const ast::SwitchStmt& stmt, Int128 value) {
    for (const ast::CaseStmt* label : stmt.cases()) {
        if (label->low() <= value && value <= label->high()) {
            return label;
        }
    }
    return nullptr;
}

std::uint64_t keyOf(witness::WaypointType type, std::uint32_t offset) {
    return (static_cast<std::uint64_t>(type) << 32) | offset;
}

std::string_view nameOf(interp::ConditionEvent::Construct construct) {
    switch (construct) {
    case interp::ConditionEvent::Construct::If:
        return "if";
    case interp::ConditionEvent::Construct::While:
    case interp::ConditionEvent::Construct::DoWhile:
        return "while";
    case interp::ConditionEvent::Construct::For:
        return "for";
    case interp::ConditionEvent::Construct::Conditional:
        return "?:";
    case interp::ConditionEvent::Construct::Switch:
        return "switch";
    }
    std::unreachable();
}

} // namespace

std::string_view nameOf(witness::WaypointType type) {
    switch (type) {
    case witness::WaypointType::Assumption:
        return "assumption";
    case witness::WaypointType::Branching:
        return "branching";
    case witness::WaypointType::FunctionEnter:
        return "function_enter";
    case witness::WaypointType::FunctionReturn:
        return "function_return";
    case witness::WaypointType::Target:
        return "target";
    }
    std::unreachable();
}

SegmentMonitor::SegmentMonitor(const witness::ViolationSequence& witness, const ProgramInput& program,
                               const std::string& errorFunction, ConstraintCompiler& constraints, WitnessOracle& oracle,
                               std::ostream* log, std::size_t keptAtoms)
    : segments_(witness.segments), program_(program), errorFunction_(errorFunction), constraints_(constraints),
      oracle_(oracle), log_(log), locations_(program.sources, program.file, program.originalFile, program.unit),
      kept_(keptAtoms) {
    NITWIT_ASSERT(!segments_.empty(), "a violation sequence without segments");
    for (const witness::Segment& segment : segments_) {
        std::unordered_map<std::uint64_t, std::vector<std::uint32_t>>& index = waypointIndex_.emplace_back();
        std::vector<Place>& places = places_.emplace_back();
        for (std::uint32_t i = 0; i < segment.waypoints.size(); ++i) {
            const witness::Waypoint& point = segment.waypoints[i];
            const Place& place = places.emplace_back(locations_.place(point.location, point.type));
            if (point.type == witness::WaypointType::Target) {
                target_ = place; // unsupportedFeature(): only the last waypoint of the last segment is a target
                continue;
            }
            for (const std::uint32_t offset : place.offsets()) {
                std::vector<std::uint32_t>& here = index[keyOf(point.type, offset)];
                if (here.empty() || here.back() != i) {
                    here.push_back(i);
                }
            }
        }
    }
    inconsistency_ = findInconsistency();
}

// validate/README.md, "Evaluation points": a witness whose locations do not fit the program is invalid. A waypoint
// that matched nothing would never be evaluated, which for an avoid waypoint would confirm what the witness excludes.
std::optional<std::string> SegmentMonitor::findInconsistency() const {
    if (const std::optional<std::uint32_t> line = locations_.lineDirective()) {
        return std::format("a #line directive (line {}) in a program that needs the preprocessor: its presumed lines "
                           "are not the lines of the witness",
                           *line);
    }
    for (std::uint32_t s = 0; s < segments_.size(); ++s) {
        for (std::uint32_t i = 0; i < segments_[s].waypoints.size(); ++i) {
            const WaypointId id{.segment = s, .index = i};
            const witness::Waypoint& point = waypoint(id);
            const Place& place = places_[s][i];
            if (point.type == witness::WaypointType::Target) {
                continue;
            }
            if (place.offsets().empty()) {
                return std::format("{} is at no evaluation point of its type{}", describe(id),
                                   place.isAfterPreprocessorChange()
                                       ? ": its column lies after text that the preprocessor changed"
                                       : "");
            }
            const std::string& function = point.location.function;
            for (const ast::FunctionDecl* containing : place.functions()) {
                if (!function.empty() && containing->name().text() != function) {
                    return std::format("{} is in function '{}', not in '{}' as the witness says", describe(id),
                                       containing->name().text(), function);
                }
            }
        }
    }
    return std::nullopt;
}

void SegmentMonitor::beginRun(std::uint32_t run, std::uint64_t eventBudget) {
    events_ = 0;
    eventBudget_ = eventBudget;
    run_ = run;
    current_ = 0;
    isAtTarget_ = false;
    state_ = State::Following;
    reason_.clear();
    changes_.clear();
    kept_.clear();
    isKeptFull_ = false;
}

interp::Directive SegmentMonitor::beforeStatement(const interp::StatementEvent& event,
                                                  interp::EvaluationContext& context) {
    return handle({.type = witness::WaypointType::Assumption,
                   .location = event.stmt->location(),
                   .condition = nullptr,
                   .call = nullptr},
                  context);
}

interp::Directive SegmentMonitor::afterCondition(const interp::ConditionEvent& event,
                                                 interp::EvaluationContext& context) {
    return handle(
        {.type = witness::WaypointType::Branching, .location = event.location, .condition = &event, .call = nullptr},
        context);
}

interp::Directive SegmentMonitor::afterArguments(const interp::CallEvent& event, interp::EvaluationContext& context) {
    oracle_.clearHint();
    const Event call{.type = witness::WaypointType::FunctionEnter,
                     .location = event.call->rightParenLocation(),
                     .condition = nullptr,
                     .call = event.call};
    if (handle(call, context) == interp::Directive::Stop) {
        return interp::Directive::Stop;
    }
    // The target waypoint passes when the error function is called at its location (the start of the call, where
    // producers put it) while the target segment is current.
    if (event.callee != nullptr && event.callee->name().text() == errorFunction_) {
        const SourceLocation start = event.call->location();
        isAtTarget_ = isTargetSegmentCurrent() && target_.contains(start);
        const bool isElsewhere = isTargetSegmentCurrent() && !isAtTarget_;
        return stop(State::ErrorCalled, std::format("'{}' was called at {} in segment {} of {}{}", errorFunction_,
                                                    locations_.describe(start), current_ + 1, segmentCount(),
                                                    isElsewhere ? ", not at the target" : ""));
    }
    offerHint(event, context);
    return state_ == State::Following ? interp::Directive::Continue : interp::Directive::Stop;
}

interp::Directive SegmentMonitor::afterReturn(const interp::ReturnEvent& event, interp::EvaluationContext& context) {
    oracle_.clearHint();
    return handle({.type = witness::WaypointType::FunctionReturn,
                   .location = event.call->rightParenLocation(),
                   .condition = nullptr,
                   .call = event.call},
                  context);
}

interp::Directive SegmentMonitor::onUndefinedBehavior(value::UndefinedBehavior, const ast::Expr*,
                                                      interp::EvaluationContext&) {
    return interp::Directive::Continue; // the interpreter continues like a native run; the witness is not concerned
}

// validate/README.md, "Follow, avoid and progress": only the waypoints of the current segment are evaluated, the
// follow waypoint first. It must pass at its first evaluation point; if it fails, the search may find inputs with which
// it passes, and the run restarts. Then the avoid waypoints are evaluated: one that passes rejects the execution,
// unless the search finds inputs with which it fails. A waypoint that has its outcome keeps its requirement for the
// searches later in the run. A passing follow waypoint makes the next segment current. Cascading: after an assumption,
// the waypoints of that segment are evaluated at the same event, for the same execution of the statement.
interp::Directive SegmentMonitor::handle(const Event& event, interp::EvaluationContext& context) {
    if (state_ != State::Following) {
        return interp::Directive::Stop;
    }
    if (++events_ > eventBudget_ && eventBudget_ != 0) {
        return stop(State::Unknown, std::format("the run exceeded its budget of {} events", eventBudget_));
    }
    while (true) {
        const std::span<const std::uint32_t> here = waypointsAt(current_, event.type, event.location);
        if (here.empty()) {
            return interp::Directive::Continue;
        }
        const auto followIndex = static_cast<std::uint32_t>(segments_[current_].waypoints.size() - 1);
        const bool isFollowHere = here.back() == followIndex;
        if (isFollowHere) {
            const WaypointId followId{.segment = current_, .index = followIndex};
            Evaluation evaluation = evaluate(followId, event, context, true);
            switch (evaluation.outcome) {
            case Evaluation::Outcome::Unsupported:
                return stop(State::Unsupported, std::format("{}: {}", describe(followId), evaluation.reason));
            case Evaluation::Outcome::Fails:
                return searchOrReject(
                    followId, evaluation.requirement, context,
                    std::format("{} failed at {}", describe(followId), locations_.describe(event.location)),
                    std::format("{} did not pass at its first evaluation point, {}{}", describe(followId),
                                locations_.describe(event.location),
                                evaluation.reason.empty() ? "" : ": " + evaluation.reason));
            case Evaluation::Outcome::Undefined:
                NITWIT_UNREACHABLE("a follow constraint without value fails");
            case Evaluation::Outcome::Passes:
                keep(evaluation.requirement, context.terms());
                break;
            }
        }
        for (const std::uint32_t index : here.first(here.size() - (isFollowHere ? 1 : 0))) {
            const WaypointId id{.segment = current_, .index = index};
            Evaluation avoid = evaluate(id, event, context, false);
            switch (avoid.outcome) {
            case Evaluation::Outcome::Unsupported:
                return stop(State::Unsupported, std::format("{}: {}", describe(id), avoid.reason));
            case Evaluation::Outcome::Undefined:
                return stop(State::Unknown, std::format("{} has no value at {}: {}", describe(id),
                                                        locations_.describe(event.location), avoid.reason));
            case Evaluation::Outcome::Passes:
                return searchOrReject(
                    id, avoid.requirement, context,
                    std::format("{} passed at {}", describe(id), locations_.describe(event.location)));
            case Evaluation::Outcome::Fails:
                keep(avoid.requirement, context.terms());
                break;
            }
        }
        if (!isFollowHere) {
            return interp::Directive::Continue;
        }
        advance(event);
        if (event.type != witness::WaypointType::Assumption) {
            return interp::Directive::Continue;
        }
    }
}

SegmentMonitor::Evaluation SegmentMonitor::evaluate(WaypointId id, const Event& event,
                                                    interp::EvaluationContext& context, bool isFollow) {
    const witness::Waypoint& point = waypoint(id);
    switch (point.type) {
    case witness::WaypointType::Assumption:
        if (!point.constraint) {
            return {.outcome = Evaluation::Outcome::Unsupported, .requirement = {}, .reason = "it has no constraint"};
        }
        return evaluateConstraint(id, event, context, isFollow, ast::QualType());
    case witness::WaypointType::Branching:
        return evaluateBranching(id, event, context, isFollow);
    case witness::WaypointType::FunctionEnter:
        return {};
    case witness::WaypointType::FunctionReturn:
        if (!point.constraint) {
            return {};
        }
        return evaluateConstraint(id, event, context, isFollow, event.call->type());
    case witness::WaypointType::Target:
        break;
    }
    NITWIT_UNREACHABLE("target waypoints are never evaluated");
}

// A constraint passes when all its conjuncts hold, evaluated in the current state (validate/README.md, "Values"). A
// follow constraint needs `c != 0` of each conjunct c, a clause each; an avoid constraint needs `c == 0` of one of
// them, evaluated like `&&` up to the first false one ("Search").
SegmentMonitor::Evaluation SegmentMonitor::evaluateConstraint(WaypointId id, const Event& event,
                                                              interp::EvaluationContext& context, bool isFollow,
                                                              ast::QualType resultType) {
    const std::expected<const CompiledConstraint*, std::string> compiled =
        constraints_.compile(id, waypoint(id).constraint->value, context.function(), event.location, resultType);
    if (!compiled) {
        return {.outcome = Evaluation::Outcome::Unsupported, .requirement = {}, .reason = compiled.error()};
    }
    Evaluation evaluation;
    Clause someConjunctFails;
    for (const Conjunct& conjunct : (*compiled)->conjuncts) {
        const std::expected<value::Value, std::string> value = context.evaluate(conjunct.expr);
        if (!value && !isFollow) {
            // `&&` evaluates this conjunct, since all before it hold: the constraint has no value.
            return {.outcome = Evaluation::Outcome::Undefined, .requirement = {}, .reason = value.error()};
        }
        const std::optional<value::TermId> term = value ? value->term() : std::nullopt;
        if (isFollow) {
            evaluation.requirement.push_back({{.term = term, .op = value::Comparison::NotEqual, .constant = 0}});
        } else {
            someConjunctFails.push_back({.term = term, .op = value::Comparison::Equal, .constant = 0});
        }
        if (!value) {
            evaluation.outcome = Evaluation::Outcome::Fails;
            evaluation.reason = std::format("the constraint cannot be evaluated: {}", value.error());
            continue;
        }
        if (!isTrue(*value)) {
            evaluation.outcome = Evaluation::Outcome::Fails;
            if (!isFollow) {
                break; // an avoid constraint with a false conjunct does not pass
            }
        }
    }
    if (!isFollow) {
        evaluation.requirement.push_back(std::move(someConjunctFails));
    }
    return evaluation;
}

// Witness format 2.x, "branching": the outcome of the condition, or for a switch the case value or default.
SegmentMonitor::Evaluation SegmentMonitor::evaluateBranching(WaypointId id, const Event& event,
                                                             interp::EvaluationContext& context, bool isFollow) {
    const witness::Waypoint& point = waypoint(id);
    if (!point.constraint) {
        return {.outcome = Evaluation::Outcome::Unsupported, .requirement = {}, .reason = "it has no constraint"};
    }
    const std::expected<BranchExpectation, std::string> expectation =
        constraints_.branch(id, point.constraint->value, context.function(), event.location);
    if (!expectation) {
        return {.outcome = Evaluation::Outcome::Unsupported, .requirement = {}, .reason = expectation.error()};
    }
    const interp::ConditionEvent& condition = *event.condition;
    const bool isSwitch = condition.construct == interp::ConditionEvent::Construct::Switch;
    const bool isOutcome =
        expectation->kind == BranchExpectation::Kind::True || expectation->kind == BranchExpectation::Kind::False;
    if (isSwitch == isOutcome) {
        return {.outcome = Evaluation::Outcome::Unsupported,
                .requirement = {},
                .reason = std::format("the constraint '{}' does not fit a branching at '{}'", point.constraint->value,
                                      nameOf(condition.construct))};
    }
    bool passes = false;
    std::vector<Clause> requirement;
    if (isOutcome) {
        const bool isTrueExpected = expectation->kind == BranchExpectation::Kind::True;
        passes = condition.outcome == isTrueExpected;
        // A follow waypoint needs its outcome, an avoid waypoint the other one.
        const bool needsTrue = isTrueExpected == isFollow;
        requirement.push_back({{.term = condition.term,
                                .op = needsTrue ? value::Comparison::NotEqual : value::Comparison::Equal,
                                .constant = 0}});
    } else if (expectation->kind == BranchExpectation::Kind::Default) {
        passes = condition.switchLabel == nullptr || ast::isa<ast::DefaultStmt>(condition.switchLabel);
        requirement = defaultRequirement(condition, isFollow, passes);
    } else {
        // A case value in a GNU range `case lo ... hi:` takes the range's label, like any value in it.
        const auto* switchStmt = ast::cast<ast::SwitchStmt>(condition.stmt);
        const value::Value caseValue =
            valueOf(program_.context, expectation->caseValue, switchStmt->condition()->type());
        const ast::CaseStmt* label = labelOf(*switchStmt, caseValue.signedBits());
        passes = label != nullptr ? condition.switchLabel == label : caseValue.bits() == condition.switchValue.bits();
        requirement = caseRequirement(condition.term, label, caseValue.signedBits(), isFollow);
    }
    return {.outcome = passes ? Evaluation::Outcome::Passes : Evaluation::Outcome::Fails,
            .requirement = std::move(requirement),
            .reason = {}};
}

// validate/README.md, "Search": the default branch needs `t != v` for every case label (`t < lo || t > hi` for a
// range); avoided, `t == v` for one of the labels, and while it is avoided, the label the run took.
std::vector<Clause> SegmentMonitor::defaultRequirement(const interp::ConditionEvent& condition, bool isFollow,
                                                       bool passes) const {
    const auto* switchStmt = ast::cast<ast::SwitchStmt>(condition.stmt);
    if (!isFollow && !passes) {
        const auto* label = ast::cast<ast::CaseStmt>(condition.switchLabel);
        return caseRequirement(condition.term, label, label->low(), true);
    }
    std::vector<Clause> requirement;
    Clause someCase;
    for (const ast::CaseStmt* label : switchStmt->cases()) {
        if (!isFollow) {
            someCase.push_back({.term = condition.term, .op = value::Comparison::Equal, .constant = label->low()});
        } else {
            requirement.push_back(caseRequirement(condition.term, label, label->low(), false).front());
        }
    }
    if (!isFollow) {
        requirement.push_back(std::move(someCase));
    }
    return requirement;
}

// A case value v needs `t == v`, avoided `t != v`; in a range, `t >= lo` and `t <= hi`, avoided `t < lo || t > hi`.
std::vector<Clause> SegmentMonitor::caseRequirement(std::optional<value::TermId> term, const ast::CaseStmt* label,
                                                    Int128 caseValue, bool isTaken) {
    if (label == nullptr || label->low() == label->high()) {
        return {{{.term = term,
                  .op = isTaken ? value::Comparison::Equal : value::Comparison::NotEqual,
                  .constant = caseValue}}};
    }
    if (isTaken) {
        return {{{.term = term, .op = value::Comparison::GreaterEqual, .constant = label->low()}},
                {{.term = term, .op = value::Comparison::LessEqual, .constant = label->high()}}};
    }
    // Leaving the range gives lo - 1 or hi + 1: the non-negative one first, as with candidates.
    const Atom below{.term = term, .op = value::Comparison::Less, .constant = label->low()};
    const Atom above{.term = term, .op = value::Comparison::Greater, .constant = label->high()};
    return {label->low() > 0 ? Clause{below, above} : Clause{above, below}};
}

interp::Directive SegmentMonitor::searchOrReject(WaypointId id, const std::vector<Clause>& requirement,
                                                 interp::EvaluationContext& context, std::string reason,
                                                 std::string rejection) {
    std::vector<SearchInput> inputs;
    inputs.reserve(oracle_.inputCount());
    for (std::uint32_t i = 0; i < oracle_.inputCount(); ++i) {
        const ProvidedInput& input = oracle_.input(value::InputId{i});
        inputs.push_back({.value = input.value,
                          .type = input.type,
                          .source = input.source,
                          .history = oracle_.fixedValues(input.key)});
    }
    SearchResult result = search(program_.context, context.terms(), requirement, kept_, inputs);
    const bool isRestart = !result.changes.empty();
    stop(isRestart ? State::Restart : State::Rejected,
         isRestart || rejection.empty() ? std::move(reason) : std::move(rejection));
    if (log_ != nullptr) {
        std::println(*log_, "run {}: {} {}", run_, describe(id), result.explanation);
    }
    changes_ = std::move(result.changes);
    return interp::Directive::Stop;
}

void SegmentMonitor::keep(const std::vector<Clause>& requirement, const value::TermTable& terms) {
    for (const Clause& clause : requirement) {
        if (kept_.keep(terms, clause) == KeptClauses::Outcome::Full && !isKeptFull_) {
            isKeptFull_ = true;
            if (log_ != nullptr) {
                std::println(*log_, "run {}: the kept clauses reach the limit of their atoms; no more in this run",
                             run_);
            }
        }
    }
}

// validate/README.md, "Values": `\result op constant` of a function_return waypoint at this call. A follow waypoint
// asks for a value that satisfies all its relations on `\result`, an avoid waypoint for one that violates one of them.
// The follow waypoint's hint wins.
void SegmentMonitor::offerHint(const interp::CallEvent& event, interp::EvaluationContext& context) {
    const SourceLocation location = event.call->rightParenLocation();
    std::optional<value::Value> followHint;
    std::optional<value::Value> avoidHint;
    for (const std::uint32_t i : waypointsAt(current_, witness::WaypointType::FunctionReturn, location)) {
        const WaypointId id{.segment = current_, .index = i};
        const witness::Waypoint& point = waypoint(id);
        if (!point.constraint) {
            continue;
        }
        const std::expected<const CompiledConstraint*, std::string> compiled =
            constraints_.compile(id, point.constraint->value, context.function(), location, event.call->type());
        if (!compiled) {
            stop(State::Unsupported, std::format("{}: {}", describe(id), compiled.error()));
            return;
        }
        std::vector<Bound> bounds;
        for (const Conjunct& conjunct : (*compiled)->conjuncts) {
            if (conjunct.subject == nullptr || !ast::isa<ast::ResultExpr>(conjunct.subject)) {
                continue;
            }
            if (const auto constant = context.evaluate(conjunct.constant)) {
                bounds.push_back({.op = conjunct.op, .constant = *constant, .type = conjunct.type});
            }
        }
        if (point.action == witness::WaypointAction::Follow) {
            if (!bounds.empty()) {
                followHint = choose(program_.context, bounds, event.call->type());
            }
            continue;
        }
        for (const Bound& bound : bounds) {
            if (avoidHint) {
                break;
            }
            const Bound violated{.op = negated(bound.op), .constant = bound.constant, .type = bound.type};
            avoidHint = choose(program_.context, std::span(&violated, 1), event.call->type());
        }
    }
    if (followHint) {
        oracle_.hint(event.call, std::move(*followHint));
    } else if (avoidHint) {
        oracle_.hint(event.call, std::move(*avoidHint));
    }
}

std::span<const std::uint32_t> SegmentMonitor::waypointsAt(std::uint32_t segment, witness::WaypointType type,
                                                           SourceLocation location) const {
    if (location.file != program_.file) {
        return {};
    }
    const auto& index = waypointIndex_[segment];
    const auto found = index.find(keyOf(type, location.offset));
    return found != index.end() ? std::span<const std::uint32_t>(found->second) : std::span<const std::uint32_t>();
}

interp::Directive SegmentMonitor::stop(State state, std::string reason) {
    state_ = state;
    reason_ = std::move(reason);
    if (log_ != nullptr) {
        std::println(*log_, "run {}: {}", run_, reason_);
    }
    return interp::Directive::Stop;
}

void SegmentMonitor::advance(const Event& event) {
    if (log_ != nullptr) {
        std::println(*log_, "run {}: segment {} of {} passed at {} ({})", run_, current_ + 1, segmentCount(),
                     locations_.describe(event.location), nameOf(event.type));
    }
    ++current_;
}

const witness::Waypoint& SegmentMonitor::waypoint(WaypointId id) const {
    return segments_[id.segment].waypoints[id.index];
}

std::string SegmentMonitor::describe(WaypointId id) const {
    const witness::Waypoint& point = waypoint(id);
    const bool isFollow = point.action == witness::WaypointAction::Follow;
    const std::string column = point.location.column ? std::format(":{}", *point.location.column) : "";
    return std::format("the {} waypoint of segment {} ({} at {}{})", isFollow ? "follow" : "avoid", id.segment + 1,
                       nameOf(point.type), point.location.line, column);
}

} // namespace nitwit::validate
