#pragma once

// Replay mode (tools/difftest/README.md, "Contract with the interpreter"): runs a program with the nondeterministic
// values of an inputs file and logs them like the native difftest harness, so that the interpreter can be compared
// with natively compiled runs.

#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ast/context.hpp"
#include "interp/observer.hpp"
#include "interp/oracle.hpp"
#include "support/target.hpp"

namespace nitwit::driver {

/// The exit codes and stderr markers of the harness.
inline constexpr int exitError = 97;
inline constexpr int exitAssume = 98;
inline constexpr int exitBadInput = 99;
inline constexpr std::string_view marker = "NITWIT-DIFFTEST: ";

/// Answers __VERIFIER_nondet_* calls from the inputs file, one line per value, and appends each value to the input
/// log. Other requests (uninitialized reads, unknown functions) get 0, since native runs have no counterpart for them.
/// A line that cannot be read ends the process with the harness's marker and exit code 99.
class ReplayOracle final : public interp::InputOracle {
public:
    /// An empty path means no inputs (every value is 0) or no log.
    ReplayOracle(ast::AstContext& context, std::string inputsPath, std::string logPath);
    ~ReplayOracle() override;
    ReplayOracle(const ReplayOracle&) = delete;
    ReplayOracle& operator=(const ReplayOracle&) = delete;

    value::Value provide(const interp::InputRequest& request) override;

private:
    /// The next line of the inputs file without its line break; nullopt when the file is exhausted.
    std::optional<std::string> nextLine(std::string_view function);
    [[noreturn]] void badInput(std::string_view function, std::string_view message) const;
    void log(std::string_view function, std::string_view value, bool isDefault);
    /// The next input as a value of `type`, logged.
    value::Value integer(ast::QualType type, std::string_view function);
    value::Value floating(ast::QualType type, std::string_view function);
    /// The return type of the harness's function of this name (tools/difftest/harness.c), if it has one.
    [[nodiscard]] std::optional<ast::QualType> harnessType(std::string_view function) const;
    [[nodiscard]] bool isBool(ast::QualType type) const;

    ast::AstContext& context_;
    std::string inputsPath_;
    std::string logPath_;
    bool isOpened_ = false;
    std::vector<std::string> lines_;
    std::size_t nextLine_ = 0;
    std::FILE* log_ = nullptr;
    bool isLogOpened_ = false;
};

/// Stops the run when the program calls an error function the harness provides (__assert_fail, __VERIFIER_error, or
/// reach_error without a definition) and remembers which.
class ReplayObserver final : public interp::ExecutionObserver {
public:
    interp::Directive beforeStatement(const interp::StatementEvent&, interp::EvaluationContext&) override {
        return interp::Directive::Continue;
    }
    interp::Directive afterCondition(const interp::ConditionEvent&, interp::EvaluationContext&) override {
        return interp::Directive::Continue;
    }
    interp::Directive afterArguments(const interp::CallEvent& event, interp::EvaluationContext& context) override;
    interp::Directive afterReturn(const interp::ReturnEvent&, interp::EvaluationContext&) override {
        return interp::Directive::Continue;
    }
    interp::Directive onUndefinedBehavior(value::UndefinedBehavior, const ast::Expr*,
                                          interp::EvaluationContext&) override {
        return interp::Directive::Continue;
    }

    /// The error function that was called, if any.
    [[nodiscard]] const std::optional<std::string>& errorFunction() const { return errorFunction_; }

private:
    std::optional<std::string> errorFunction_;
};

struct ReplayOptions {
    std::string inputs;
    std::string inputLog;
    DataModel dataModel = DataModel::ILP32;
    std::string program;
};

/// How the process ends after a replay.
struct ReplayOutcome {
    int exitCode = 0;
    bool abort = false; // end by SIGABRT, like abort() natively
};

/// Loads and runs the program. Call it on a large stack.
[[nodiscard]] ReplayOutcome replay(const ReplayOptions& options);

} // namespace nitwit::driver
