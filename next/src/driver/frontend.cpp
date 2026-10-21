#include "driver/frontend.hpp"

#include <format>
#include <utility>

#include "lex/lexer.hpp"
#include "parse/parser.hpp"
#include "sema/sema.hpp"

namespace nitwit::driver {

namespace {

FrontendError invalid(const DiagnosticEngine& diagnostics, std::string fallback) {
    FrontendError error{.kind = FrontendError::Kind::Invalid, .messages = {}};
    for (const Diagnostic& diagnostic : diagnostics.diagnostics()) {
        if (diagnostic.severity == Severity::Error) {
            error.messages.push_back(diagnostics.format(diagnostic));
        }
    }
    if (error.messages.empty()) {
        error.messages.push_back(std::move(fallback));
    }
    return error;
}

} // namespace

std::expected<std::unique_ptr<LoadedProgram>, FrontendError> loadProgram(const std::filesystem::path& file,
                                                                         DataModel model) {
    auto loaded = std::make_unique<LoadedProgram>(model);
    std::expected<FileId, Error> source = loaded->sources.loadFile(file);
    if (!source) {
        return std::unexpected(
            FrontendError{.kind = FrontendError::Kind::Unreadable, .messages = {source.error().message}});
    }
    auto diagnostics = std::make_unique<DiagnosticEngine>(loaded->sources);
    lex::LexResult lexed = lex::lexFile(loaded->sources, *source, loaded->interner, *diagnostics);
    if (lexed.needsPreprocessing) {
        // The lexer's diagnostics so far concern the unpreprocessed text; the preprocessed one is lexed afresh.
        std::expected<std::string, std::string> preprocessed = preprocess(file, model);
        if (!preprocessed) {
            return std::unexpected(
                FrontendError{.kind = FrontendError::Kind::Invalid, .messages = {preprocessed.error()}});
        }
        source = loaded->sources.addFile(file.string(), std::move(*preprocessed));
        diagnostics = std::make_unique<DiagnosticEngine>(loaded->sources);
        lexed = lex::lexFile(loaded->sources, *source, loaded->interner, *diagnostics);
        if (lexed.needsPreprocessing) {
            return std::unexpected(invalid(*diagnostics, "the preprocessed program still has directives"));
        }
    }
    if (diagnostics->hasErrors()) {
        return std::unexpected(invalid(*diagnostics, "the program cannot be lexed"));
    }
    loaded->tokens = std::move(lexed.tokens);
    loaded->unit = parse::parseTranslationUnit(loaded->tokens, loaded->context, loaded->sources, *diagnostics);
    if (loaded->unit == nullptr) {
        return std::unexpected(invalid(*diagnostics, "the program cannot be parsed"));
    }
    if (!sema::analyze(*loaded->unit, loaded->context, *diagnostics)) {
        return std::unexpected(invalid(*diagnostics, "the program is not valid C"));
    }
    std::expected<std::shared_ptr<const interp::Program>, Error> program =
        interp::buildProgram(*loaded->unit, loaded->context);
    if (!program) {
        return std::unexpected(
            FrontendError{.kind = FrontendError::Kind::Invalid, .messages = {program.error().message}});
    }
    loaded->program = std::move(*program);
    return loaded;
}

} // namespace nitwit::driver
