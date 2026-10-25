#pragma once

// For the validation tests: a fixture that analyzes a small C program (so that events carry real AST nodes and
// locations), builds witnesses in code, and validates them with scripted runs. A script reports the events of an
// execution through FakeExecution, which also evaluates constraints over variables it holds, and records terms as the
// interpreter does (value/README.md, "Terms").

#include <cstdint>
#include <expected>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "ast/casting.hpp"
#include "ast/context.hpp"
#include "ast/stmt.hpp"
#include "interp/observer.hpp"
#include "interp/oracle.hpp"
#include "lex/lexer.hpp"
#include "parse/parser.hpp"
#include "sema/sema.hpp"
#include "sema/types.hpp"
#include "support/int128.hpp"
#include "validate/restart.hpp"
#include "validate/validator.hpp"
#include "value/arithmetic.hpp"
#include "witness/model.hpp"

namespace nitwit::validate::test {

using witness::WaypointAction;
using witness::WaypointType;

/// A waypoint at `line` and, if given, `column`; with a constraint if `constraint` is not empty.
inline witness::Waypoint waypoint(WaypointType type, WaypointAction action, std::uint32_t line,
                                  std::optional<std::uint32_t> column, std::string_view constraint = "") {
    witness::Waypoint point;
    point.type = type;
    point.action = action;
    point.location = {.fileName = "test.c", .line = line, .column = column, .function = ""};
    if (!constraint.empty()) {
        point.constraint = witness::Constraint{.value = std::string(constraint), .format = "c_expression"};
    }
    return point;
}

inline witness::Waypoint follow(WaypointType type, std::uint32_t line, std::optional<std::uint32_t> column,
                                std::string_view constraint = "") {
    return waypoint(type, WaypointAction::Follow, line, column, constraint);
}

inline witness::Waypoint avoid(WaypointType type, std::uint32_t line, std::optional<std::uint32_t> column,
                               std::string_view constraint = "") {
    return waypoint(type, WaypointAction::Avoid, line, column, constraint);
}

inline witness::Waypoint target(std::uint32_t line, std::optional<std::uint32_t> column) {
    return waypoint(WaypointType::Target, WaypointAction::Follow, line, column);
}

/// A witness with one violation sequence of these segments.
inline witness::YamlWitness sequence(std::vector<std::vector<witness::Waypoint>> segments) {
    witness::ViolationSequence violation;
    for (std::vector<witness::Waypoint>& waypoints : segments) {
        violation.segments.push_back({.waypoints = std::move(waypoints)});
    }
    witness::YamlWitness yaml;
    yaml.violationSequences.push_back(std::move(violation));
    return yaml;
}

/// The run's end: main returned `status`.
inline interp::RunResult returned(int status) {
    return {.termination = interp::Termination::Returned,
            .exitStatus = status,
            .message = {},
            .location = {},
            .steps = 0,
            .inputs = 0};
}

inline interp::RunResult endedBy(interp::Termination termination, std::string message = {}) {
    return {.termination = termination,
            .exitStatus = 0,
            .message = std::move(message),
            .location = {},
            .steps = 0,
            .inputs = 0};
}

class ValidateTest;

/// One scripted execution. Each event method reports the event at the AST node found at the line and column, and
/// ends the script (by the exception Stopped) when the observer stops the run. Constraints are evaluated over the
/// variables the script assigned; reading one that was never assigned asks the oracle, as the interpreter does.
/// Values derived from inputs carry terms: the inputs, and the operations of the expressions the fake evaluates.
class FakeExecution final : public interp::EvaluationContext {
public:
    struct Stopped {};

    FakeExecution(ValidateTest& fixture, interp::InputOracle& oracle, interp::ExecutionObserver& observer);

    /// A witness constraint: its terms are strict, and any undefined behaviour is an error, as in the interpreter.
    std::expected<value::Value, std::string> evaluate(const ast::Expr* expr) override;
    [[nodiscard]] const ast::FunctionDecl* function() const override { return function_; }
    [[nodiscard]] const value::TermTable& terms() const override { return terms_; }

    /// Before the statement that starts here.
    void statement(std::uint32_t line, std::uint32_t column);
    /// The statement that starts here: its event, then a declaration's initializers or the assignment `x = e` to a
    /// variable.
    void execute(std::uint32_t line, std::uint32_t column);
    /// After the condition of the if, loop or `?:` at this keyword or `?`; its outcome is the condition's value, and
    /// the event carries its term.
    bool branch(std::uint32_t line, std::uint32_t column);
    /// The same with a given outcome and no term, for conditions the fake cannot evaluate.
    bool branch(std::uint32_t line, std::uint32_t column, bool outcome);
    /// After the condition of the switch here: the case label taken, or null.
    const ast::Stmt* switchOn(std::uint32_t line, std::uint32_t column);
    /// A call of a nondeterministic function whose `)` is here: its events and its input request. Returns the value.
    value::Value nondet(std::uint32_t line, std::uint32_t column);
    /// A call whose `)` is here, without input: its call and return events (`result` for a non-void function).
    void call(std::uint32_t line, std::uint32_t column, std::optional<value::Value> result = std::nullopt);
    /// The call of the builtin __VERIFIER_assume whose `)` is here: its call event with the argument, and the return
    /// event if the argument is not 0. Returns whether it is not; if it is, the interpreter ends the run.
    bool assume(std::uint32_t line, std::uint32_t column);

