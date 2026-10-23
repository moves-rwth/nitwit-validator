// The entry point of semantic analysis, diagnostics, and the end of the translation unit.

#include "sema/sema.hpp"

#include <format>
#include <unordered_set>
#include <utility>

#include "ast/casting.hpp"
#include "ast/dump.hpp"
#include "sema/expression.hpp"
#include "sema/sema_impl.hpp"
#include "support/assert.hpp"

namespace nitwit::sema {

namespace detail {

Sema::Sema(ast::AstContext& context, DiagnosticEngine& diagnostics) : context_(context), diagnostics_(diagnostics) {}

void Sema::error(SourceLocation location, std::string message) {
    diagnostics_.error(location, std::move(message));
    throw SemaError{};
}

void Sema::warning(SourceLocation location, std::string message) {
    diagnostics_.warning(location, std::move(message));
}

std::string Sema::typeName(ast::QualType type) const {
    return ast::typeToString(type);
}

void Sema::analyzeTranslationUnit(ast::TranslationUnit& unit) {
    for (ast::Decl* decl : unit.decls) {
        declaration(decl);
    }
    completeTentativeDefinitions();
}

// A constraint is checked like a controlling expression (C17 6.8.4.1p1); the declarations it names were analyzed with
// the translation unit.
ast::Expr* Sema::analyzeConstraint(ast::Expr* expr, ast::QualType resultType) {
    resultType_ = resultType;
    return condition(expr, "a constraint");
}

// C17 6.9.2p2: a tentative definition without a later definition becomes a definition with a zero initializer. GCC
// completes an array of unknown size to one element, with a warning; any other incomplete type is an error.
void Sema::completeTentativeDefinitions() {
    std::unordered_set<const ast::VarDecl*> redeclared;
    for (const ast::VarDecl* var : fileScopeVariables_) {
        if (var->previous() != nullptr) {
            redeclared.insert(var->previous());
        }
    }
    for (ast::VarDecl* last : fileScopeVariables_) {
        if (redeclared.contains(last)) {
            continue; // only the last declaration of an object has the composite type of all of them
        }
        bool isDefined = false;
        for (const ast::VarDecl* var = last; var != nullptr; var = var->previous()) {
            isDefined = isDefined || var->storage() != ast::StorageClass::Extern || var->initializer() != nullptr;
        }
        if (!isDefined) {
            continue;
        }
        const ast::QualType type = context_.canonical(last->type());
        if (const auto* array = ast::dyn_cast<ast::ArrayType>(type.type());
            array != nullptr && array->sizeKind() == ast::ArrayType::SizeKind::Incomplete) {
            warning(last->location(), std::format("array '{}' assumed to have one element", last->name().text()));
            last->setType(context_.constantArrayOf(array->element(), 1));
            continue;
        }
        if (!isComplete(last->type())) {
            error(last->location(), std::format("storage size of '{}' isn't known", last->name().text()));
        }
    }
}

} // namespace detail

bool analyze(ast::TranslationUnit& unit, ast::AstContext& context, DiagnosticEngine& diagnostics) {
    try {
        detail::Sema sema(context, diagnostics);
        sema.analyzeTranslationUnit(unit);
        return true;
    } catch (const detail::SemaError&) {
        return false;
    }
}

ast::Expr* analyzeConstraint(ast::Expr* expr, ast::QualType resultType, ast::AstContext& context,
                             DiagnosticEngine& diagnostics) {
    try {
        detail::Sema sema(context, diagnostics);
        return sema.analyzeConstraint(expr, resultType);
    } catch (const detail::SemaError&) {
        return nullptr;
    }
}

} // namespace nitwit::sema
