#include "validate/restart.hpp"

#include <format>
#include <optional>
#include <print>
#include <string>

#include "ast/casting.hpp"
#include "validate/constraints.hpp"
#include "validate/monitor.hpp"
#include "validate/oracle.hpp"
#include "validate/support_check.hpp"
#include "validate/values.hpp"
#include "validate/verdict.hpp"

namespace nitwit::validate {

namespace {

// "input 3 (the call of __VERIFIER_nondet_int at 7:35, occurrence 0)", for the log. A call is located at its `)`,
// as in witnesses; reads in constraints are located in the constraint's text.
std::string describeInput(value::InputId id, const ProvidedInput& input, const ProgramInput& program,
                          const SegmentMonitor& monitor) {
    const std::string site = input.function != nullptr ? std::format("the call of {}", input.function->name().text())
                                                       : std::string("the read");
    const auto* call = ast::dyn_cast<ast::CallExpr>(input.key.site);
    const SourceLocation location = call != nullptr ? call->rightParenLocation() : input.key.site->location();
    const std::string where =
        location.file == program.file ? monitor.locations().describe(location) : program.sources.describe(location);
    return std::format("input {} ({} at {}, occurrence {})", static_cast<std::uint32_t>(id), site, where,
                       input.key.occurrence);
}

} // namespace

ValidationResult validateWithRuns(const witness::YamlWitness& witness, const ProgramInput& program,
                                  const ValidationOptions& options, DiagnosticEngine& diagnostics,
                                  const RunFunction& run) {
    if (const std::optional<std::string> feature = unsupportedFeature(witness)) {
        return {.verdict = Verdict::Unsupported,
                .message = "NITWIT does not support " + *feature,
                .runs = 0,
                .segmentsReached = 0};
    }
    // Waypoints refer to the original program; one in another file (a header) would be matched against its lines.
    const FileId original = program.originalFile != FileId::None ? program.originalFile : program.file;
    if (const std::optional<std::string> other =
            waypointInOtherFile(witness.violationSequences.front(), program.sources.name(original))) {
        return {.verdict = Verdict::Unsupported,
                .message = "NITWIT does not support " + *other,
                .runs = 0,
                .segmentsReached = 0};
    }
    ConstraintCompiler constraints(program, diagnostics);
    WitnessOracle oracle(program.context, options.defaultInput);
    SegmentMonitor monitor(witness.violationSequences.front(), program, options.errorFunction, constraints, oracle,
                           options.log);
    if (const std::optional<std::string>& inconsistency = monitor.inconsistency()) {
        return {.verdict = Verdict::Unsupported,
                .message = "the witness does not fit the program: " + *inconsistency,
                .runs = 0,
                .segmentsReached = 0};
    }
    // Every confirmation comes from one complete run: a restart begins again at main with more inputs fixed.
    for (std::uint32_t runs = 1;; ++runs) {
        oracle.beginRun();
        monitor.beginRun(runs);
        const interp::RunResult result = run(oracle, monitor);
        if (monitor.state() != SegmentMonitor::State::Restart) {
            ValidationResult verdict = verdictOf(result, monitor, program, options);
            verdict.runs = runs;
            return verdict;
        }
        if (runs > options.maxRestarts) {
            return {.verdict = Verdict::Unknown,
                    .message =
                        std::format("the limit of {} restarts was reached: {}", options.maxRestarts, monitor.reason()),
                    .runs = runs,
                    .segmentsReached = monitor.segmentsPassed()};
        }
        for (const Fix& fix : monitor.fixes()) {
            const ProvidedInput& input = oracle.input(fix.input);
            if (options.log != nullptr) {
                std::println(*options.log, "run {}: fixing {} to {}", runs,
                             describeInput(fix.input, input, program, monitor),
                             toString(program.context, fix.value, input.type));
            }
            oracle.fix(input.key, fix.value);
        }
    }
}

} // namespace nitwit::validate