    /// Assigns a local of the current function or a global.
    void assign(std::string_view name, value::Value value);
    /// A value of the type of variable `name`.
    value::Value number(std::string_view name, Int128 number);
    /// The value of an assigned integer variable.
    [[nodiscard]] Int128 integer(std::string_view name) const;
    /// `value`, an int or a floating value, converted to the type of variable `name` as an initialization converts it,
    /// with its term.
    value::Value convertedTo(std::string_view name, const value::Value& value);
    /// The function whose frame is current; `main` at first.
    void enter(std::string_view function);

private:
    struct EvaluationError {
        std::string message;
    };

    void obey(interp::Directive directive) const;
    /// The value of an expression of the program; the fake must be able to evaluate it.
    value::Value programValue(const ast::Expr* expr);
    value::Value eval(const ast::Expr* expr);
    value::Value cast(const ast::Expr* expr, ast::CastKind kind, const ast::Expr* operand);
    value::Value binary(const ast::BinaryExpr* expr);
    value::Value read(const ast::VarDecl* var, const ast::Expr* site);
    value::Value request(interp::InputRequest::Kind kind, const ast::Expr* site, ast::QualType type,
                         const ast::FunctionDecl* function);
    const ast::VarDecl* variable(std::string_view name) const;
    value::ScalarFormat formatOf(ast::QualType type) const;
    /// Records the term of an arithmetic result and stops a constraint at undefined behaviour.
    value::Value checked(const value::Result& result, std::optional<value::TermId> term) const;

    ValidateTest& fixture_;
    interp::InputOracle& oracle_;
    interp::ExecutionObserver& observer_;
    const ast::FunctionDecl* function_ = nullptr;
    std::map<const ast::VarDecl*, value::Value> variables_;
    std::uint32_t inputs_ = 0;
    std::unordered_map<const ast::Expr*, std::uint32_t> occurrences_;
    const value::Value* result_ = nullptr; // during a return event
    value::TermTable terms_;
    bool isConstraint_ = false; // while a constraint is evaluated
};

class ValidateTest : public ::testing::Test {
public:
    /// Parses and analyzes `text` as the file `name`; fails the test if that fails.
    void program(std::string text, DataModel model = DataModel::ILP32, std::string name = "test.c") {
        target_ = std::make_unique<TargetInfo>(TargetInfo::forDataModel(model));
        context_ = std::make_unique<ast::AstContext>(*target_, interner_);
        file_ = sources_.addFile(std::move(name), std::move(text));
        lex::LexResult lexed = lex::lexFile(sources_, file_, interner_, diagnostics_);
        tokens_.push_back(std::move(lexed.tokens));
        unit_ = parse::parseTranslationUnit(tokens_.back(), *context_, sources_, diagnostics_);
        ASSERT_TRUE(unit_ != nullptr && sema::analyze(*unit_, *context_, diagnostics_)) << firstError();
        input_ = std::make_unique<ProgramInput>(ProgramInput{.sources = sources_,
                                                             .file = file_,
                                                             .originalFile = FileId::None,
                                                             .context = *context_,
                                                             .unit = *unit_,
                                                             .program = nullptr});
    }

    /// Validates `witness` with runs made by `script`; counts the runs in scriptRuns_.
    ValidationResult validate(const witness::YamlWitness& witness,
                              std::function<interp::RunResult(FakeExecution&)> script) {
        const RunFunction run = [&](interp::InputOracle& oracle, interp::ExecutionObserver& observer) {
            ++scriptRuns_;
            FakeExecution execution(*this, oracle, observer);
            try {
                return script(execution);
            } catch (const FakeExecution::Stopped&) {
                return endedBy(interp::Termination::Stopped, "the observer stopped the run");
            }
        };
        ValidationOptions options = options_;
        options.log = &log_;
        return validateWithRuns(witness, *input_, options, diagnostics_, run);
    }

    // ---- The AST node at a position; each fails the test (by an exception) if there is none ----

    [[nodiscard]] const ast::Stmt* statementAt(std::uint32_t line, std::uint32_t column) const {
        const ast::Stmt* found = nullptr;
        forEachStatement([&](const ast::Stmt* stmt) {
            if (found == nullptr && isAt(stmt->location(), line, column)) {
                found = stmt;
            }
        });
        return required(found, "statement", line, column);
    }

    /// The call whose `)` is at the position.
    [[nodiscard]] const ast::CallExpr* callAt(std::uint32_t line, std::uint32_t column) const {
        const ast::CallExpr* found = nullptr;
        forEachExpression([&](const ast::Expr* expr) {
            const auto* call = ast::dyn_cast<ast::CallExpr>(expr);
            if (found == nullptr && call != nullptr && isAt(call->rightParenLocation(), line, column)) {
                found = call;
            }
        });
        return required(found, "call", line, column);
    }

