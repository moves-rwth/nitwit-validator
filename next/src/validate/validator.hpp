#pragma once

// Validation of a violation witness: runs the program with a monitor of the witness as observer and input oracle,
// restarts with fixed inputs where a constraint asks for a value, and decides the verdict.

#include <cstdint>
#include <memory>
#include <ostream>
#include <string>

#include "ast/context.hpp"
#include "ast/decl.hpp"
#include "interp/interpreter.hpp"
#include "support/diagnostics.hpp"
#include "support/source_manager.hpp"
#include "witness/model.hpp"

namespace nitwit::validate {

/// The analyzed program and where it came from.
struct ProgramInput {
    SourceManager& sources;
    FileId file;
    /// For a program that went through the external preprocessor: the original program, and `file` is the
    /// preprocessor's output. Waypoint lines then refer to presumed lines of the original file, whose lines also bound
    /// the columns (architecture.md, "Source locations"). FileId::None otherwise.
    FileId originalFile = FileId::None;
    ast::AstContext& context;
    const ast::TranslationUnit& unit;
    std::shared_ptr<const interp::Program> program;
};

struct ValidationOptions {
    std::string errorFunction = "reach_error";
    interp::RunOptions run;
    std::uint32_t maxRestarts = 1000;
    /// The value of inputs that are neither fixed nor hinted, converted to the requested type (pointers get null).
    std::int64_t defaultInput = 0;
    /// Progress of the validation (segments reached, restarts, why a run ended); null for none.
    std::ostream* log = nullptr;
};

enum class Verdict : std::uint8_t {
    Confirmed,          // the error function was called at the target, in the target segment
    LeftWitness,        // a follow waypoint failed or an avoid waypoint passed
    ProgramFinished,    // main returned or exit was called without reaching the error function
    ErrorBeforeTarget,  // the error function was called before the target segment was reached, or not at the target
    TargetWithoutError, // the target segment was reached, but the program did not call the error function
    Unsupported,        // the witness or the program uses something NITWIT does not support
    Unknown,            // limits, undefined behaviour, abort, infeasible runs (__VERIFIER_assume)
    OutOfMemory,        // the program exceeded the memory limit
};

struct ValidationResult {
    Verdict verdict = Verdict::Unknown;
    std::string message; // human-readable reason, printed by the driver
    std::uint32_t runs = 0;
    std::uint32_t segmentsReached = 0; // segments whose follow waypoint passed in the final run
};

/// The exit code of docs/exit-codes.md.
[[nodiscard]] int exitCode(const ValidationResult& result);

/// Validates a witness in format 2.x. A witness without exactly one violation sequence, or with features NITWIT does
/// not support, gives Unsupported. Constraint errors (unknown names, syntax) are reported to `diagnostics` and make
/// the verdict Unsupported.
[[nodiscard]] ValidationResult validate(const witness::YamlWitness& witness, const ProgramInput& program,
                                        const ValidationOptions& options, DiagnosticEngine& diagnostics);

} // namespace nitwit::validate
