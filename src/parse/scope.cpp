// Scopes and name binding (C17 6.2.1, 6.2.2, 6.2.3), labels, and implicit function declarations.

#include <format>
#include <string_view>

#include "ast/casting.hpp"
#include "parse/parser_impl.hpp"

namespace nitwit::parse::detail {

void Parser::pushScope(ScopeKind kind) {
    scopes_.emplace_back().kind = kind;
}

void Parser::popScope() {
    if (scopes_.back().localLabels) {
        resolveGotos(*scopes_.back().localLabels);
    }
    scopes_.pop_back();
}

ast::Decl* Parser::lookupOrdinary(Symbol name) const {
    for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
        if (const auto found = scope->ordinary.find(name); found != scope->ordinary.end()) {
            return found->second;
        }
    }
    return nullptr;
}

bool Parser::isTypedefName(const lex::Token& token) const {
    if (!token.is(TokenKind::Identifier)) {
        return false;
    }
    const ast::Decl* decl = lookupOrdinary(token.identifier);
    return decl != nullptr && ast::isa<ast::TypedefDecl>(decl);
}

ast::Decl* Parser::lookupTag(Symbol name, bool currentScopeOnly) const {
    for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
        if (const auto found = scope->tags.find(name); found != scope->tags.end()) {
            return found->second;
        }
        if (currentScopeOnly) {
            break;
        }
    }
    return nullptr;
}

void Parser::bindTag(ast::Decl* tag) {
    scopes_.back().tags[tag->name()] = tag;
}

namespace {

bool hasExternStorage(const ast::Decl* decl) {
    const auto* var = ast::dyn_cast<ast::VarDecl>(decl);
    return var != nullptr && var->storage() == ast::StorageClass::Extern;
}

// Links a redeclaration to the previous declaration of the same entity.
void linkPrevious(ast::Decl* previous, ast::Decl* decl) {
    if (auto* var = ast::dyn_cast<ast::VarDecl>(decl)) {
        var->setPrevious(ast::cast<ast::VarDecl>(previous));
    } else if (auto* function = ast::dyn_cast<ast::FunctionDecl>(decl)) {
        function->setPrevious(ast::cast<ast::FunctionDecl>(previous));
    }
}

} // namespace

void Parser::bindOrdinary(ast::Decl* decl, bool hasLinkage) {
    const Symbol name = decl->name();
    if (name.empty()) {
        return;
    }
    Scope& scope = scopes_.back();
    ast::Decl* previous = nullptr;
    if (const auto found = scope.ordinary.find(name); found != scope.ordinary.end()) {
        previous = found->second;
        if (previous->kind() != decl->kind()) {
            fail(decl->location(), std::format("redefinition of '{}' as a different kind of symbol", name.text()));
        }
        // C17 6.7p3: only typedefs (C11) and entities with linkage may be declared twice in the same scope. Objects
        // at file scope have linkage; a block-scope object only with `extern`.
        const bool redeclarable = ast::isa<ast::TypedefDecl>(decl) || ast::isa<ast::FunctionDecl>(decl)
                                  || (scope.kind == ScopeKind::File && ast::isa<ast::VarDecl>(decl))
                                  || (hasExternStorage(previous) && hasExternStorage(decl));
        if (!redeclarable) {
            fail(decl->location(), std::format("redefinition of '{}'", name.text()));
        }
    } else if (hasLinkage) {
        // C17 6.2.2p4: an `extern` declaration in a block refers to the entity with linkage of the same name.
        if (const auto external = externals_.find(name); external != externals_.end()) {
            previous = external->second;
            if (previous->kind() != decl->kind()) {
                fail(decl->location(), std::format("redeclaration of '{}' as a different kind of symbol", name.text()));
            }
        }
    }
    if (previous != nullptr && !ast::isa<ast::TypedefDecl>(decl)) {
        linkPrevious(previous, decl);
    }
    if (hasLinkage) {
        externals_[name] = ast::cast<ast::ValueDecl>(decl);
    }
    scope.ordinary[name] = decl;
}

void Parser::declarePredefinedTypedef(std::string_view name, ast::BuiltinKind kind) {
    auto* decl = context_.create<ast::TypedefDecl>(intern(name), SourceLocation{}, context_.builtin(kind));
    scopes_.front().ordinary[decl->name()] = decl;
}

// C89 6.3.2.2: a call of an undeclared identifier declares `extern int f();`. GCC still accepts it with a warning.
ast::FunctionDecl* Parser::declareImplicitFunction(const lex::Token& name) {
    const ast::QualType type = context_.functionType(context_.builtin(ast::BuiltinKind::Int), {}, false, false);
    auto* function =
        context_.create<ast::FunctionDecl>(name.identifier, name.location, type, ast::StorageClass::Extern);
    function->setImplicit();
    // GCC knows its builtins without a declaration; sema gives them their real types.
    const std::string_view text = name.identifier.text();
    if (!text.starts_with("__builtin_") && !text.starts_with("__atomic_") && !text.starts_with("__sync_")) {
        warning(name.location, std::format("implicit declaration of function '{}'", name.identifier.text()));
    }
    if (const auto found = externals_.find(name.identifier); found != externals_.end()) {
        if (auto* previous = ast::dyn_cast<ast::FunctionDecl>(found->second)) {
            function->setPrevious(previous);
        }
    }
    // Bound at file scope, so that later calls and declarations in the file refer to the same function.
    externals_[name.identifier] = function;
    scopes_.front().ordinary[name.identifier] = function;
    implicitDecls_.push_back(function);
    return function;
}

LabelScope& Parser::labelScopeFor(Symbol name) {
    for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
        if (scope->localLabels && scope->localLabels->labels.contains(name)) {
            return *scope->localLabels;
        }
    }
    return functionLabels_;
}

void Parser::defineLabel(ast::LabelStmt* label) {
    LabelScope& scope = labelScopeFor(label->name());
    const auto [found, inserted] = scope.labels.try_emplace(label->name(), label);
    if (!inserted) {
        if (found->second != nullptr) {
            fail(label->location(), std::format("redefinition of label '{}'", label->name().text()));
        }
        found->second = label;
    }
}

void Parser::resolveGotos(LabelScope& scope) {
    for (ast::GotoStmt* gotoStmt : scope.gotos) {
        const auto found = scope.labels.find(gotoStmt->label());
        if (found == scope.labels.end() || found->second == nullptr) {
            fail(gotoStmt->location(), std::format("use of undeclared label '{}'", gotoStmt->label().text()));
        }
        gotoStmt->setTarget(found->second);
    }
}

} // namespace nitwit::parse::detail