    /// The condition event of the branching at the position, without outcome; `expr` is its condition.
    [[nodiscard]] interp::ConditionEvent conditionAt(std::uint32_t line, std::uint32_t column,
                                                     const ast::Expr*& expr) const {
        using Construct = interp::ConditionEvent::Construct;
        interp::ConditionEvent event;
        bool isFound = false;
        forEachStatement([&](const ast::Stmt* stmt) {
            const auto set = [&](Construct construct, SourceLocation location, const ast::Expr* condition) {
                if (!isFound && isAt(location, line, column)) {
                    event.construct = construct;
                    event.stmt = stmt;
                    event.location = location;
                    expr = condition;
                    isFound = true;
                }
            };
            if (const auto* ifStmt = ast::dyn_cast<ast::IfStmt>(stmt)) {
                set(Construct::If, stmt->location(), ifStmt->condition());
            } else if (const auto* whileStmt = ast::dyn_cast<ast::WhileStmt>(stmt)) {
                set(Construct::While, stmt->location(), whileStmt->condition());
            } else if (const auto* doStmt = ast::dyn_cast<ast::DoWhileStmt>(stmt)) {
                set(Construct::DoWhile, doStmt->whileLocation(), doStmt->condition());
            } else if (const auto* forStmt = ast::dyn_cast<ast::ForStmt>(stmt)) {
                set(Construct::For, stmt->location(), forStmt->condition());
            } else if (const auto* switchStmt = ast::dyn_cast<ast::SwitchStmt>(stmt)) {
                set(Construct::Switch, stmt->location(), switchStmt->condition());
            }
        });
        forEachExpression([&](const ast::Expr* candidate) {
            const auto* conditional = ast::dyn_cast<ast::ConditionalExpr>(candidate);
            if (!isFound && conditional != nullptr && isAt(conditional->questionLocation(), line, column)) {
                event.construct = Construct::Conditional;
                event.conditional = conditional;
                event.location = conditional->questionLocation();
                expr = conditional->condition();
                isFound = true;
            }
        });
        if (!isFound) {
            throw std::runtime_error(std::format("no branching at {}:{}", line, column));
        }
        return event;
    }

    [[nodiscard]] const ast::FunctionDecl* functionNamed(std::string_view name) const {
        for (const ast::Decl* decl : unit_->decls) {
            const auto* function = ast::dyn_cast<ast::FunctionDecl>(decl);
            if (function != nullptr && function->body() != nullptr && function->name().text() == name) {
                return function;
            }
        }
        throw std::runtime_error(std::format("no function {}", name));
    }

    /// The line of a node; 0 for null.
    [[nodiscard]] std::uint32_t lineOf(const ast::Stmt* stmt) const {
        return stmt != nullptr ? sources_.lineColumn(stmt->location()).line : 0;
    }

    [[nodiscard]] const ast::TranslationUnit& unit() const { return *unit_; }
    [[nodiscard]] ast::AstContext& context() const { return *context_; }
    [[nodiscard]] std::string log() const { return log_.str(); }

    [[nodiscard]] std::string firstError() const {
        for (const Diagnostic& diagnostic : diagnostics_.diagnostics()) {
            if (diagnostic.severity == Severity::Error) {
                return diagnostics_.format(diagnostic);
            }
        }
        return "";
    }

protected:
    template <typename F>
    void forEachStatement(F visit) const {
        for (const ast::Decl* decl : unit_->decls) {
            if (const auto* function = ast::dyn_cast<ast::FunctionDecl>(decl);
                function != nullptr && function->body() != nullptr) {
                walkStatement(function->body(), visit);
            }
        }
    }

    template <typename F>
    void forEachExpression(F visit) const {
        const auto visitExpr = [&](const ast::Expr* expr) {
            walkExpression(expr, visit);
        };
        forEachStatement([&](const ast::Stmt* stmt) { childExpressions(stmt, visitExpr); });
    }

    SourceManager sources_;
    StringInterner interner_;
    DiagnosticEngine diagnostics_{sources_};
    std::unique_ptr<TargetInfo> target_;
    std::unique_ptr<ast::AstContext> context_;
    std::vector<std::vector<lex::Token>> tokens_;
    FileId file_ = FileId::None;
    ast::TranslationUnit* unit_ = nullptr;
    std::unique_ptr<ProgramInput> input_;
    ValidationOptions options_;
    std::ostringstream log_;
    int scriptRuns_ = 0;

private:
    bool isAt(SourceLocation location, std::uint32_t line, std::uint32_t column) const {
        return location.file == file_ && sources_.lineColumn(location) == LineColumn{line, column};
    }

    template <typename T>
    static const T* required(const T* node, std::string_view what, std::uint32_t line, std::uint32_t column) {
        if (node == nullptr) {
            throw std::runtime_error(std::format("no {} at {}:{}", what, line, column));
        }
        return node;
    }

