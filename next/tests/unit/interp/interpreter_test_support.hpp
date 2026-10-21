#pragma once

// For the interpreter tests: a fixture that builds programs from C text and runs them with an oracle that answers
// from a script and records the requests, and with an observer that records the events as text and can evaluate
// witness-like constraints at them.

#include <deque>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "ast/casting.hpp"
#include "ast/context.hpp"
#include "ast/dump.hpp"
#include "interp/interpreter.hpp"
#include "lex/lexer.hpp"
#include "parse/parser.hpp"
#include "sema/expression.hpp"
#include "sema/sema.hpp"
#include "sema/types.hpp"
#include "support/int128.hpp"

namespace nitwit::interp::test {

/// The text of a value in event records: integers in decimal (signed), floating values with std::format.
inline std::string valueText(const value::Value& value) {
    switch (value.kind()) {
    case value::Value::Kind::None:
        return "void";
    case value::Value::Kind::Integer: {
        const Int128 bits = value.signedBits();
        return bits < 0 ? std::format("-{}", static_cast<unsigned long long>(UInt128{0} - value.bits()))
                        : std::format("{}", static_cast<unsigned long long>(bits));
    }
    case value::Value::Kind::Float:
        return std::format("{}", value.asFloat());
    case value::Value::Kind::Double:
        return std::format("{}", value.asDouble());
    case value::Value::Kind::LongDouble:
        return std::format("{}", static_cast<double>(value.asLongDouble()));
    case value::Value::Kind::Bytes:
        return std::format("bytes({})", value.asBytes().size());
    }
    return "?";
}

/// Answers each request with the next integer of `answers` (0 when they are exhausted), converted to the requested
/// type, and records the requests.
class ScriptedOracle final : public InputOracle {
public:
    value::Value provide(const InputRequest& request) override {
        requests.push_back(request);
        Int128 answer = 0;
        if (!answers.empty()) {
            answer = answers.front();
            answers.pop_front();
        }
        if (sema::isFloating(*context, request.type)) {
            const value::FloatFormat format = sema::floatFormat(*context, request.type);
            return value::integerToFloat(
                value::Value::integer(static_cast<UInt128>(answer), {.bits = 128, .isSigned = true}),
                {.bits = 128, .isSigned = true}, format);
        }
        return value::Value::integer(static_cast<UInt128>(answer), sema::integerFormat(*context, request.type));
    }

    ast::AstContext* context = nullptr;
    std::deque<Int128> answers;
    std::vector<InputRequest> requests;
};

/// Records the events as text, e.g. "S 3:5" before a statement, "C if 3:5 true" after a condition, "call f(1, 2)",
/// "return f 3", "ub signed overflow". Hooks may evaluate constraints and stop the run.
class RecordingObserver final : public ExecutionObserver {
public:
    Directive beforeStatement(const StatementEvent& event, EvaluationContext& context) override {
        if (recordStatements) {
            events.push_back("S " + position(event.stmt->location()));
        }
        return onStatement ? onStatement(event, context) : Directive::Continue;
    }

    Directive afterCondition(const ConditionEvent& event, EvaluationContext& context) override {
        static constexpr std::string_view names[] = {"if", "while", "do", "for", "?:", "switch"};
        std::string text =
            std::format("C {} {} ", names[static_cast<std::size_t>(event.construct)], position(event.location));
        if (event.construct == ConditionEvent::Construct::Switch) {
            text += valueText(event.switchValue) + " "
                    + (event.switchLabel != nullptr ? position(event.switchLabel->location()) : "none");
        } else {
            text += event.outcome ? "true" : "false";
        }
        events.push_back(text);
        return onCondition ? onCondition(event, context) : Directive::Continue;
    }

    Directive afterArguments(const CallEvent& event, EvaluationContext& context) override {
        std::string arguments;
        for (const value::Value& argument : event.arguments) {
            arguments += (arguments.empty() ? "" : ", ") + valueText(argument);
        }
        events.push_back(std::format("call {}({})", name(event.callee), arguments));
        return onCall ? onCall(event, context) : Directive::Continue;
    }

