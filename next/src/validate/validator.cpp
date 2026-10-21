#include "validate/validator.hpp"

#include "support/assert.hpp"
#include "validate/restart.hpp"

namespace nitwit::validate {

ValidationResult validate(const witness::YamlWitness& witness, const ProgramInput& program,
                          const ValidationOptions& options, DiagnosticEngine& diagnostics) {
    NITWIT_ASSERT(program.program != nullptr, "validation needs the lowered program");
    // A fresh interpreter per run: runs start from scratch and cannot influence each other.
    const auto run = [&](interp::InputOracle& oracle, interp::ExecutionObserver& observer) {
        interp::Interpreter interpreter(program.program, oracle, observer, options.run);
        return interpreter.run();
    };
    return validateWithRuns(witness, program, options, diagnostics, run);
}

} // namespace nitwit::validate
