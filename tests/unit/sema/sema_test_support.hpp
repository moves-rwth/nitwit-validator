#pragma once

// A fixture for sema tests: parses and analyzes C text for a data model, and finds and dumps parts of the result.

#include <cstddef>
#include <format>
#include <memory>
#include <regex>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "ast/casting.hpp"
#include "ast/context.hpp"
#include "ast/dump.hpp"
#include "lex/lexer.hpp"
#include "parse/parser.hpp"
#include "sema/sema.hpp"
#include "sema/types.hpp"

namespace nitwit::sema {

class SemaTest : public ::testing::Test {
protected:
    // Parses and analyzes `text` as the file "test.c". Null if the parser or sema reported an error.
    ast::TranslationUnit* analyze(std::string text, DataModel model = DataModel::LP64) {
        firstDiagnostic_ = diagnostics_.diagnostics().size();
        target_ = std::make_unique<TargetInfo>(TargetInfo::forDataModel(model));
        context_ = std::make_unique<ast::AstContext>(*target_, interner_);
        const FileId file = sources_.addFile("test.c", std::move(text));
        lex::LexResult lexed = lex::lexFile(sources_, file, interner_, diagnostics_);
        tokens_.push_back(std::move(lexed.tokens));
        ast::TranslationUnit* unit = parse::parseTranslationUnit(tokens_.back(), *context_, sources_, diagnostics_);
        if (unit == nullptr || !sema::analyze(*unit, *context_, diagnostics_)) {
            return nullptr;
        }
        return unit;
    }

    // The dump of `text`, which must be valid, without the positions of the nodes.
    std::string dumpOf(std::string text, DataModel model = DataModel::LP64) {
        const ast::TranslationUnit* unit = analyze(std::move(text), model);
        EXPECT_NE(unit, nullptr) << firstError();
        return unit != nullptr ? withoutPositions(ast::dump(*unit, sources_)) : "";
    }

    // The dump of the expression statement `expression;` in a function after `declarations`, without positions.
    std::string dumpExpr(std::string_view declarations, std::string_view expression,
                         DataModel model = DataModel::LP64) {
        const ast::TranslationUnit* unit =
            analyze(std::format("{}\nvoid test(void) {{\n{};\n}}\n", declarations, expression), model);
        EXPECT_NE(unit, nullptr) << firstError();
        if (unit == nullptr) {
            return "";
        }
        return withoutPositions(ast::dump(statementExpr(unit), sources_));
    }

    // The type of `expression` in a function after `declarations`, as the dump spells it, with " lvalue" for lvalues.
    std::string typeOf(std::string_view declarations, std::string_view expression, DataModel model = DataModel::LP64) {
        const ast::TranslationUnit* unit =
            analyze(std::format("{}\nvoid test(void) {{\n{};\n}}\n", declarations, expression), model);
        EXPECT_NE(unit, nullptr) << firstError();
        if (unit == nullptr) {
            return "";
        }
        const ast::Expr* expr = statementExpr(unit);
        return ast::typeToString(expr->type()) + (expr->isLValue() ? " lvalue" : "");
    }

    // The value of the integer constant expression `expression` after `declarations`, through an enumerator.
    Int128 constantOf(std::string_view declarations, std::string_view expression, DataModel model = DataModel::LP64) {
        const ast::TranslationUnit* unit =
            analyze(std::format("{}\nenum {{ probe = {} }};\n", declarations, expression), model);
        EXPECT_NE(unit, nullptr) << firstError();
        if (unit == nullptr) {
            return -999999;
        }
        return find<ast::EnumConstantDecl>(unit, "probe")->value();
    }

    // "size S align A" and per field "name@offset" (bit-fields "name@offset.bit"), for the record named `name`.
    std::string layoutText(std::string text, std::string_view name, DataModel model = DataModel::LP64) {
        const ast::TranslationUnit* unit = analyze(std::move(text), model);
        EXPECT_NE(unit, nullptr) << firstError();
        if (unit == nullptr) {
            return "";
        }
        const ast::RecordDecl* record = findRecord(unit, name);
        if (record == nullptr || record->layout() == nullptr) {
            ADD_FAILURE() << "no laid-out record " << name;
            return "";
        }
        const ast::RecordLayout& layout = *record->layout();
        std::string text2 = std::format("size {} align {}", layout.size, layout.alignment);
        for (const ast::FieldDecl* field : record->fields()) {
            const std::string_view fieldName = field->name().empty() ? "_" : field->name().text();
            text2 += std::format(" {}@{}", fieldName, layout.fieldOffsets[field->index()]);
            if (field->isBitField()) {
                text2 += std::format(".{}", layout.bitOffsets[field->index()]);
            }
        }
        return text2;
    }

