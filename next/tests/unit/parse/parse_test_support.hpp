#pragma once

// A fixture for parser tests: parses C text, and finds and dumps parts of the result.

#include <cstddef>
#include <format>
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

namespace nitwit::parse {

class ParserTest : public ::testing::Test {
protected:
    // Parses `text` as the file "test.c". Null if the parser reported an error.
    ast::TranslationUnit* parse(std::string text) {
        firstDiagnostic_ = diagnostics_.diagnostics().size();
        file_ = sources_.addFile("test.c", std::move(text));
        lex::LexResult lexed = lex::lexFile(sources_, file_, interner_, diagnostics_);
        tokens_ = std::move(lexed.tokens);
        return parseTranslationUnit(tokens_, context_, sources_, diagnostics_);
    }

    // Parses `text`, which must be valid, and returns the dump without the positions of the nodes.
    std::string dumpOf(std::string text) {
        const ast::TranslationUnit* unit = parse(std::move(text));
        EXPECT_NE(unit, nullptr) << firstError();
        return unit != nullptr ? withoutPositions(ast::dump(*unit, sources_)) : "";
    }

    // The dump of the expression statement `expression;` in a function after `declarations`, without positions.
    std::string dumpExpr(std::string_view declarations, std::string_view expression) {
        const ast::TranslationUnit* unit =
            parse(std::format("{}\nvoid test(void) {{\n{};\n}}\n", declarations, expression));
        EXPECT_NE(unit, nullptr) << firstError();
        if (unit == nullptr) {
            return "";
        }
        const auto* function = ast::cast<ast::FunctionDecl>(unit->decls.back());
        const auto* statement = ast::cast<ast::ExpressionStmt>(function->body()->body().front());
        return withoutPositions(ast::dump(statement->expr(), sources_));
    }

    // The statements of the body of function `name`.
    static std::span<ast::Stmt* const> bodyOf(const ast::TranslationUnit* unit, std::string_view name) {
        return find<ast::FunctionDecl>(unit, name)->body()->body();
    }

    // The last top-level declaration named `name`, which must be a T.
    template <typename T>
    static T* find(const ast::TranslationUnit* unit, std::string_view name) {
        T* found = nullptr;
        for (ast::Decl* decl : unit->decls) {
            if (decl->name().text() == name) {
                found = ast::dyn_cast<T>(decl);
            }
        }
        EXPECT_NE(found, nullptr) << "no declaration " << name;
        return found;
    }

    // "<line>:<column>" of a location.
    [[nodiscard]] std::string position(SourceLocation location) const {
        const LineColumn position = sources_.lineColumn(location);
        return std::format("{}:{}", position.line, position.column);
    }

    // The diagnostics of the last parse.
    [[nodiscard]] std::span<const Diagnostic> diagnostics() const {
        return diagnostics_.diagnostics().subspan(firstDiagnostic_);
    }

    [[nodiscard]] std::vector<std::string> messages() const {
        std::vector<std::string> messages;
        for (const Diagnostic& diagnostic : diagnostics()) {
            messages.push_back(diagnostics_.format(diagnostic));
        }
        return messages;
    }

    // The first error of the last parse, formatted as "test.c:line:column: error: message", or "" if there is none.
    [[nodiscard]] std::string firstError() const {
        for (const Diagnostic& diagnostic : diagnostics()) {
            if (diagnostic.severity == Severity::Error) {
                return diagnostics_.format(diagnostic);
            }
        }
        return "";
    }

    // The error of parsing `text`, which must fail.
    std::string errorOf(std::string text) {
        EXPECT_EQ(parse(std::move(text)), nullptr);
        return firstError();
    }

    [[nodiscard]] bool hasWarning(std::string_view message) const {
        for (const Diagnostic& diagnostic : diagnostics()) {
            if (diagnostic.severity == Severity::Warning && diagnostic.message == message) {
                return true;
            }
        }
        return false;
    }

    static std::string withoutPositions(const std::string& dump) {
        static const std::regex positions(" <(\\d+:\\d+|invalid)>");
        return std::regex_replace(dump, positions, "");
    }

    SourceManager sources_;
    StringInterner interner_;
    DiagnosticEngine diagnostics_{sources_};
    TargetInfo target_ = TargetInfo::forDataModel(DataModel::LP64);
    ast::AstContext context_{target_, interner_};
    FileId file_ = FileId::None;
    std::vector<lex::Token> tokens_;
    std::size_t firstDiagnostic_ = 0;
};

} // namespace nitwit::parse
