// nitwit-dump: prints the tokens, the syntactic AST or the analyzed AST of a C file, for debugging and golden tests.
// Usage: nitwit-dump [--tokens|--ast|--sema] [-32|-64] <file>
// The output goes to stdout, diagnostics to stderr. The exit code is 0 on success, 1 if the file has errors or needs
// the preprocessor, and 2 for a usage error.

#include <cstddef>
#include <cstdio>
#include <optional>
#include <print>
#include <string>
#include <string_view>

#include "ast/casting.hpp"
#include "ast/context.hpp"
#include "ast/dump.hpp"
#include "lex/lexer.hpp"
#include "parse/parser.hpp"
#include "sema/sema.hpp"
#include "support/large_stack.hpp"

namespace {

using namespace nitwit;

// The parser recurses on the host stack; deeply nested programs need more than the default 8 MiB.
constexpr std::size_t stackBytes = std::size_t{1} << 30;

enum class Mode : std::uint8_t { Tokens, Ast, Sema };

struct Options {
    Mode mode = Mode::Ast;
    DataModel dataModel = DataModel::LP64;
    std::string file;
};

std::optional<Options> parseOptions(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument = argv[i];
        if (argument == "--tokens") {
            options.mode = Mode::Tokens;
        } else if (argument == "--ast") {
            options.mode = Mode::Ast;
        } else if (argument == "--sema") {
            options.mode = Mode::Sema;
        } else if (argument == "-32") {
            options.dataModel = DataModel::ILP32;
        } else if (argument == "-64") {
            options.dataModel = DataModel::LP64;
        } else if (options.file.empty() && !argument.starts_with("-")) {
            options.file = argument;
        } else {
            return std::nullopt;
        }
    }
    if (options.file.empty()) {
        return std::nullopt;
    }
    return options;
}

void printDiagnostics(const DiagnosticEngine& diagnostics) {
    for (const Diagnostic& diagnostic : diagnostics.diagnostics()) {
        std::println(stderr, "{}", diagnostics.format(diagnostic));
    }
}

// The locals of every function definition with their indices, which the tree dump does not show.
void printLocals(const ast::TranslationUnit& unit, const SourceManager& sources) {
    for (const ast::Decl* decl : unit.decls) {
        const auto* function = ast::dyn_cast<ast::FunctionDecl>(decl);
        if (function == nullptr || function->body() == nullptr) {
            continue;
        }
        std::print("Locals of {}:", function->name().text());
        for (const ast::VarDecl* local : function->locals()) {
            const LineColumn position = sources.lineColumn(local->location());
            std::print(" {}={}<{}:{}>", local->localIndex(), local->name().empty() ? "<unnamed>" : local->name().text(),
                       position.line, position.column);
        }
        std::println("");
    }
}

int run(const Options& options) {
    SourceManager sources;
    const auto file = sources.loadFile(options.file);
    if (!file) {
        std::println(stderr, "nitwit-dump: {}", file.error().message);
        return 1;
    }
    StringInterner interner;
    DiagnosticEngine diagnostics(sources);
    const lex::LexResult lexed = lex::lexFile(sources, *file, interner, diagnostics);
    if (lexed.needsPreprocessing) {
        printDiagnostics(diagnostics);
        std::println(stderr, "nitwit-dump: {} needs the preprocessor", options.file);
        return 1;
    }

    if (options.mode == Mode::Tokens) {
        for (const lex::Token& token : lexed.tokens) {
            const LineColumn position = sources.lineColumn(token.location);
            std::println("{} '{}' <{}:{}>", lex::spelling(token.kind), lex::spellingOf(sources, token), position.line,
                         position.column);
        }
        printDiagnostics(diagnostics);
        return diagnostics.hasErrors() ? 1 : 0;
    }

    const TargetInfo target = TargetInfo::forDataModel(options.dataModel);
    ast::AstContext context(target, interner);
    ast::TranslationUnit* unit = parse::parseTranslationUnit(lexed.tokens, context, sources, diagnostics);
    if (unit != nullptr && options.mode == Mode::Sema && !sema::analyze(*unit, context, diagnostics)) {
        unit = nullptr; // a partly analyzed tree is not dumped
    }
    if (unit != nullptr) {
        std::print("{}", ast::dump(*unit, sources));
        if (options.mode == Mode::Sema) {
            printLocals(*unit, sources);
        }
    }
    printDiagnostics(diagnostics);
    return unit != nullptr && !diagnostics.hasErrors() ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    const std::optional<Options> options = parseOptions(argc, argv);
    if (!options) {
        std::println(stderr, "usage: nitwit-dump [--tokens|--ast|--sema] [-32|-64] <file>");
        return 2;
    }
    int exitCode = 1;
    runWithLargeStack(stackBytes, [&] { exitCode = run(*options); });
    return exitCode;
}