    Directive afterReturn(const ReturnEvent& event, EvaluationContext& context) override {
        events.push_back(std::format("return {} {}", name(event.callee),
                                     event.result != nullptr ? valueText(*event.result) : "none"));
        return onReturn ? onReturn(event, context) : Directive::Continue;
    }

    Directive onUndefinedBehavior(value::UndefinedBehavior kind, const ast::Expr* expr, EvaluationContext&) override {
        static constexpr std::string_view names[] = {
            "none",           "signed overflow", "division by zero", "shift count", "shift of negative",
            "shift overflow", "float to integer"};
        events.push_back(std::format("ub {} {}", names[static_cast<std::size_t>(kind)], position(expr->location())));
        return stopOnUndefinedBehavior ? Directive::Stop : Directive::Continue;
    }

    /// The events whose text starts with `prefix`.
    [[nodiscard]] std::vector<std::string> only(std::string_view prefix) const {
        std::vector<std::string> selected;
        for (const std::string& event : events) {
            if (event.starts_with(prefix)) {
                selected.push_back(event);
            }
        }
        return selected;
    }

    std::string position(SourceLocation location) const {
        const LineColumn lineColumn = sources->lineColumn(location);
        return std::format("{}:{}", lineColumn.line, lineColumn.column);
    }

    static std::string name(const ast::FunctionDecl* function) {
        return function != nullptr ? std::string(function->name().text()) : "<none>";
    }

    const SourceManager* sources = nullptr;
    bool recordStatements = true;
    bool stopOnUndefinedBehavior = false;
    std::vector<std::string> events;
    std::function<Directive(const StatementEvent&, EvaluationContext&)> onStatement;
    std::function<Directive(const ConditionEvent&, EvaluationContext&)> onCondition;
    std::function<Directive(const CallEvent&, EvaluationContext&)> onCall;
    std::function<Directive(const ReturnEvent&, EvaluationContext&)> onReturn;
};

class InterpreterTest : public ::testing::Test {
protected:
    /// Parses, analyzes and lowers `text` as the file "test.c"; null (with a test failure) if that fails.
    std::shared_ptr<const Program> build(std::string text, DataModel model = DataModel::LP64) {
        target_ = std::make_unique<TargetInfo>(TargetInfo::forDataModel(model));
        context_ = std::make_unique<ast::AstContext>(*target_, interner_);
        oracle_.context = context_.get();
        observer_.sources = &sources_;
        const FileId file = sources_.addFile("test.c", std::move(text));
        lex::LexResult lexed = lex::lexFile(sources_, file, interner_, diagnostics_);
        tokens_.push_back(std::move(lexed.tokens));
        unit_ = parse::parseTranslationUnit(tokens_.back(), *context_, sources_, diagnostics_);
        if (unit_ == nullptr || !sema::analyze(*unit_, *context_, diagnostics_)) {
            ADD_FAILURE() << "the program is invalid: " << firstError();
            return nullptr;
        }
        auto program = buildProgram(*unit_, *context_);
        if (!program) {
            ADD_FAILURE() << program.error().message;
            return nullptr;
        }
        return *program;
    }

    /// Builds and runs `text`; the program's output is in output().
    RunResult run(std::string text, DataModel model = DataModel::LP64, RunOptions options = {}) {
        const std::shared_ptr<const Program> program = build(std::move(text), model);
        if (program == nullptr) {
            return {.termination = Termination::Unsupported,
                    .exitStatus = 0,
                    .message = "build failed",
                    .location = {},
                    .steps = 0,
                    .inputs = 0};
        }
        output_.str("");
        options.output = &output_;
        Interpreter interpreter(program, oracle_, observer_, options);
        return interpreter.run();
    }

    /// The exit status of a program that must return normally.
    int status(std::string text, DataModel model = DataModel::LP64) {
        const RunResult result = run(std::move(text), model);
        EXPECT_EQ(result.termination, Termination::Returned) << result.message;
        return result.exitStatus;
    }

    /// The output of a program that must return normally.
    std::string outputOf(std::string text, DataModel model = DataModel::LP64) {
        const RunResult result = run(std::move(text), model);
        EXPECT_EQ(result.termination, Termination::Returned) << result.message;
        return output();
    }

