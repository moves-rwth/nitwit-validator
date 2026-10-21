#pragma once

// Text renderings of the AST, for golden tests and debugging.

#include <string>

#include "ast/decl.hpp"
#include "ast/expr.hpp"
#include "ast/stmt.hpp"
#include "support/source_manager.hpp"

namespace nitwit::ast {

/// The C spelling of a type as written, e.g. "const char *", "int (*)(int, ...)", "struct S [3]", "size_t".
[[nodiscard]] std::string typeToString(QualType type);

/// One node per line, children indented by two spaces. Each line names the node kind, the name or operator, the type
/// (once sema has run, with "lvalue" for lvalues), and the physical position of the node as <line:column>, e.g.
///   FunctionDecl main 'int (void)' <3:5>
///     CompoundStmt <3:16>
///       ReturnStmt <4:5>
///         IntegerLiteral 0 'int' <4:12>
/// Line markers are ignored, so dumps of the same file are stable. The indentation stops at 64 levels; deeper lines
/// start with their level, e.g. "[65] ".
[[nodiscard]] std::string dump(const TranslationUnit& unit, const SourceManager& sources);
[[nodiscard]] std::string dump(const Decl* decl, const SourceManager& sources);
[[nodiscard]] std::string dump(const Stmt* stmt, const SourceManager& sources);
[[nodiscard]] std::string dump(const Expr* expr, const SourceManager& sources);

} // namespace nitwit::ast
