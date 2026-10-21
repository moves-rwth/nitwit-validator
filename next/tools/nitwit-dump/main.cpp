// nitwit-dump: prints the tokens or the syntactic AST of a C file, for debugging and golden tests.
// Usage: nitwit-dump [--tokens|--ast] [-32|-64] <file>
// The output goes to stdout, diagnostics to stderr. The exit code is 0 on success, 1 if the file has errors or needs
// the preprocessor, and 2 for a usage error.

#include <cstddef>
#include <cstdio>
#include <optional>
#include <print>
#include <string>
#include <string_view>

#include "ast/context.hpp"
#include "ast/dump.hpp"
#include "lex/lexer.hpp"
#include "parse/parser.hpp"
#include "support/large_stack.hpp"

namespace {

using namespace nitwit;

// The parser recurses on the host stack; deeply nested programs need more than the default 8 MiB.
constexpr std::size_t stackBytes = std::size_t{1} << 30;

struct Options {
    bool tokens = false;
    DataModel dataModel = DataModel::LP64;
    std::string file;
};

std::optional<Options> parseOptions(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument = argv[i];
        if (argument == "--tokens") {
            options.tokens = true;
        } else if (argument == "--ast") {
            options.tokens = false;
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

    if (options.tokens) {
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
    const ast::TranslationUnit* unit = parse::parseTranslationUnit(lexed.tokens, context, sources, diagnostics);
    if (unit != nullptr) {
        std::print("{}", ast::dump(*unit, sources));
    }
    printDiagnostics(diagnostics);
    return unit != nullptr && !diagnostics.hasErrors() ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    const std::optional<Options> options = parseOptions(argc, argv);
    if (!options) {
        std::println(stderr, "usage: nitwit-dump [--tokens|--ast] [-32|-64] <file>");
        return 2;
    }
    int exitCode = 1;
    runWithLargeStack(stackBytes, [&] { exitCode = run(*options); });
    return exitCode;
}
