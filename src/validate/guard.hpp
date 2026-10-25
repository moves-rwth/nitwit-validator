#pragma once

// Guards of the program (validate/README.md, "Guards"): the places where a run that takes one side ends at once, so
// that the search treats the other side like the outcome a waypoint needs.

#include <optional>
#include <string_view>
#include <vector>

#include "ast/decl.hpp"
#include "ast/stmt.hpp"

namespace nitwit::validate {

/// An `if` of which one branch consists only of a call of a function that ends the program (inside blocks or after
/// labels), and the other branch, if there is one, does not.
struct IfGuard {
    bool continuingOutcome = false;            // the outcome of the condition with which the run goes on
    const ast::FunctionDecl* ending = nullptr; // the function the other branch calls
};

/// Finds the guards of one program.
class GuardFinder {
public:
    /// The functions that end the program are abort, exit, _Exit and __assert_fail, but not those that `unit` defines:
    /// the interpreter calls the definition, as for any function. Never `errorFunction`: its call is where the witness
    /// goes.
    GuardFinder(const ast::TranslationUnit& unit, std::string_view errorFunction);

    /// The guard that `stmt` is, if it is one.
    [[nodiscard]] std::optional<IfGuard> guardOf(const ast::IfStmt& stmt) const;

private:
    /// The function that `stmt` consists only of a call of, if it ends the program.
    [[nodiscard]] const ast::FunctionDecl* endingCallee(const ast::Stmt* stmt) const;

    std::vector<std::string_view> endingFunctions_;
};

/// Whether `callee`, the callee of a call event (the definition if the program has one), is the builtin
/// __VERIFIER_assume, which ends the run where its argument is 0; a definition of the program is not.
[[nodiscard]] bool isBuiltinAssume(const ast::FunctionDecl* callee);

} // namespace nitwit::validate
