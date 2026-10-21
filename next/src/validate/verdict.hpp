#pragma once

// The verdict of a validation from its final run (validate/README.md, "Verdict"; docs/exit-codes.md).

#include "interp/interpreter.hpp"
#include "validate/monitor.hpp"
#include "validate/validator.hpp"

namespace nitwit::validate {

/// The verdict of a run that needs no restart: from the monitor's state if it stopped the run, else from how the run
/// ended. Precondition: the monitor does not ask for a restart.
[[nodiscard]] ValidationResult verdictOf(const interp::RunResult& run, const SegmentMonitor& monitor,
                                         const ProgramInput& program, const ValidationOptions& options);

} // namespace nitwit::validate
