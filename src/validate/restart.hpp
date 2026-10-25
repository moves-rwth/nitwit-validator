#pragma once

// The restart loop of a validation (architecture.md, "Restarts"): runs the program with the monitor and the oracle,
// fixes the inputs the search found for a waypoint without its outcome, and runs again until a run decides the
// verdict; where no waypoint failed, once more with other defaults (validate/README.md, "Defaults"). The run itself is
// a parameter, so that tests can script it.

#include <cstddef>
#include <cstdint>
#include <functional>

#include "interp/interpreter.hpp"
#include "interp/observer.hpp"
#include "interp/oracle.hpp"
#include "validate/validator.hpp"

namespace nitwit::validate {

/// One complete run of the program from `main` with this oracle and observer.
using RunFunction = std::function<interp::RunResult(interp::InputOracle&, interp::ExecutionObserver&)>;

/// The number of restarts of each phase for a witness with `waypoints` waypoints (validate/README.md, "Search"): 16
/// plus 2 per waypoint, and at most `options.maxRestarts`.
[[nodiscard]] std::uint32_t restartLimit(const ValidationOptions& options, std::size_t waypoints);

/// validate() with the runs made by `run`.
[[nodiscard]] ValidationResult validateWithRuns(const witness::YamlWitness& witness, const ProgramInput& program,
                                                const ValidationOptions& options, DiagnosticEngine& diagnostics,
                                                const RunFunction& run);

} // namespace nitwit::validate
