#pragma once

// Guards of the program (validate/README.md, "Guards"): the places where a run that takes one side ends at once, so
// that the search treats the other side like the outcome a waypoint needs.

#include <optional>
#include <string_view>

#include "ast/decl.hpp"
#include "ast/stmt.hpp"

namespace nitwit::validate {

/// An `if` of which one branch consists only of a call of abort, exit, _Exit or __assert_fail (inside blocks or after
/// labels), and the other branch, or the fall-through, does not.
struct IfGuard {
    bool continuingOutcome = false;            // the outcome of the condition with which the run goes on
    const ast::FunctionDecl* ending = nullptr; // the function the other branch calls
};

/// The guard that `stmt` is, if it is one. A branch that calls `errorFunction` makes no guard: it is where the witness
/// goes.
[[nodiscard]] std::optional<IfGuard> guardOf(const ast::IfStmt& stmt, std::string_view errorFunction);

/// Whether `callee` is the builtin __VERIFIER_assume, which ends the run where its argument is 0; a definition of the
/// program is not.
[[nodiscard]] bool isBuiltinAssume(const ast::FunctionDecl* callee);

} // namespace nitwit::validate
