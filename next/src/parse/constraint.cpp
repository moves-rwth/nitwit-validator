// Witness constraints: an expression parsed in the scope of a program point, which is rebuilt from the AST.

#include "parse/constraint.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ast/casting.hpp"
#include "ast/stmt.hpp"
#include "lex/lexer.hpp"
#include "parse/parser_impl.hpp"
#include "support/assert.hpp"

namespace nitwit::parse {

namespace detail {

namespace {

// Constraints are parsed on the caller's stack, so their nesting limit is far below that of programs. Real
// constraints nest a few levels.
constexpr std::size_t maxConstraintNestingDepth = 256;

bool isBefore(SourceLocation first, SourceLocation second) {
    return first.file == second.file && first.offset < second.offset;
}

bool contains(SourceRange range, SourceLocation location) {
    return location.file == range.begin.file && range.begin.offset <= location.offset
           && location.offset < range.end.offset;
}

// Binds a declaration of the program in `scope`, as the parser did where it was declared. Unlike Parser::bindOrdinary,
// it does not link redeclarations: the program's AST must not change.
void bindExisting(Scope& scope, ast::Decl* decl) {
    if (auto* enumDecl = ast::dyn_cast<ast::EnumDecl>(decl)) {
        for (ast::EnumConstantDecl* constant : enumDecl->constants()) {
            scope.ordinary[constant->name()] = constant;
        }
    }
    if (decl->name().empty()) {
        return;
    }
    switch (decl->kind()) {
    case ast::DeclKind::Var:
    case ast::DeclKind::Function:
    case ast::DeclKind::EnumConstant:
    case ast::DeclKind::Typedef:
        scope.ordinary[decl->name()] = decl;
        return;
    case ast::DeclKind::Record:
    case ast::DeclKind::Enum:
        scope.tags[decl->name()] = decl;
        return;
    case ast::DeclKind::Field:
    case ast::DeclKind::StaticAssert:
        return;
    }
    std::unreachable();
}

// The statement that a case, default or ordinary label labels; null for any other statement.
const ast::Stmt* labeledStatement(const ast::Stmt* stmt) {
    switch (stmt->kind()) {
    case ast::StmtKind::Case:
        return ast::cast<ast::CaseStmt>(stmt)->subStmt();
    case ast::StmtKind::Default:
        return ast::cast<ast::DefaultStmt>(stmt)->subStmt();
    case ast::StmtKind::Label:
        return ast::cast<ast::LabelStmt>(stmt)->subStmt();
    default:
        return nullptr;
    }
}

// Binds the declarations of a block item that are declared before `location`. A declaration may be labeled (C23,
// GCC).
void bindDeclarationsBefore(Scope& scope, const ast::Stmt* item, SourceLocation location) {
    while (const ast::Stmt* labeled = labeledStatement(item)) {
        item = labeled;
    }
    if (const auto* declaration = ast::dyn_cast<ast::DeclarationStmt>(item)) {
        for (ast::Decl* decl : declaration->decls()) {
            if (isBefore(decl->location(), location)) {
                bindExisting(scope, decl);
            }
        }
    }
}

// Pushes the block scopes visible at `location` in `function` (C17 6.2.1p4), outermost first: the parameters, then
// each block of the body that contains the location, with its declarations before the location. Only the one
// statement that contains the location is entered at each level, so this is a loop: `else if` chains can be
// arbitrarily deep. Blocks inside statement expressions are not entered.
void pushFunctionScopes(std::vector<Scope>& scopes, const ast::FunctionDecl& function, SourceLocation location) {
    scopes.emplace_back();
    for (ast::VarDecl* parameter : function.parameters()) {
        bindExisting(scopes.back(), parameter);
    }
    const ast::Stmt* stmt = function.body();
    while (stmt != nullptr && contains(stmt->range(), location)) {
        const ast::Stmt* inner = nullptr;
        switch (stmt->kind()) {
        case ast::StmtKind::Compound:
            scopes.emplace_back();
            for (const ast::Stmt* item : ast::cast<ast::CompoundStmt>(stmt)->body()) {
                if (isBefore(location, item->location())) {
                    break;
                }
                bindDeclarationsBefore(scopes.back(), item, location);
                if (contains(item->range(), location)) {
                    inner = item;
                    break;
                }
            }
            break;
        case ast::StmtKind::For: {
            // A declaration in the first clause is scoped to the loop (C17 6.8.5p5).
            const auto* forStmt = ast::cast<ast::ForStmt>(stmt);
            scopes.emplace_back();
            if (forStmt->init() != nullptr) {
                bindDeclarationsBefore(scopes.back(), forStmt->init(), location);
            }
            inner = forStmt->body();
            break;
        }
        case ast::StmtKind::If: {
            const auto* ifStmt = ast::cast<ast::IfStmt>(stmt);
            inner = contains(ifStmt->thenStmt()->range(), location) ? ifStmt->thenStmt() : ifStmt->elseStmt();
            break;
        }
        case ast::StmtKind::While:
            inner = ast::cast<ast::WhileStmt>(stmt)->body();
            break;
        case ast::StmtKind::DoWhile:
            inner = ast::cast<ast::DoWhileStmt>(stmt)->body();
            break;
        case ast::StmtKind::Switch:
            inner = ast::cast<ast::SwitchStmt>(stmt)->body();
            break;
        default:
            inner = labeledStatement(stmt);
            break;
        }
        stmt = inner;
    }
}

} // namespace

ast::Expr* Parser::parseConstraint(const ConstraintScope& scope) {
    inConstraint_ = true;
    maxDepth_ = maxConstraintNestingDepth;
    // All file-scope declarations are visible, also those after the location: a witness may name a global that is
    // declared later, which is harmless.
    for (ast::Decl* decl : scope.unit->decls) {
        bindExisting(scopes_.front(), decl);
    }
    if (scope.function != nullptr) {
        pushFunctionScopes(scopes_, *scope.function, scope.location);
    }
    // The constraint's own scope: `struct S { ... }` in a cast declares a new tag instead of completing the program's.
    pushScope(ScopeKind::Block);

    skipPragmas();
    // An assignment-expression (C17 6.5.16): a comma at the top level ends the constraint, which is an error.
    ast::Expr* expr = parseAssignmentExpression();
    if (!at(TokenKind::EndOfFile)) {
        failExpected("end of constraint");
    }
    return expr;
}

// A constraint is evaluated while the program is paused at a waypoint, so it must not change the program's state.
void Parser::rejectInConstraint(SourceLocation location, std::string_view construct) {
    if (inConstraint_) {
        fail(location, std::format("a constraint cannot contain {}", construct));
    }
}

namespace {

// Lexes a constraint. The lexer reports every backslash as a stray character; `\result` becomes one identifier token
// named resultName instead, and any other backslash is reported here. Returns nullopt after reporting an error.
std::optional<std::vector<lex::Token>> lexConstraint(FileId file, SourceManager& sources, StringInterner& interner,
                                                     DiagnosticEngine& diagnostics) {
    DiagnosticEngine lexerDiagnostics(sources);
    lex::LexResult lexed = lex::lexFile(sources, file, interner, lexerDiagnostics);
    const auto isBackslash = [&](const lex::Token& token) {
        return token.is(TokenKind::Unknown) && lex::spellingOf(sources, token) == "\\";
    };

    std::vector<std::uint32_t> backslashes; // offsets, in increasing order
    for (const lex::Token& token : lexed.tokens) {
        if (isBackslash(token)) {
            backslashes.push_back(token.location.offset);
        }
    }
    bool failed = false;
    for (const Diagnostic& diagnostic : lexerDiagnostics.diagnostics()) {
        if (diagnostic.severity == Severity::Error
            && std::ranges::binary_search(backslashes, diagnostic.location.offset)) {
            continue;
        }
        diagnostics.report(diagnostic.severity, diagnostic.location, diagnostic.message);
        failed = failed || diagnostic.severity == Severity::Error;
    }
    if (failed) {
        return std::nullopt;
    }
    if (lexed.needsPreprocessing) {
        // The end-of-file token is at the directive.
        diagnostics.error(lexed.tokens.back().location, "a constraint cannot contain preprocessing directives");
        return std::nullopt;
    }

    std::vector<lex::Token> tokens;
    tokens.reserve(lexed.tokens.size());
    for (std::size_t i = 0; i < lexed.tokens.size(); ++i) {
        const lex::Token& token = lexed.tokens[i];
        if (!isBackslash(token)) {
            tokens.push_back(token);
            continue;
        }
        // The last token is EndOfFile, so a backslash has a successor.
        const lex::Token& next = lexed.tokens[i + 1];
        const bool isAdjacentName = next.is(TokenKind::Identifier) && next.location.offset == token.location.offset + 1;
        if (!isAdjacentName) {
            diagnostics.error(token.location, "stray '\\' in constraint");
            return std::nullopt;
        }
        if (next.identifier.text() != "result") {
            diagnostics.error(token.location, std::format("'\\{}' is not supported in a constraint, only '\\result'",
                                                          next.identifier.text()));
            return std::nullopt;
        }
        lex::Token result = token;
        result.kind = TokenKind::Identifier;
        result.length += next.length;
        result.identifier = interner.intern(resultName);
        tokens.push_back(result);
        ++i;
    }
    return tokens;
}

} // namespace

} // namespace detail

ast::Expr* parseConstraint(std::string_view text, const ConstraintScope& scope, ast::AstContext& context,
                           SourceManager& sources, DiagnosticEngine& diagnostics) {
    NITWIT_ASSERT(scope.unit != nullptr, "a constraint needs the translation unit");
    const FileId file = sources.addFile("<constraint>", std::string(text));
    const std::optional<std::vector<lex::Token>> tokens =
        detail::lexConstraint(file, sources, context.interner(), diagnostics);
    if (!tokens) {
        return nullptr;
    }
    try {
        detail::Parser parser(*tokens, context, sources, diagnostics);
        return parser.parseConstraint(scope);
    } catch (const detail::ParseError&) {
        return nullptr;
    }
}

} // namespace nitwit::parse
