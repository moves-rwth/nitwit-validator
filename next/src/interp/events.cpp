// The observer: events, the reports of undefined behaviour that the run continues after, and the EvaluationContext
// through which the observer evaluates constraints in the current frame.

#include <format>

#include "interp/execution.hpp"

namespace nitwit::interp {

namespace {

std::string_view describe(value::UndefinedBehavior kind) {
    switch (kind) {
    case value::UndefinedBehavior::None:
        return "no undefined behaviour";
    case value::UndefinedBehavior::SignedOverflow:
        return "signed integer overflow";
    case value::UndefinedBehavior::DivisionByZero:
        return "division by zero";
    case value::UndefinedBehavior::ShiftCountOutOfRange:
        return "a shift count that is negative or not less than the width";
    case value::UndefinedBehavior::ShiftOfNegative:
        return "a left shift of a negative value";
    case value::UndefinedBehavior::ShiftOverflow:
        return "a left shift whose result is not representable";
    case value::UndefinedBehavior::FloatToIntegerOverflow:
        return "a floating value out of the range of the integer type";
    }
    return "undefined behaviour";
}

} // namespace

void Execution::obey(Directive directive, SourceLocation location) const {
    if (directive == Directive::Stop) {
        stop(Termination::Stopped, "the observer stopped the run", location);
    }
}

void Execution::beforeStatement(const ast::Stmt* stmt) {
    obey(observer_.beforeStatement({.stmt = stmt}, *this), stmt->location());
}

void Execution::afterCondition(const ConditionEvent& event) {
    obey(observer_.afterCondition(event, *this), event.location);
}

void Execution::reportUndefined(value::UndefinedBehavior kind, const ast::Expr* where) {
    if (isEvaluatingConstraint_) {
        stop(Termination::UndefinedBehavior, std::string(describe(kind)), locationOf(where));
    }
    obey(observer_.onUndefinedBehavior(kind, where, *this), locationOf(where));
}

std::expected<value::Value, std::string> Execution::evaluate(const ast::Expr* expr) {
    if (isEvaluatingConstraint_) {
        return std::unexpected("a constraint is already being evaluated");
    }
    isEvaluatingConstraint_ = true;
    try {
        value::Value result = rvalue(expr);
        isEvaluatingConstraint_ = false;
        return result;
    } catch (const RunEnd& end) {
        isEvaluatingConstraint_ = false;
        return std::unexpected(end.message);
    }
}

const ast::FunctionDecl* Execution::function() const {
    return frame_ != nullptr ? frame_->body->definition : nullptr;
}

} // namespace nitwit::interp
