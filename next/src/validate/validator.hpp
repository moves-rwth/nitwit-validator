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
    /// Whether `file` is the output of the external preprocessor; then waypoint lines refer to presumed lines of the
    /// original file (architecture.md, "Source locations").
    bool isPreprocessed = false;
    ast::AstContext& context;
    const ast::TranslationUnit& unit;
    std::shared_ptr<const interp::Program> program;
};

struct ValidationOptions {
    std::string errorFunction = "reach_error";
    interp::RunOptions run;
    std::uint32_t maxRestarts = 1000;
    /// Progress of the validation (segments reached, restarts, why a run ended); null for none.
    std::ostream* log = nullptr;
};

enum class Verdict : std::uint8_t {
    Confirmed,          // the error function was called in the target segment
    LeftWitness,        // a follow waypoint failed or an avoid waypoint passed
    ProgramFinished,    // main returned or exit was called without reaching the error function
    ErrorBeforeTarget,  // the error function was called before the target segment was reached
    TargetWithoutError, // the target segment was reached, but the program did not call the error function
    Unsupported,        // the witness or the program uses something NITWIT does not support
    Unknown,            // limits, undefined behaviour, abort, infeasible runs (__VERIFIER_assume)
};

struct ValidationResult {
    Verdict verdict = Verdict::Unknown;
    std::string message; // human-readable reason, printed by the driver
    std::uint32_t runs = 0;
    std::uint32_t segmentsReached = 0; // segments whose follow waypoint passed in the final run
};

/// The exit code of docs/exit-codes.md.
[[nodiscard]] int exitCode(const ValidationResult& result);

/// Validates a witness in format 2.x. Constraint errors (unknown names, syntax) are reported to `diagnostics` and make
/// the verdict Unsupported.
[[nodiscard]] ValidationResult validate(const witness::ViolationSequence& witness, const ProgramInput& program,
                                        const ValidationOptions& options, DiagnosticEngine& diagnostics);

} // namespace nitwit::validate
