#pragma once

// Parsing of witness constraints: C expressions (plus `\result`) written as if they appeared at a point of the program.

#include <string_view>

#include "ast/context.hpp"
#include "ast/decl.hpp"
#include "ast/expr.hpp"
#include "support/diagnostics.hpp"
#include "support/source_manager.hpp"

namespace nitwit::parse {

/// The program point whose visible declarations a constraint may use.
struct ConstraintScope {
    const ast::TranslationUnit* unit = nullptr;
    /// The function the waypoint is in; null for file scope only.
    const ast::FunctionDecl* function = nullptr;
    /// With a function: block-scope declarations of its body are visible if they are declared before this location
    /// and their block contains it (C17 6.2.1p4); the parameters are visible anywhere in the body.
    SourceLocation location;
};

/// Parses `text` as one expression. Identifiers bind as at the scope's location, falling back to file-scope
/// declarations of the whole unit; `\result` becomes an ast::ResultExpr. The text is registered with `sources` as a
/// file named `<constraint>`, so diagnostics point into it. Returns null after reporting an error (syntax error,
/// unknown identifier, trailing tokens). Runs on the caller's stack; constraints are short.
[[nodiscard]] ast::Expr* parseConstraint(std::string_view text, const ConstraintScope& scope, ast::AstContext& context,
                                         SourceManager& sources, DiagnosticEngine& diagnostics);

} // namespace nitwit::parse
