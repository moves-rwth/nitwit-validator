#pragma once

// The refutation of correctness witnesses (validate/README.md, "Correctness witnesses"): a run of the program that
// calls the error function shows that the program violates unreach-call, so no correctness witness of it is valid.
// The runs follow no witness: inputs get the defaults, the guards of the program are searched for, and the retry
// gives inputs 1, as in a validation of a witness without waypoints.

#include "support/diagnostics.hpp"
#include "validate/validator.hpp"

namespace nitwit::validate {

/// Looks for a run that calls the error function. Verdict::Confirmed if one does (the program is refuted, the message
/// names the call), Verdict::Unknown if it does so only after undefined behaviour, else the verdict of the final run,
/// as validate() decides it. `options.confirmingRun`, if set,
/// keeps the run that called the error function, which can be written as a concrete violation witness.
[[nodiscard]] ValidationResult refute(const ProgramInput& program, const ValidationOptions& options,
                                      DiagnosticEngine& diagnostics);

} // namespace nitwit::validate
