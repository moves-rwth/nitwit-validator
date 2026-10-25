#include "validate/guard.hpp"

#include <algorithm>
#include <array>

#include "ast/casting.hpp"
#include "ast/expr.hpp"

namespace nitwit::validate {

namespace {

/// The library functions that end the program.
constexpr std::array<std::string_view, 4> libraryEndingFunctions{"abort", "exit", "_Exit", "__assert_fail"};

} // namespace

GuardFinder::GuardFinder(const ast::TranslationUnit& unit, std::string_view errorFunction) {
    for (const std::string_view name : libraryEndingFunctions) {
        const auto isDefinition = [&](const ast::Decl* decl) {
            const auto* function = ast::dyn_cast<ast::FunctionDecl>(decl);
            return function != nullptr && function->body() != nullptr && function->name().text() == name;
        };
        if (name != errorFunction && std::ranges::none_of(unit.decls, isDefinition)) {
            endingFunctions_.push_back(name);
        }
    }
}

std::optional<IfGuard> GuardFinder::guardOf(const ast::IfStmt& stmt) const {
    const ast::FunctionDecl* thenEnding = endingCallee(stmt.thenStmt());
    const ast::FunctionDecl* elseEnding = endingCallee(stmt.elseStmt());
    if ((thenEnding == nullptr) == (elseEnding == nullptr)) {
        return std::nullopt;
    }
    return thenEnding != nullptr ? IfGuard{.continuingOutcome = false, .ending = thenEnding}
                                 : IfGuard{.continuingOutcome = true, .ending = elseEnding};
}

// Blocks of one statement and labels around the call are skipped.
const ast::FunctionDecl* GuardFinder::endingCallee(const ast::Stmt* stmt) const {
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
    return callee != nullptr && std::ranges::contains(endingFunctions_, callee->name().text()) ? callee : nullptr;
}

bool isBuiltinAssume(const ast::FunctionDecl* callee) {
    return callee != nullptr && callee->body() == nullptr && callee->name().text() == "__VERIFIER_assume";
}

} // namespace nitwit::validate