    // ---- Constraints, built as ASTs and analyzed like witness constraints (sema/expression.hpp) ----

    /// Analyzes a constraint built with the functions below; `resultType` is the type of \result.
    const ast::Expr* analyzed(ast::Expr* expr, ast::QualType resultType = {}) {
        const ast::Expr* result = sema::analyzeConstraint(expr, resultType, *context_, diagnostics_);
        if (result == nullptr) {
            ADD_FAILURE() << "cannot analyze the constraint: " << firstError();
        }
        return result;
    }

    /// A reference to the local `name` of `function`, or else to the global or function `name`.
    ast::Expr* ref(std::string_view name, const ast::FunctionDecl* function) {
        ast::ValueDecl* decl = nullptr;
        if (function != nullptr) {
            for (ast::VarDecl* local : function->locals()) {
                if (local->name().text() == name) {
                    decl = local;
                }
            }
        }
        for (ast::Decl* global : unit_->decls) {
            if (decl == nullptr && global->name().text() == name) {
                decl = ast::dyn_cast<ast::ValueDecl>(global);
            }
        }
        EXPECT_NE(decl, nullptr) << "no declaration " << name;
        return context_->create<ast::DeclRefExpr>(SourceRange{}, interner_.intern(name), decl);
    }
    ast::Expr* integer(std::uint64_t value) {
        return context_->create<ast::IntegerLiteralExpr>(
            SourceRange{},
            lex::IntegerLiteral{.value = value, .isDecimal = true, .hasUnsignedSuffix = false, .longCount = 0});
    }
    ast::Expr* binary(ast::BinaryOperator op, ast::Expr* lhs, ast::Expr* rhs) {
        return context_->create<ast::BinaryExpr>(SourceRange{}, op, lhs, rhs, SourceLocation{});
    }
    ast::Expr* unary(ast::UnaryOperator op, ast::Expr* operand) {
        return context_->create<ast::UnaryExpr>(SourceRange{}, op, operand);
    }
    ast::Expr* castTo(ast::BuiltinKind kind, ast::Expr* operand) {
        return context_->create<ast::CastExpr>(SourceRange{}, context_->builtin(kind), operand);
    }
    ast::Expr* member(ast::Expr* base, std::string_view name) {
        return context_->create<ast::MemberExpr>(SourceRange{}, base, interner_.intern(name), false, SourceLocation{});
    }
    ast::Expr* subscript(ast::Expr* base, ast::Expr* index) {
        return context_->create<ast::ArraySubscriptExpr>(SourceRange{}, base, index);
    }
    ast::Expr* callOf(ast::Expr* callee, ast::Expr* argument) {
        const std::span<ast::Expr*> arguments = context_->copy(std::span<ast::Expr* const>(&argument, 1));
        return context_->create<ast::CallExpr>(SourceRange{}, callee, arguments, SourceLocation{});
    }
    ast::Expr* result() { return context_->create<ast::ResultExpr>(SourceRange{}); }

    /// "line:column" of a location in test.c.
    [[nodiscard]] std::string position(SourceLocation location) const { return observer_.position(location); }

    [[nodiscard]] std::string output() const { return output_.str(); }

    [[nodiscard]] std::string firstError() const {
        for (const Diagnostic& diagnostic : diagnostics_.diagnostics()) {
            if (diagnostic.severity == Severity::Error) {
                return diagnostics_.format(diagnostic);
            }
        }
        return "";
    }

    SourceManager sources_;
    StringInterner interner_;
    DiagnosticEngine diagnostics_{sources_};
    std::unique_ptr<TargetInfo> target_;
    std::unique_ptr<ast::AstContext> context_;
    std::vector<std::vector<lex::Token>> tokens_;
    ast::TranslationUnit* unit_ = nullptr;
    ScriptedOracle oracle_;
    RecordingObserver observer_;
    std::ostringstream output_;
};

/// The same fixture, run once per data model.
class DataModelTest : public InterpreterTest, public ::testing::WithParamInterface<DataModel> {};

inline std::string dataModelName(const ::testing::TestParamInfo<DataModel>& info) {
    return info.param == DataModel::ILP32 ? "ILP32" : "LP64";
}

} // namespace nitwit::interp::test