    template <typename F>
    static void walkStatement(const ast::Stmt* stmt, F& visit) {
        if (stmt == nullptr) {
            return;
        }
        visit(stmt);
        switch (stmt->kind()) {
        case ast::StmtKind::Compound:
            for (const ast::Stmt* item : ast::cast<ast::CompoundStmt>(stmt)->body()) {
                walkStatement(item, visit);
            }
            return;
        case ast::StmtKind::If:
            walkStatement(ast::cast<ast::IfStmt>(stmt)->thenStmt(), visit);
            walkStatement(ast::cast<ast::IfStmt>(stmt)->elseStmt(), visit);
            return;
        case ast::StmtKind::While:
            return walkStatement(ast::cast<ast::WhileStmt>(stmt)->body(), visit);
        case ast::StmtKind::DoWhile:
            return walkStatement(ast::cast<ast::DoWhileStmt>(stmt)->body(), visit);
        case ast::StmtKind::For:
            walkStatement(ast::cast<ast::ForStmt>(stmt)->init(), visit);
            return walkStatement(ast::cast<ast::ForStmt>(stmt)->body(), visit);
        case ast::StmtKind::Switch:
            return walkStatement(ast::cast<ast::SwitchStmt>(stmt)->body(), visit);
        case ast::StmtKind::Case:
            return walkStatement(ast::cast<ast::CaseStmt>(stmt)->subStmt(), visit);
        case ast::StmtKind::Default:
            return walkStatement(ast::cast<ast::DefaultStmt>(stmt)->subStmt(), visit);
        case ast::StmtKind::Label:
            return walkStatement(ast::cast<ast::LabelStmt>(stmt)->subStmt(), visit);
        default:
            return;
        }
    }

    // The expressions directly in a statement (not in its sub-statements).
    template <typename F>
    static void childExpressions(const ast::Stmt* stmt, F& visit) {
        switch (stmt->kind()) {
        case ast::StmtKind::Expression:
            return visit(ast::cast<ast::ExpressionStmt>(stmt)->expr());
        case ast::StmtKind::Declaration:
            for (const ast::Decl* decl : ast::cast<ast::DeclarationStmt>(stmt)->decls()) {
                if (const auto* var = ast::dyn_cast<ast::VarDecl>(decl); var != nullptr && var->initializer()) {
                    visit(var->initializer());
                }
            }
            return;
        case ast::StmtKind::If:
            return visit(ast::cast<ast::IfStmt>(stmt)->condition());
        case ast::StmtKind::While:
            return visit(ast::cast<ast::WhileStmt>(stmt)->condition());
        case ast::StmtKind::DoWhile:
            return visit(ast::cast<ast::DoWhileStmt>(stmt)->condition());
        case ast::StmtKind::For:
            if (ast::cast<ast::ForStmt>(stmt)->condition() != nullptr) {
                visit(ast::cast<ast::ForStmt>(stmt)->condition());
            }
            if (ast::cast<ast::ForStmt>(stmt)->increment() != nullptr) {
                visit(ast::cast<ast::ForStmt>(stmt)->increment());
            }
            return;
        case ast::StmtKind::Switch:
            return visit(ast::cast<ast::SwitchStmt>(stmt)->condition());
        case ast::StmtKind::Return:
            if (ast::cast<ast::ReturnStmt>(stmt)->value() != nullptr) {
                visit(ast::cast<ast::ReturnStmt>(stmt)->value());
            }
            return;
        default:
            return;
        }
    }

