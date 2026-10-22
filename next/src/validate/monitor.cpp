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
                               std::ostream* log)
    : segments_(witness.segments), program_(program), errorFunction_(errorFunction), constraints_(constraints),
      oracle_(oracle), log_(log), locations_(program.sources, program.file, program.originalFile, program.unit) {
    NITWIT_ASSERT(!segments_.empty(), "a violation sequence without segments");
    for (std::uint32_t s = 0; s < segments_.size(); ++s) {
        std::unordered_map<std::uint64_t, std::vector<std::uint32_t>>& index = waypointIndex_.emplace_back();
        const witness::Segment& segment = segments_[s];
        for (std::uint32_t i = 0; i < segment.waypoints.size(); ++i) {
            const witness::Waypoint& point = segment.waypoints[i];
            const Place place = locations_.place(point.location, point.type);
            if (place.isAfterPreprocessorChange() && log_ != nullptr) {
                std::println(*log_, "{} matches nothing in a line that the preprocessor changed before the column",
                             describe({.segment = s, .index = i}));
            }
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
}

void SegmentMonitor::beginRun(std::uint32_t run) {
    run_ = run;
    current_ = 0;
    isAtTarget_ = false;
    state_ = State::Following;
    reason_.clear();
    fixes_.clear();
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
// follow waypoint first. It must pass at its first evaluation point; a failure that fixes inputs restarts the run.
// Then the avoid waypoints are evaluated: one that passes rejects the execution, unless fixing an input may make it
// fail, which restarts the run. A passing follow waypoint makes the next segment current. Cascading: after an
// assumption, the waypoints of that segment are evaluated at the same event, for the same execution of the statement.
interp::Directive SegmentMonitor::handle(const Event& event, interp::EvaluationContext& context) {
    if (state_ != State::Following) {
        return interp::Directive::Stop;
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
                if (!evaluation.fixes.empty()) {
                    fixes_ = std::move(evaluation.fixes);
                    return stop(State::Restart, std::format("{} failed at {}", describe(followId),
                                                            locations_.describe(event.location)));
                }
                return stop(State::Rejected, std::format("{} did not pass at its first evaluation point, {}{}",
                                                         describe(followId), locations_.describe(event.location),
                                                         evaluation.reason.empty() ? "" : ": " + evaluation.reason));
            case Evaluation::Outcome::Passes:
                break;
            }
        }
        for (const std::uint32_t index : here.first(here.size() - (isFollowHere ? 1 : 0))) {
            const WaypointId id{.segment = current_, .index = index};
            Evaluation avoid = evaluate(id, event, context, false);
            if (avoid.outcome == Evaluation::Outcome::Unsupported) {
                return stop(State::Unsupported, std::format("{}: {}", describe(id), avoid.reason));
            }
            if (avoid.outcome == Evaluation::Outcome::Passes) {
                std::string reason = std::format("{} passed at {}", describe(id), locations_.describe(event.location));
                if (!avoid.fixes.empty()) {
                    fixes_ = std::move(avoid.fixes);
                    return stop(State::Restart, std::move(reason));
                }
                return stop(State::Rejected, std::move(reason));
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
            return {.outcome = Evaluation::Outcome::Unsupported, .fixes = {}, .reason = "it has no constraint"};
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
// failing follow constraint collects the fixes of its false conjuncts; a passing avoid constraint gets the fix of its
// first conjunct that a fix may make false.
SegmentMonitor::Evaluation SegmentMonitor::evaluateConstraint(WaypointId id, const Event& event,
                                                              interp::EvaluationContext& context, bool isFollow,
                                                              ast::QualType resultType) {
    const std::expected<const CompiledConstraint*, std::string> compiled =
        constraints_.compile(id, waypoint(id).constraint->value, context.function(), event.location, resultType);
    if (!compiled) {
        return {.outcome = Evaluation::Outcome::Unsupported, .fixes = {}, .reason = compiled.error()};
    }
    const std::vector<Conjunct>& conjuncts = (*compiled)->conjuncts;
    Evaluation evaluation;
    std::vector<value::Value> values; // of the conjuncts, while all hold
    for (const Conjunct& conjunct : conjuncts) {
        const std::expected<value::Value, std::string> value = context.evaluate(conjunct.expr);
        if (!value) {
            evaluation.outcome = Evaluation::Outcome::Fails;
            evaluation.reason = std::format("the constraint cannot be evaluated: {}", value.error());
            continue;
        }
        if (isTrue(*value)) {
            values.push_back(*value);
            continue;
        }
        evaluation.outcome = Evaluation::Outcome::Fails;
        if (!isFollow) {
            return evaluation; // an avoid constraint with a false conjunct does not pass
        }
        const std::optional<Fix> fix = resolve(conjunct, *value, true, context);
        const auto isSameInput = [&](const Fix& other) {
            return fix && other.input == fix->input;
        };
        if (fix && std::ranges::none_of(evaluation.fixes, isSameInput)) {
            evaluation.fixes.push_back(*fix);
        }
    }
    if (!isFollow && evaluation.outcome == Evaluation::Outcome::Passes) {
        for (std::size_t i = 0; i < conjuncts.size(); ++i) {
            if (std::optional<Fix> fix = resolve(conjuncts[i], values[i], false, context)) {
                evaluation.fixes.push_back(std::move(*fix));
                break;
            }
        }
    }
    return evaluation;
}

// Witness format 2.x, "branching": the outcome of the condition, or for a switch the case value or default. A case
// value that a follow waypoint asks for fixes an unfixed input like the equality `condition == value`; a passing avoid
// waypoint fixes it to a value that no avoid waypoint at the switch avoids. Outcomes are taken as they are.
SegmentMonitor::Evaluation SegmentMonitor::evaluateBranching(WaypointId id, const Event& event,
                                                             interp::EvaluationContext& context, bool isFollow) {
    const witness::Waypoint& point = waypoint(id);
    if (!point.constraint) {
        return {.outcome = Evaluation::Outcome::Unsupported, .fixes = {}, .reason = "it has no constraint"};
    }
    const std::expected<BranchExpectation, std::string> expectation =
        constraints_.branch(id, point.constraint->value, context.function(), event.location);
    if (!expectation) {
        return {.outcome = Evaluation::Outcome::Unsupported, .fixes = {}, .reason = expectation.error()};
    }
    const interp::ConditionEvent& condition = *event.condition;
    const bool isSwitch = condition.construct == interp::ConditionEvent::Construct::Switch;
    const bool isOutcome =
        expectation->kind == BranchExpectation::Kind::True || expectation->kind == BranchExpectation::Kind::False;
    if (isSwitch == isOutcome) {
        return {.outcome = Evaluation::Outcome::Unsupported,
                .fixes = {},
                .reason = std::format("the constraint '{}' does not fit a branching at '{}'", point.constraint->value,
                                      nameOf(condition.construct))};
    }
    if (isOutcome) {
        const bool passes = condition.outcome == (expectation->kind == BranchExpectation::Kind::True);
        return {
            .outcome = passes ? Evaluation::Outcome::Passes : Evaluation::Outcome::Fails, .fixes = {}, .reason = {}};
    }

    Evaluation evaluation;
    const auto* switchStmt = ast::cast<ast::SwitchStmt>(condition.stmt);
    const ast::QualType type = switchStmt->condition()->type();
    const auto addFix = [&](const Bound& bound) {
        std::optional<Fix> fix = fixFor(condition.switchValue, type, bound);
        if (fix) {
            evaluation.fixes.push_back(std::move(*fix));
        }
        return fix.has_value();
    };
    bool passes = false;
    if (expectation->kind == BranchExpectation::Kind::Default) {
        passes = condition.switchLabel == nullptr || ast::isa<ast::DefaultStmt>(condition.switchLabel);
    } else {
        const value::Value expected = valueOf(program_.context, expectation->caseValue, type);
        passes = expected.bits() == condition.switchValue.bits();
        if (!passes && isFollow) {
            addFix({.op = value::Comparison::Equal, .constant = expected, .type = type});
        }
    }
    if (passes && !isFollow && isUnfixedInput(condition.switchValue)) {
        for (const value::Value& candidate : unavoidedValues(event, context)) {
            if (addFix({.op = value::Comparison::Equal, .constant = candidate, .type = type})) {
                break;
            }
        }
    }
    evaluation.outcome = passes ? Evaluation::Outcome::Passes : Evaluation::Outcome::Fails;
    return evaluation;
}

// The values of a switch that no avoid waypoint of the current segment at it avoids, so that one fix satisfies all of
// them (validation-crafted/switch.3A1 avoids two cases and default): the case values in source order, then the
// smallest non-negative value that matches no case label, unless default is avoided.
std::vector<value::Value> SegmentMonitor::unavoidedValues(const Event& event, interp::EvaluationContext& context) {
    const auto* switchStmt = ast::cast<ast::SwitchStmt>(event.condition->stmt);
    const ast::QualType type = switchStmt->condition()->type();
    std::vector<UInt128> avoided; // the bits of avoided case values in the switch's type
    bool isDefaultAvoided = false;
    for (const std::uint32_t i : waypointsAt(current_, witness::WaypointType::Branching, event.location)) {
        const WaypointId id{.segment = current_, .index = i};
        const witness::Waypoint& point = waypoint(id);
        if (point.action != witness::WaypointAction::Avoid || !point.constraint) {
            continue;
        }
        const std::expected<BranchExpectation, std::string> expectation =
            constraints_.branch(id, point.constraint->value, context.function(), event.location);
        if (expectation && expectation->kind == BranchExpectation::Kind::Default) {
            isDefaultAvoided = true;
        } else if (expectation && expectation->kind == BranchExpectation::Kind::Case) {
            avoided.push_back(valueOf(program_.context, expectation->caseValue, type).bits());
        }
    }
    std::vector<value::Value> values;
    const auto add = [&](Int128 number) {
        value::Value candidate = valueOf(program_.context, number, type);
        if (!std::ranges::contains(avoided, candidate.bits())) {
            values.push_back(std::move(candidate));
        }
    };
    std::vector<std::pair<Int128, Int128>> ranges;
    for (const ast::CaseStmt* label : switchStmt->cases()) {
        add(label->low());
        ranges.emplace_back(label->low(), label->high());
    }
    if (!isDefaultAvoided) {
        std::sort(ranges.begin(), ranges.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        Int128 free = 0;
        for (const auto& [low, high] : ranges) {
            if (low > free) {
                break;
            }
            free = std::max(free, high + 1);
        }
        add(free);
    }
    return values;
}

// validate/README.md, "Values": a conjunct is made true or false by fixing the input that is the origin of its
// subject, or of its own value, if that input is neither fixed nor hinted.
std::optional<Fix> SegmentMonitor::resolve(const Conjunct& conjunct, const value::Value& value, bool makeTrue,
                                           interp::EvaluationContext& context) {
    if (conjunct.subject != nullptr) {
        const std::expected<value::Value, std::string> subject = context.evaluate(conjunct.subject);
        const std::expected<value::Value, std::string> constant = context.evaluate(conjunct.constant);
        if (!subject || !constant) {
            return std::nullopt;
        }
        const value::Comparison op = makeTrue ? conjunct.op : negated(conjunct.op);
        return fixFor(*subject, conjunct.subject->type(), {.op = op, .constant = *constant, .type = conjunct.type});
    }
    const ast::QualType type = conjunct.expr->type();
    if (!isArithmetic(program_.context, type)) {
        return std::nullopt;
    }
    const value::Comparison op = makeTrue ? value::Comparison::NotEqual : value::Comparison::Equal;
    return fixFor(value, type, {.op = op, .constant = valueOf(program_.context, 0, type), .type = type});
}

std::optional<Fix> SegmentMonitor::fixFor(const value::Value& subject, ast::QualType subjectType, const Bound& bound) {
    if (!isUnfixedInput(subject)) {
        return std::nullopt;
    }
    const value::InputId input = *subject.origin();
    std::optional<value::Value> chosen =
        chooseInput(program_.context, std::span(&bound, 1), subjectType, oracle_.input(input).type);
    if (!chosen) {
        return std::nullopt;
    }
    return Fix{.input = input, .value = std::move(*chosen)};
}

bool SegmentMonitor::isUnfixedInput(const value::Value& value) const {
    return value.origin().has_value() && oracle_.input(*value.origin()).source == InputSource::Default;
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
