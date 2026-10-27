#include "validate/refute.hpp"

#include <format>
#include <optional>
#include <string>
#include <utility>

#include "validate/location.hpp"
#include "witness/model.hpp"

namespace nitwit::validate {

ValidationResult refute(const ProgramInput& program, const ValidationOptions& options, DiagnosticEngine& diagnostics) {
    // One segment with only a target, which every call of the error function reaches. Waypoints name the original
    // program, which the validation checks.
    const FileId original = program.originalFile != FileId::None ? program.originalFile : program.file;
    witness::YamlWitness witness;
    witness.violationSequences.push_back(
        {.metadata = {},
         .segments = {{.waypoints = {{.type = witness::WaypointType::Target,
                                      .action = witness::WaypointAction::Follow,
                                      .location = {.fileName = std::string(program.sources.name(original)),
                                                   .line = 1,
                                                   .column = std::nullopt,
                                                   .function = ""},
                                      .constraint = std::nullopt,
                                      .threadId = std::nullopt}}}}});
    ValidationOptions refuting = options;
    refuting.anyErrorCallIsTarget = true;
    ConfirmingRun run; // for the location of the call, if the caller keeps no run
    if (refuting.confirmingRun == nullptr) {
        refuting.confirmingRun = &run;
    }
    ValidationResult result = validate(witness, program, refuting, diagnostics);
    if (result.verdict != Verdict::Confirmed) {
        result.message = std::format("no run of the program calls '{}': {}", options.errorFunction, result.message);
        return result;
    }
    const LocationMap locations(program.sources, program.file, program.originalFile, program.unit);
    result.message =
        std::format("a run of the program calls '{}' at {}, so the program violates the property and no "
                    "correctness witness of it is valid",
                    options.errorFunction, locations.describe(refuting.confirmingRun->errorCall->location()));
    return result;
}

} // namespace nitwit::validate