    template <typename F>
    static void walkExpression(const ast::Expr* expr, F& visit) {
        if (expr == nullptr) {
            return;
        }
        visit(expr);
        switch (expr->kind()) {
        case ast::ExprKind::Paren:
            return walkExpression(ast::cast<ast::ParenExpr>(expr)->inner(), visit);
        case ast::ExprKind::Unary:
            return walkExpression(ast::cast<ast::UnaryExpr>(expr)->operand(), visit);
        case ast::ExprKind::Binary:
            walkExpression(ast::cast<ast::BinaryExpr>(expr)->lhs(), visit);
            return walkExpression(ast::cast<ast::BinaryExpr>(expr)->rhs(), visit);
        case ast::ExprKind::Conditional: {
            const auto* conditional = ast::cast<ast::ConditionalExpr>(expr);
            walkExpression(conditional->condition(), visit);
            walkExpression(conditional->trueExpr(), visit);
            return walkExpression(conditional->falseExpr(), visit);
        }
        case ast::ExprKind::Call:
            walkExpression(ast::cast<ast::CallExpr>(expr)->callee(), visit);
            for (const ast::Expr* argument : ast::cast<ast::CallExpr>(expr)->arguments()) {
                walkExpression(argument, visit);
            }
            return;
        case ast::ExprKind::Cast:
            return walkExpression(ast::cast<ast::CastExpr>(expr)->operand(), visit);
        case ast::ExprKind::ImplicitCast:
            return walkExpression(ast::cast<ast::ImplicitCastExpr>(expr)->operand(), visit);
        default:
            return;
        }
    }
};

// ---- FakeExecution ----

inline FakeExecution::FakeExecution(ValidateTest& fixture, interp::InputOracle& oracle,
                                    interp::ExecutionObserver& observer)
    : fixture_(fixture), oracle_(oracle), observer_(observer), function_(fixture.functionNamed("main")),
      terms_(fixture.context().target().dataModel()) {}

inline void FakeExecution::obey(interp::Directive directive) const {
    if (directive == interp::Directive::Stop) {
        throw Stopped{};
    }
}

inline void FakeExecution::statement(std::uint32_t line, std::uint32_t column) {
    obey(observer_.beforeStatement({.stmt = fixture_.statementAt(line, column)}, *this));
}

inline void FakeExecution::execute(std::uint32_t line, std::uint32_t column) {
    const ast::Stmt* stmt = fixture_.statementAt(line, column);
    obey(observer_.beforeStatement({.stmt = stmt}, *this));
    if (const auto* declaration = ast::dyn_cast<ast::DeclarationStmt>(stmt)) {
        for (const ast::Decl* decl : declaration->decls()) {
            if (const auto* var = ast::dyn_cast<ast::VarDecl>(decl); var != nullptr && var->initializer()) {
                variables_.insert_or_assign(var, programValue(var->initializer()));
            }
        }
        return;
    }
    const auto* expression = ast::dyn_cast<ast::ExpressionStmt>(stmt);
    const auto* assignment =
        expression != nullptr ? ast::dyn_cast<ast::BinaryExpr>(ast::ignoreParens(expression->expr())) : nullptr;
    const auto* target = assignment != nullptr && assignment->op() == ast::BinaryOperator::Assign
                             ? ast::dyn_cast<ast::DeclRefExpr>(ast::ignoreParens(assignment->lhs()))
                             : nullptr;
    const auto* var = target != nullptr ? ast::dyn_cast<ast::VarDecl>(target->decl()) : nullptr;
    if (var == nullptr) {
        throw std::runtime_error(std::format("the fake cannot execute the statement at {}:{}", line, column));
    }
    variables_.insert_or_assign(var, programValue(assignment->rhs()));
}

inline bool FakeExecution::branch(std::uint32_t line, std::uint32_t column) {
    const ast::Expr* condition = nullptr;
    interp::ConditionEvent event = fixture_.conditionAt(line, column, condition);
    const value::Value value = programValue(condition);
    event.outcome = value::toBool(value).bits() != 0;
    event.term = value.term();
    obey(observer_.afterCondition(event, *this));
    return event.outcome;
}

inline bool FakeExecution::branch(std::uint32_t line, std::uint32_t column, bool outcome) {
    const ast::Expr* condition = nullptr;
    interp::ConditionEvent event = fixture_.conditionAt(line, column, condition);
    event.outcome = outcome;
    obey(observer_.afterCondition(event, *this));
    return outcome;
}

inline const ast::Stmt* FakeExecution::switchOn(std::uint32_t line, std::uint32_t column) {
    const ast::Expr* condition = nullptr;
    interp::ConditionEvent event = fixture_.conditionAt(line, column, condition);
    const value::Value value = programValue(condition);
    const auto* switchStmt = ast::cast<ast::SwitchStmt>(event.stmt);
    event.switchValue = value;
    event.term = value.term();
    event.switchLabel = switchStmt->defaultStmt();
    for (const ast::CaseStmt* label : switchStmt->cases()) {
        if (label->low() <= value.signedBits() && value.signedBits() <= label->high()) {
            event.switchLabel = label;
        }
    }
    event.outcome = event.switchLabel != nullptr;
    obey(observer_.afterCondition(event, *this));
    return event.switchLabel;
}

inline value::Value FakeExecution::nondet(std::uint32_t line, std::uint32_t column) {
    const ast::CallExpr* call = fixture_.callAt(line, column);
    obey(observer_.afterArguments({.call = call, .callee = call->directCallee(), .arguments = {}}, *this));
    value::Value result =
        request(interp::InputRequest::Kind::NondetCall, call, call->type().unqualified(), call->directCallee());
    result_ = &result;
    const interp::Directive directive =
        observer_.afterReturn({.call = call, .callee = call->directCallee(), .result = &result}, *this);
    result_ = nullptr;
    obey(directive);
    return result;
}

inline void FakeExecution::call(std::uint32_t line, std::uint32_t column, std::optional<value::Value> result) {
    const ast::CallExpr* call = fixture_.callAt(line, column);
    obey(observer_.afterArguments({.call = call, .callee = call->directCallee(), .arguments = {}}, *this));
    result_ = result ? &*result : nullptr;
    const interp::Directive directive =
        observer_.afterReturn({.call = call, .callee = call->directCallee(), .result = result_}, *this);
    result_ = nullptr;
    obey(directive);
}

inline bool FakeExecution::assume(std::uint32_t line, std::uint32_t column) {
    const ast::CallExpr* call = fixture_.callAt(line, column);
    const value::Value argument = programValue(call->arguments().front());
    obey(observer_.afterArguments({.call = call, .callee = call->directCallee(), .arguments = std::span(&argument, 1)},
                                  *this));
    if (value::toBool(argument).bits() == 0) {
        return false;
    }
    obey(observer_.afterReturn({.call = call, .callee = call->directCallee(), .result = nullptr}, *this));
    return true;
}

inline void FakeExecution::assign(std::string_view name, value::Value value) {
    variables_.insert_or_assign(variable(name), std::move(value));
}

inline value::Value FakeExecution::number(std::string_view name, Int128 number) {
    const ast::VarDecl* var = variable(name);
    if (sema::isFloating(fixture_.context(), var->type())) {
        return value::integerToFloat(
            value::Value::integer(static_cast<UInt128>(number), {.bits = 128, .isSigned = true}),
            {.bits = 128, .isSigned = true}, sema::floatFormat(fixture_.context(), var->type()));
    }
    return value::Value::integer(static_cast<UInt128>(number), sema::integerFormat(fixture_.context(), var->type()));
}

inline Int128 FakeExecution::integer(std::string_view name) const {
    const auto found = variables_.find(variable(name));
    if (found == variables_.end()) {
        throw std::runtime_error(std::format("{} was not assigned", name));
    }
    return found->second.signedBits();
}

inline value::Value FakeExecution::convertedTo(std::string_view name, const value::Value& value) {
    ast::AstContext& context = fixture_.context();
    const ast::QualType to = variable(name)->type();
    const bool isFloating = value.kind() != value::Value::Kind::Integer;
    const value::FloatFormat from = value.kind() == value::Value::Kind::Float        ? value::FloatFormat::Float
                                    : value.kind() == value::Value::Kind::LongDouble ? value::FloatFormat::LongDouble
                                                                                     : value::FloatFormat::Double;
    // The source of an integer is an int: the scripts convert the results of __VERIFIER_nondet_int.
    const value::IntegerFormat fromInteger{.bits = 32, .isSigned = true};
    value::Value converted;
    std::optional<value::TermId> term;
    if (sema::isFloating(context, to)) {
        const value::FloatFormat format = sema::floatFormat(context, to);
        converted =
            isFloating ? value::convertFloat(value, from, format) : value::integerToFloat(value, fromInteger, format);
        term =
            isFloating ? terms_.convertFloat(value, from, format) : terms_.integerToFloat(value, fromInteger, format);
    } else {
        const value::IntegerFormat format = sema::integerFormat(context, to);
        converted = isFloating ? value::floatToInteger(value, from, format, context.target().dataModel()).value
                               : value::convertInteger(value, format);
        term =
            isFloating ? terms_.floatToInteger(value, from, format) : terms_.convertInteger(value, fromInteger, format);
    }
    converted.setTerm(term);
    return converted;
}

inline void FakeExecution::enter(std::string_view function) {
    function_ = fixture_.functionNamed(function);
}

inline const ast::VarDecl* FakeExecution::variable(std::string_view name) const {
    for (const ast::VarDecl* local : function_->locals()) {
        if (local->name().text() == name) {
            return local;
        }
    }
    for (const ast::Decl* decl : fixture_.unit().decls) {
        if (const auto* var = ast::dyn_cast<ast::VarDecl>(decl); var != nullptr && var->name().text() == name) {
            return var;
        }
    }
    throw std::runtime_error(std::format("no variable {}", name));
}

inline value::ScalarFormat FakeExecution::formatOf(ast::QualType type) const {
    if (sema::isFloating(fixture_.context(), type)) {
        return sema::floatFormat(fixture_.context(), type);
    }
    return sema::integerFormat(fixture_.context(), type);
}

inline value::Value FakeExecution::request(interp::InputRequest::Kind kind, const ast::Expr* site, ast::QualType type,
                                           const ast::FunctionDecl* function) {
    const interp::InputRequest request{.kind = kind,
                                       .id = value::InputId{inputs_++},
                                       .site = site,
                                       .occurrence = occurrences_[site]++,
                                       .type = type,
                                       .function = function};
    value::Value value = oracle_.provide(request);
    value.setTerm(terms_.input(request.id, value, formatOf(type)));
    return value;
}

inline std::expected<value::Value, std::string> FakeExecution::evaluate(const ast::Expr* expr) {
    const value::TermTable::StrictScope strict(terms_);
    isConstraint_ = true;
    try {
        value::Value value = eval(expr);
        isConstraint_ = false;
        return value;
    } catch (const EvaluationError& error) {
        isConstraint_ = false;
        return std::unexpected(error.message);
    }
}

inline value::Value FakeExecution::programValue(const ast::Expr* expr) {
    try {
        return eval(expr);
    } catch (const EvaluationError& error) {
        throw std::runtime_error("the fake cannot evaluate an expression of the program: " + error.message);
    }
}

inline value::Value FakeExecution::checked(const value::Result& result, std::optional<value::TermId> term) const {
    if (result.undefinedBehavior == value::UndefinedBehavior::DivisionByZero) {
        throw EvaluationError{"division by zero"};
    }
    if (isConstraint_ && result.undefinedBehavior != value::UndefinedBehavior::None) {
        throw EvaluationError{"undefined behaviour"};
    }
    value::Value value = result.value;
    value.setTerm(term);
    return value;
}

inline value::Value FakeExecution::read(const ast::VarDecl* var, const ast::Expr* site) {
    if (const auto found = variables_.find(var); found != variables_.end()) {
        return found->second;
    }
    // An uninitialized read asks the oracle; the value is stored, so later reads agree.
    value::Value value =
        request(interp::InputRequest::Kind::UninitializedRead, site, var->type().unqualified(), nullptr);
    variables_.emplace(var, value);
    return value;
}

inline value::Value FakeExecution::eval(const ast::Expr* expr) {
    ast::AstContext& context = fixture_.context();
    switch (expr->kind()) {
    case ast::ExprKind::IntegerLiteral:
        return value::Value::integer(ast::cast<ast::IntegerLiteralExpr>(expr)->value().value,
                                     sema::integerFormat(context, expr->type()));
    case ast::ExprKind::CharacterLiteral:
        return value::Value::integer(
            static_cast<UInt128>(static_cast<Int128>(ast::cast<ast::CharacterLiteralExpr>(expr)->value().value)),
            sema::integerFormat(context, expr->type()));
    case ast::ExprKind::FloatingLiteral: {
        const lex::FloatingLiteral& literal = ast::cast<ast::FloatingLiteralExpr>(expr)->value();
        switch (sema::floatFormat(context, expr->type())) {
        case value::FloatFormat::Float:
            return value::Value::ofFloat(literal.floatValue);
        case value::FloatFormat::Double:
            return value::Value::ofDouble(literal.doubleValue);
        case value::FloatFormat::LongDouble:
            return value::Value::ofLongDouble(literal.longDoubleValue);
        }
        break;
    }
    case ast::ExprKind::DeclRef:
        if (const auto* constant = ast::dyn_cast<ast::EnumConstantDecl>(ast::cast<ast::DeclRefExpr>(expr)->decl())) {
            return value::Value::integer(static_cast<UInt128>(constant->value()),
                                         sema::integerFormat(context, expr->type()));
        }
        break;
    case ast::ExprKind::Paren:
        return eval(ast::cast<ast::ParenExpr>(expr)->inner());
    case ast::ExprKind::ImplicitCast: {
        const auto* implicit = ast::cast<ast::ImplicitCastExpr>(expr);
        return cast(expr, implicit->castKind(), implicit->operand());
    }
    case ast::ExprKind::Cast: {
        const auto* written = ast::cast<ast::CastExpr>(expr);
        return cast(expr, written->castKind(), written->operand());
    }
    case ast::ExprKind::Unary: {
        const auto* unary = ast::cast<ast::UnaryExpr>(expr);
        const value::Value operand = eval(unary->operand());
        switch (unary->op()) {
        case ast::UnaryOperator::Minus: {
            const value::IntegerFormat format = sema::integerFormat(context, expr->type());
            return checked(value::integerNegate(operand, format), terms_.integerNegate(operand, format));
        }
        case ast::UnaryOperator::LogicalNot: {
            value::Value result = value::Value::integer(value::toBool(operand).bits() == 0 ? 1 : 0,
                                                        sema::integerFormat(context, expr->type()));
            result.setTerm(terms_.logicalNot(operand, formatOf(unary->operand()->type())));
            return result;
        }
        default:
            break;
        }
        break;
    }
    case ast::ExprKind::Binary:
        return binary(ast::cast<ast::BinaryExpr>(expr));
    case ast::ExprKind::Result:
        if (result_ == nullptr) {
            throw EvaluationError{"\\result outside of a return"};
        }
        return *result_;
    default:
        break;
    }
    throw EvaluationError{"the fake cannot evaluate this expression"};
}

// The conversions between arithmetic types, with their terms.
inline value::Value FakeExecution::cast(const ast::Expr* expr, ast::CastKind kind, const ast::Expr* operand) {
    ast::AstContext& context = fixture_.context();
    if (kind == ast::CastKind::LValueToRValue) {
        const auto* ref = ast::dyn_cast<ast::DeclRefExpr>(ast::ignoreParens(operand));
        const auto* var = ref != nullptr ? ast::dyn_cast<ast::VarDecl>(ref->decl()) : nullptr;
        if (var == nullptr) {
            throw EvaluationError{"the fake reads variables only"};
        }
        return read(var, expr);
    }
    const value::Value value = eval(operand);
    const value::ScalarFormat from = formatOf(operand->type());
    const value::ScalarFormat to = formatOf(expr->type());
    value::Value converted;
    std::optional<value::TermId> term;
    switch (kind) {
    case ast::CastKind::NoOp:
        return value;
    case ast::CastKind::IntegralCast:
        converted = value::convertInteger(value, std::get<value::IntegerFormat>(to));
        term = terms_.convertInteger(value, std::get<value::IntegerFormat>(from), std::get<value::IntegerFormat>(to));
        break;
    case ast::CastKind::IntegralToBoolean:
    case ast::CastKind::FloatingToBoolean:
        converted = value::toBool(value);
        term = terms_.toBool(value, from);
        break;
    case ast::CastKind::IntegralToFloating:
        converted =
            value::integerToFloat(value, std::get<value::IntegerFormat>(from), std::get<value::FloatFormat>(to));
        term = terms_.integerToFloat(value, std::get<value::IntegerFormat>(from), std::get<value::FloatFormat>(to));
        break;
    case ast::CastKind::FloatingToIntegral:
        return checked(
            value::floatToInteger(value, std::get<value::FloatFormat>(from), std::get<value::IntegerFormat>(to),
                                  context.target().dataModel()),
            terms_.floatToInteger(value, std::get<value::FloatFormat>(from), std::get<value::IntegerFormat>(to)));
    case ast::CastKind::FloatingCast:
        converted = value::convertFloat(value, std::get<value::FloatFormat>(from), std::get<value::FloatFormat>(to));
        term = terms_.convertFloat(value, std::get<value::FloatFormat>(from), std::get<value::FloatFormat>(to));
        break;
    default:
        throw EvaluationError{"the fake cannot evaluate this cast"};
    }
    converted.setTerm(term);
    return converted;
}

// Integer arithmetic, comparisons, `&&` and `||`, with their terms.
inline value::Value FakeExecution::binary(const ast::BinaryExpr* expr) {
    ast::AstContext& context = fixture_.context();
    const value::IntegerFormat intFormat = sema::integerFormat(context, expr->type());
    if (expr->op() == ast::BinaryOperator::LogicalAnd || expr->op() == ast::BinaryOperator::LogicalOr) {
        const bool isOr = expr->op() == ast::BinaryOperator::LogicalOr;
        const value::Value lhs = eval(expr->lhs());
        const value::ScalarFormat lhsFormat = formatOf(expr->lhs()->type());
        const value::ScalarFormat rhsFormat = formatOf(expr->rhs()->type());
        const bool left = value::toBool(lhs).bits() != 0;
        if (left == isOr) {
            value::Value result = value::Value::integer(left ? 1 : 0, intFormat);
            result.setTerm(isOr ? terms_.logicalOr(lhs, lhsFormat, nullptr, rhsFormat)
                                : terms_.logicalAnd(lhs, lhsFormat, nullptr, rhsFormat));
            return result;
        }
        const value::Value rhs = eval(expr->rhs());
        value::Value result = value::Value::integer(value::toBool(rhs).bits(), intFormat);
        result.setTerm(isOr ? terms_.logicalOr(lhs, lhsFormat, &rhs, rhsFormat)
                            : terms_.logicalAnd(lhs, lhsFormat, &rhs, rhsFormat));
        return result;
    }
    const value::Value lhs = eval(expr->lhs());
    const value::Value rhs = eval(expr->rhs());
    const ast::QualType operandType = expr->lhs()->type();
    const auto compare = [&](value::Comparison comparison) {
        value::Value result;
        if (sema::isFloating(context, operandType)) {
            const value::FloatFormat format = sema::floatFormat(context, operandType);
            result = value::Value::integer(value::floatCompare(comparison, lhs, rhs, format) ? 1 : 0, intFormat);
            result.setTerm(terms_.floatCompare(comparison, lhs, rhs, format));
        } else {
            const value::IntegerFormat format = sema::integerFormat(context, operandType);
            result = value::Value::integer(value::integerCompare(comparison, lhs, rhs, format) ? 1 : 0, intFormat);
            result.setTerm(terms_.integerCompare(comparison, lhs, rhs, format));
        }
        return result;
    };
    const auto arithmetic = [&](value::IntegerOperator op) {
        return checked(value::integerBinary(op, lhs, rhs, intFormat), terms_.integerBinary(op, lhs, rhs, intFormat));
    };
    switch (expr->op()) {
    case ast::BinaryOperator::Equal:
        return compare(value::Comparison::Equal);
    case ast::BinaryOperator::NotEqual:
        return compare(value::Comparison::NotEqual);
    case ast::BinaryOperator::Less:
        return compare(value::Comparison::Less);
    case ast::BinaryOperator::LessEqual:
        return compare(value::Comparison::LessEqual);
    case ast::BinaryOperator::Greater:
        return compare(value::Comparison::Greater);
    case ast::BinaryOperator::GreaterEqual:
        return compare(value::Comparison::GreaterEqual);
    case ast::BinaryOperator::Add:
        return arithmetic(value::IntegerOperator::Add);
    case ast::BinaryOperator::Sub:
        return arithmetic(value::IntegerOperator::Sub);
    case ast::BinaryOperator::Mul:
        return arithmetic(value::IntegerOperator::Mul);
    case ast::BinaryOperator::Div:
        return arithmetic(value::IntegerOperator::Div);
    case ast::BinaryOperator::Rem:
        return arithmetic(value::IntegerOperator::Rem);
    default:
        throw EvaluationError{"the fake cannot evaluate this operator"};
    }
}

} // namespace nitwit::validate::test
