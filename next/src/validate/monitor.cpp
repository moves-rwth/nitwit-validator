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

// `\result`, possibly converted for a comparison: the subject of a hint (validate/README.md, "Values").
bool isResult(const ast::Expr* expr) {
    expr = ast::ignoreParens(expr);
    while (const auto* cast = ast::dyn_cast<ast::ImplicitCastExpr>(expr)) {
        expr = ast::ignoreParens(cast->operand());
    }
    return ast::isa<ast::ResultExpr>(expr);
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
      oracle_(oracle), log_(log), locations_(program.sources, program.file, program.isPreprocessed) {
    NITWIT_ASSERT(!segments_.empty(), "a violation sequence without segments");
    for (const witness::Segment& segment : segments_) {
        std::vector<Place>& places = places_.emplace_back();
        for (const witness::Waypoint& waypoint : segment.waypoints) {
            places.push_back(locations_.place(waypoint.location));
        }
    }
}

void SegmentMonitor::beginRun(std::uint32_t run) {
    run_ = run;
    current_ = 0;
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
    // The target waypoint passes when the error function is called while the target segment is current.
    if (event.callee != nullptr && event.callee->name().text() == errorFunction_) {
        return stop(State::ErrorCalled, std::format("'{}' was called at {} in segment {} of {}", errorFunction_,
                                                    locations_.describe(call.location), current_ + 1, segmentCount()));
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

// validate/README.md, "Follow, avoid and progress": only the waypoints of the current segment are evaluated. A
// passing avoid waypoint rejects the execution, also where the follow waypoint would pass. A follow waypoint must pass
// at its first evaluation point and then makes the next segment current.
interp::Directive SegmentMonitor::handle(const Event& event, interp::EvaluationContext& context) {
    if (state_ != State::Following) {
        return interp::Directive::Stop;
    }
    // Cascading: one event completes several segments only through assumption waypoints at the same statement. Such
    // a waypoint does not fail strictly, since it may refer to a later execution of the statement. As in 3.x, the
    // avoid waypoints of a segment are not evaluated at the event that made it current.
    bool isCascading = false;
    while (true) {
        const witness::Segment& segment = segments_[current_];
        const auto followIndex = static_cast<std::uint32_t>(segment.waypoints.size() - 1);
        for (std::uint32_t i = 0; i < followIndex && !isCascading; ++i) {
            const WaypointId id{.segment = current_, .index = i};
            if (waypoint(id).type != event.type || !places_[current_][i].contains(event.location)) {
                continue;
            }
            Evaluation avoid = evaluate(id, event, context, false);
            if (avoid.outcome == Evaluation::Outcome::Unsupported) {
                return stop(State::Unsupported, std::format("{}: {}", describe(id), avoid.reason));
            }
            if (avoid.outcome == Evaluation::Outcome::Passes) {
                return stop(State::Rejected,
                            std::format("{} passed at {}", describe(id), locations_.describe(event.location)));
            }
        }

        const WaypointId followId{.segment = current_, .index = followIndex};
        const witness::Waypoint& follow = waypoint(followId);
        if (follow.type != event.type || !places_[current_][followIndex].contains(event.location)) {
            return interp::Directive::Continue;
        }
        Evaluation evaluation = evaluate(followId, event, context, true);
        switch (evaluation.outcome) {
        case Evaluation::Outcome::Unsupported:
            return stop(State::Unsupported, std::format("{}: {}", describe(followId), evaluation.reason));
        case Evaluation::Outcome::Passes:
            advance(event);
            if (event.type != witness::WaypointType::Assumption) {
                return interp::Directive::Continue;
            }
            isCascading = true;
            continue;
        case Evaluation::Outcome::Fails:
            if (!evaluation.fixes.empty()) {
                fixes_ = std::move(evaluation.fixes);
                return stop(State::Restart,
                            std::format("{} failed at {}", describe(followId), locations_.describe(event.location)));
            }
            if (isCascading) {
                return interp::Directive::Continue;
            }
            return stop(State::Rejected, std::format("{} did not pass at its first evaluation point, {}{}",
                                                     describe(followId), locations_.describe(event.location),
                                                     evaluation.reason.empty() ? "" : ": " + evaluation.reason));
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

// A constraint passes when all its conjuncts hold. A failing follow constraint collects the fixes of its conjuncts;
// for an avoid constraint, a conjunct whose truth rests on an unfixed input counts as not holding (validate/README.md,
// "Avoid constraints never fix inputs").
SegmentMonitor::Evaluation SegmentMonitor::evaluateConstraint(WaypointId id, const Event& event,
                                                              interp::EvaluationContext& context, bool isFollow,
                                                              ast::QualType resultType) {
    const std::expected<const CompiledConstraint*, std::string> compiled =
        constraints_.compile(id, waypoint(id).constraint->value, context.function(), event.location, resultType);
    if (!compiled) {
        return {.outcome = Evaluation::Outcome::Unsupported, .fixes = {}, .reason = compiled.error()};
    }
    Evaluation evaluation;
    for (const Conjunct& conjunct : (*compiled)->conjuncts) {
        const std::expected<value::Value, std::string> value = context.evaluate(conjunct.expr);
        if (!value) {
            evaluation.outcome = Evaluation::Outcome::Fails;
            evaluation.reason = std::format("the constraint cannot be evaluated: {}", value.error());
            continue;
        }
        bool holds = isTrue(*value);
        if (holds && !isFollow) {
            holds = !isUnfixedInput(*value);
            if (holds && conjunct.subject != nullptr) {
                const std::expected<value::Value, std::string> subject = context.evaluate(conjunct.subject);
                holds = !(subject && isUnfixedInput(*subject));
            }
        }
        if (holds) {
            continue;
        }
        evaluation.outcome = Evaluation::Outcome::Fails;
        if (!isFollow) {
            return evaluation;
        }
        const std::optional<Fix> fix = resolve(conjunct, context);
        const auto isSameInput = [&](const Fix& other) {
            return fix && other.input == fix->input;
        };
        if (fix && std::ranges::none_of(evaluation.fixes, isSameInput)) {
            evaluation.fixes.push_back(*fix);
        }
    }
    return evaluation;
}

// Witness format 2.x, "branching": the outcome of the condition, or for a switch the case value or default. A case
// value that a follow waypoint asks for fixes an unfixed input like the equality `condition == value`.
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
    bool passes = false;
    if (expectation->kind == BranchExpectation::Kind::Default) {
        passes = condition.switchLabel == nullptr || ast::isa<ast::DefaultStmt>(condition.switchLabel);
    } else {
        const ast::QualType type = ast::cast<ast::SwitchStmt>(condition.stmt)->condition()->type();
        const value::Value expected = valueOf(program_.context, expectation->caseValue, type);
        passes = expected.bits() == condition.switchValue.bits();
        if (!passes && isFollow) {
            if (std::optional<Fix> fix = resolveEqual(condition.switchValue, expected, type)) {
                evaluation.fixes.push_back(std::move(*fix));
            }
        }
    }
    if (passes && !isFollow && isUnfixedInput(condition.switchValue)) {
        passes = false;
    }
    evaluation.outcome = passes ? Evaluation::Outcome::Passes : Evaluation::Outcome::Fails;
    return evaluation;
}

// validate/README.md, "Values": a failed relation with a constant fixes the input that its subject is an unmodified
// copy of, if that input is neither fixed nor hinted.
std::optional<Fix> SegmentMonitor::resolve(const Conjunct& conjunct, interp::EvaluationContext& context) {
    if (conjunct.subject == nullptr) {
        return std::nullopt;
    }
    const std::expected<value::Value, std::string> subject = context.evaluate(conjunct.subject);
    if (!subject || !isUnfixedInput(*subject)) {
        return std::nullopt;
    }
    const std::expected<value::Value, std::string> constant = context.evaluate(conjunct.constant);
    if (!constant) {
        return std::nullopt;
    }
    const ProvidedInput& input = oracle_.input(*subject->origin());
    const Bound bound{.op = conjunct.op, .constant = *constant, .type = conjunct.subject->type()};
    std::optional<value::Value> chosen = choose(program_.context, std::span(&bound, 1), input.type);
    if (!chosen) {
        return std::nullopt;
    }
    return Fix{.input = *subject->origin(), .value = std::move(*chosen)};
}

std::optional<Fix> SegmentMonitor::resolveEqual(const value::Value& value, const value::Value& expected,
                                                ast::QualType type) {
    if (!isUnfixedInput(value)) {
        return std::nullopt;
    }
    const ProvidedInput& input = oracle_.input(*value.origin());
    const Bound bound{.op = value::Comparison::Equal, .constant = expected, .type = type};
    std::optional<value::Value> chosen = choose(program_.context, std::span(&bound, 1), input.type);
    if (!chosen) {
        return std::nullopt;
    }
    return Fix{.input = *value.origin(), .value = std::move(*chosen)};
}

bool SegmentMonitor::isUnfixedInput(const value::Value& value) const {
    return value.origin().has_value() && oracle_.input(*value.origin()).source == InputSource::Default;
}

// validate/README.md, "Values": `\result op constant` of a function_return waypoint at this call. A follow waypoint
// asks for a value that satisfies all its relations on `\result`, an avoid waypoint for one that violates one of them.
// The follow waypoint's hint wins.
void SegmentMonitor::offerHint(const interp::CallEvent& event, interp::EvaluationContext& context) {
    const SourceLocation location = event.call->rightParenLocation();
    const witness::Segment& segment = segments_[current_];
    std::optional<value::Value> followHint;
    std::optional<value::Value> avoidHint;
    for (std::uint32_t i = 0; i < segment.waypoints.size(); ++i) {
        const WaypointId id{.segment = current_, .index = i};
        const witness::Waypoint& point = waypoint(id);
        if (point.type != witness::WaypointType::FunctionReturn || !point.constraint
            || !places_[current_][i].contains(location)) {
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
            if (conjunct.subject == nullptr || !isResult(conjunct.subject)) {
                continue;
            }
            if (const auto constant = context.evaluate(conjunct.constant)) {
                bounds.push_back({.op = conjunct.op, .constant = *constant, .type = conjunct.subject->type()});
            }
        }
        const bool isFollow = point.action == witness::WaypointAction::Follow;
        if (isFollow && !bounds.empty()) {
            followHint = choose(program_.context, bounds, event.call->type());
        }
        for (const Bound& bound : bounds) {
            if (isFollow || avoidHint) {
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
