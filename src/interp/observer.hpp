#pragma once

// Events of an execution, at the points where the witness formats evaluate waypoints (witness format 2.x,
// "Waypoints"). The validation layer implements ExecutionObserver; while handling an event it can evaluate witness
// constraints in the current frame through EvaluationContext.

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "ast/decl.hpp"
#include "ast/expr.hpp"
#include "ast/stmt.hpp"
#include "support/source_manager.hpp"
#include "value/arithmetic.hpp"
#include "value/term.hpp"
#include "value/value.hpp"

namespace nitwit::interp {

/// Evaluates analyzed expressions in the frame of the current event. Evaluation has no side effects on the program
/// except for input requests (reading an uninitialized variable asks the oracle, as the program itself would).
class EvaluationContext {
public:
    virtual ~EvaluationContext() = default;

    /// The value of `expr`, an rvalue. `\result` (ast::ResultExpr) is the return value of a ReturnEvent and an error
    /// elsewhere. Errors are memory errors and undefined behaviour during evaluation.
    virtual std::expected<value::Value, std::string> evaluate(const ast::Expr* expr) = 0;

    /// The function whose frame is current.
    [[nodiscard]] virtual const ast::FunctionDecl* function() const = 0;

    /// The terms of the run so far (value/README.md, "Terms"); the terms of values in events and of evaluated
    /// expressions refer to it.
    [[nodiscard]] virtual const value::TermTable& terms() const = 0;
};

/// Before a statement executes: every statement in a block, the body of a loop or `if` each time it is entered, and
/// the statements a label or case leads to. Not for blocks themselves.
struct StatementEvent {
    const ast::Stmt* stmt = nullptr;
};

/// After a controlling expression was evaluated.
struct ConditionEvent {
    enum class Construct : std::uint8_t { If, While, DoWhile, For, Conditional, Switch };

    Construct construct = Construct::If;
    const ast::Stmt* stmt = nullptr;                   // the statement; null for Conditional
    const ast::ConditionalExpr* conditional = nullptr; // for Conditional
    SourceLocation location;                           // the keyword (`while` for do loops), or the `?`
    bool outcome = false;                              // for loops and `?:`/`if`: whether the condition holds
    // For Switch: the promoted value of the condition and the label that matched (CaseStmt or DefaultStmt; null if
    // none matched and there is no default).
    value::Value switchValue;
    const ast::Stmt* switchLabel = nullptr;
    // The term of the controlling expression's value, if it has one (for Switch also switchValue.term()).
    std::optional<value::TermId> term{};
};

/// After the arguments of a call were evaluated, before the callee runs.
struct CallEvent {
    const ast::CallExpr* call = nullptr;
    const ast::FunctionDecl* callee = nullptr; // null if the function pointer points to no function
    std::span<const value::Value> arguments;
};

/// After a call returned, before its value is used. A function that a longjmp leaves does not return; a setjmp call
/// returns again after each longjmp to it (interp/README.md, "Non-local jumps").
struct ReturnEvent {
    const ast::CallExpr* call = nullptr;
    const ast::FunctionDecl* callee = nullptr;
    const value::Value* result = nullptr; // null for void functions
};

enum class Directive : std::uint8_t { Continue, Stop };

class ExecutionObserver {
public:
    virtual ~ExecutionObserver() = default;

    virtual Directive beforeStatement(const StatementEvent& event, EvaluationContext& context) = 0;
    virtual Directive afterCondition(const ConditionEvent& event, EvaluationContext& context) = 0;
    virtual Directive afterArguments(const CallEvent& event, EvaluationContext& context) = 0;
    virtual Directive afterReturn(const ReturnEvent& event, EvaluationContext& context) = 0;
    /// Undefined behaviour that the interpreter continues after (signed overflow, see value::UndefinedBehavior).
    virtual Directive onUndefinedBehavior(value::UndefinedBehavior kind, const ast::Expr* expr,
                                          EvaluationContext& context) = 0;
};

/// What the undefined behaviour is, for messages (events.cpp).
[[nodiscard]] std::string_view describe(value::UndefinedBehavior kind);

/// An observer that observes nothing, for replay runs.
class NullObserver final : public ExecutionObserver {
public:
    Directive beforeStatement(const StatementEvent&, EvaluationContext&) override { return Directive::Continue; }
    Directive afterCondition(const ConditionEvent&, EvaluationContext&) override { return Directive::Continue; }
    Directive afterArguments(const CallEvent&, EvaluationContext&) override { return Directive::Continue; }
    Directive afterReturn(const ReturnEvent&, EvaluationContext&) override { return Directive::Continue; }
    Directive onUndefinedBehavior(value::UndefinedBehavior, const ast::Expr*, EvaluationContext&) override {
        return Directive::Continue;
    }
};

} // namespace nitwit::interp
