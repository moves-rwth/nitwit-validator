#pragma once

// Reads a C program and runs the front end on it: lexing (through the system preprocessor if the file needs it),
// parsing, semantic analysis and lowering.

#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "ast/context.hpp"
#include "ast/decl.hpp"
#include "interp/interpreter.hpp"
#include "lex/token.hpp"
#include "support/diagnostics.hpp"
#include "support/interner.hpp"
#include "support/source_manager.hpp"
#include "support/target.hpp"

namespace nitwit::driver {

/// A program after the front end, with everything its AST refers to.
struct LoadedProgram {
    explicit LoadedProgram(DataModel model) : target(TargetInfo::forDataModel(model)), context(target, interner) {}

    SourceManager sources;
    StringInterner interner;
    TargetInfo target;
    ast::AstContext context;
    std::vector<lex::Token> tokens;
    ast::TranslationUnit* unit = nullptr;
    std::shared_ptr<const interp::Program> program;
};

struct FrontendError {
    enum class Kind : std::uint8_t {
        Unreadable, // the file cannot be read (exit code 3)
        Invalid,    // the preprocessor, lexer, parser, sema or lowering failed (exit code 4)
    };
    Kind kind = Kind::Invalid;
    std::vector<std::string> messages; // formatted diagnostics and other messages
};

/// Loads and analyzes a program for a data model. Recurses deeply on nested input: call it on a large stack.
[[nodiscard]] std::expected<std::unique_ptr<LoadedProgram>, FrontendError>
loadProgram(const std::filesystem::path& file, DataModel model);

/// Runs the system preprocessor (`cpp -m32` or `cpp -m64`) on a file; the output keeps line markers.
[[nodiscard]] std::expected<std::string, std::string> preprocess(const std::filesystem::path& file, DataModel model);

} // namespace nitwit::driver
