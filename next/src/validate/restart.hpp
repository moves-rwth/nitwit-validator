#pragma once

// The restart loop of a validation (architecture.md, "Restarts"): runs the program with the monitor and the oracle,
// fixes the inputs a failed follow waypoint asks for, and runs again until a run decides the verdict. The run itself
// is a parameter, so that tests can script it.

#include <functional>

#include "interp/interpreter.hpp"
#include "interp/observer.hpp"
#include "interp/oracle.hpp"
#include "validate/validator.hpp"

namespace nitwit::validate {

/// One complete run of the program from `main` with this oracle and observer.
using RunFunction = std::function<interp::RunResult(interp::InputOracle&, interp::ExecutionObserver&)>;

/// validate() with the runs made by `run`.
[[nodiscard]] ValidationResult validateWithRuns(const witness::YamlWitness& witness, const ProgramInput& program,
                                                const ValidationOptions& options, DiagnosticEngine& diagnostics,
                                                const RunFunction& run);

} // namespace nitwit::validate