    // The last top-level declaration named `name`, which must be a T; enumeration constants are searched in the
    // enumerations of the translation unit.
    template <typename T>
    static T* find(const ast::TranslationUnit* unit, std::string_view name) {
        T* found = nullptr;
        for (ast::Decl* decl : unit->decls) {
            if (decl->name().text() == name && ast::dyn_cast<T>(decl) != nullptr) {
                found = ast::dyn_cast<T>(decl);
            }
            if (const auto* enumDecl = ast::dyn_cast<ast::EnumDecl>(decl)) {
                for (ast::Decl* constant : enumDecl->constants()) {
                    if (constant->name().text() == name && ast::dyn_cast<T>(constant) != nullptr) {
                        found = ast::dyn_cast<T>(constant);
                    }
                }
            }
        }
        EXPECT_NE(found, nullptr) << "no declaration " << name;
        return found;
    }

    // The record with the tag `name`, or the record a typedef named `name` stands for.
    const ast::RecordDecl* findRecord(const ast::TranslationUnit* unit, std::string_view name) {
        for (const ast::Decl* decl : unit->decls) {
            if (const auto* record = ast::dyn_cast<ast::RecordDecl>(decl);
                record != nullptr && record->name().text() == name) {
                return record;
            }
            if (const auto* typedefDecl = ast::dyn_cast<ast::TypedefDecl>(decl);
                typedefDecl != nullptr && typedefDecl->name().text() == name) {
                const auto* record =
                    ast::dyn_cast<ast::RecordType>(context_->canonical(typedefDecl->underlying()).type());
                return record != nullptr ? record->decl() : nullptr;
            }
        }
        return nullptr;
    }

    // The diagnostics of the last analysis.
    [[nodiscard]] std::span<const Diagnostic> diagnostics() const {
        return diagnostics_.diagnostics().subspan(firstDiagnostic_);
    }

    // The first error of the last analysis as "line:column: message", or "" if there is none.
    [[nodiscard]] std::string firstError() const {
        for (const Diagnostic& diagnostic : diagnostics()) {
            if (diagnostic.severity == Severity::Error) {
                const LineColumn position = sources_.lineColumn(diagnostic.location);
                return std::format("{}:{}: {}", position.line, position.column, diagnostic.message);
            }
        }
        return "";
    }

    // The error of analyzing `text`, which must fail, as "line:column: message".
    std::string errorOf(std::string text, DataModel model = DataModel::LP64) {
        EXPECT_EQ(analyze(std::move(text), model), nullptr);
        return firstError();
    }

    // The warnings of the last analysis, without positions.
    [[nodiscard]] std::vector<std::string> warnings() const {
        std::vector<std::string> messages;
        for (const Diagnostic& diagnostic : diagnostics()) {
            if (diagnostic.severity == Severity::Warning) {
                messages.push_back(diagnostic.message);
            }
        }
        return messages;
    }

    [[nodiscard]] bool hasWarning(std::string_view message) const {
        for (const std::string& warning : warnings()) {
            if (warning.contains(message)) {
                return true;
            }
        }
        return false;
    }

    static std::string withoutPositions(const std::string& dump) {
        static const std::regex positions(" <(\\d+:\\d+|invalid)>");
        return std::regex_replace(dump, positions, "");
    }

    ast::AstContext& context() { return *context_; }

private:
    // The expression of the only statement of the function `test`.
    static const ast::Expr* statementExpr(const ast::TranslationUnit* unit) {
        const auto* function = ast::cast<ast::FunctionDecl>(unit->decls.back());
        return ast::cast<ast::ExpressionStmt>(function->body()->body().front())->expr();
    }

protected:
    SourceManager sources_;
    StringInterner interner_;
    DiagnosticEngine diagnostics_{sources_};
    std::unique_ptr<TargetInfo> target_;
    std::unique_ptr<ast::AstContext> context_;
    std::vector<std::vector<lex::Token>> tokens_;
    std::size_t firstDiagnostic_ = 0;
};

} // namespace nitwit::sema
