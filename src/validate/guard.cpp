#include "validate/guard.hpp"

#include <algorithm>
#include <array>

#include "ast/casting.hpp"
#include "ast/expr.hpp"

namespace nitwit::validate {

namespace {

/// The library functions that end the program and make an `if` that calls one in a branch a guard.
constexpr std::array<std::string_view, 4> endingFunctions{"abort", "exit", "_Exit", "__assert_fail"};

/// The function of endingFunctions that `stmt` consists only of a call of, inside blocks of one statement or after
/// labels; null for none and for `errorFunction`.
const ast::FunctionDecl* endingCallee(const ast::Stmt* stmt, std::string_view errorFunction) {
    while (true) {
        if (const auto* block = ast::dyn_cast<ast::CompoundStmt>(stmt); block != nullptr && block->body().size() == 1) {
            stmt = block->body().front();
        } else if (const auto* label = ast::dyn_cast<ast::LabelStmt>(stmt)) {
            stmt = label->subStmt();
        } else {
            break;
        }
    }
    const auto* expression = ast::dyn_cast<ast::ExpressionStmt>(stmt);
    const auto* call =
        expression != nullptr ? ast::dyn_cast<ast::CallExpr>(ast::ignoreParens(expression->expr())) : nullptr;
    const ast::FunctionDecl* callee = call != nullptr ? call->directCallee() : nullptr;
    if (callee == nullptr || callee->name().text() == errorFunction) {
        return nullptr;
    }
    return std::ranges::contains(endingFunctions, callee->name().text()) ? callee : nullptr;
}

} // namespace

std::optional<IfGuard> guardOf(const ast::IfStmt& stmt, std::string_view errorFunction) {
    const ast::FunctionDecl* thenEnding = endingCallee(stmt.thenStmt(), errorFunction);
    const ast::FunctionDecl* elseEnding = endingCallee(stmt.elseStmt(), errorFunction);
    if ((thenEnding == nullptr) == (elseEnding == nullptr)) {
        return std::nullopt;
    }
    return thenEnding != nullptr ? IfGuard{.continuingOutcome = false, .ending = thenEnding}
                                 : IfGuard{.continuingOutcome = true, .ending = elseEnding};
}

bool isBuiltinAssume(const ast::FunctionDecl* callee) {
    return callee != nullptr && callee->body() == nullptr && callee->name().text() == "__VERIFIER_assume";
}

} // namespace nitwit::validate
