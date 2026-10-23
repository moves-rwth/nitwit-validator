#include "validate/restart.hpp"

#include <algorithm>
#include <format>
#include <limits>
#include <new>
#include <optional>
#include <print>
#include <string>
#include <utility>

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

// Each waypoint needs a restart where the search predicts its outcome, and some a few more where a prediction fails or
// a later waypoint changes an input that an earlier one needs (a loop counter needs one per iteration); 16 more cover
// witnesses with few waypoints. The hard cap bounds the runs of long witnesses.
constexpr std::uint32_t baseRestarts = 16;
constexpr std::uint32_t restartsPerWaypoint = 2;

/// Whether one more run with other defaults may confirm what `verdict`, the verdict of the final run, does not
/// (validate/README.md, "Defaults"): the run ended by itself or at the error function elsewhere, with every waypoint
/// it evaluated passing, so the search had nothing to solve, and only the inputs between the waypoints decided its
/// path; and some of those had the default.
bool isRetried(const ValidationResult& verdict, const SegmentMonitor& monitor, const WitnessOracle& oracle,
               const ValidationOptions& options) {
    const bool endedWithoutFailedWaypoint =
        monitor.state() == SegmentMonitor::State::Following
        || (monitor.state() == SegmentMonitor::State::ErrorCalled && !monitor.isAtTarget());
    return verdict.verdict != Verdict::Confirmed && options.retryDefaultInput
           && *options.retryDefaultInput != options.defaultInput && oracle.gaveDefault() && endedWithoutFailedWaypoint;
}

/// One more run with other defaults after run `runs`, whose verdict is `verdict`, and never more: its confirmation, or
/// else `verdict`. The fixed inputs keep their values. The run does not search, and it may take the events of run
/// `runs` and ValidationOptions::retryExtraEvents more.
ValidationResult retryWithOtherDefaults(ValidationResult verdict, std::uint32_t runs, WitnessOracle& oracle,
                                        SegmentMonitor& monitor, const RunFunction& run, const ProgramInput& program,
                                        const ValidationOptions& options, DiagnosticEngine& diagnostics) {
    const std::int64_t defaultInput = *options.retryDefaultInput;
    if (options.log != nullptr) {
        std::println(*options.log, "run {}: no waypoint failed; one more run with inputs {} by default", runs,
                     defaultInput);
    }
    const std::uint64_t before = monitor.events();
    const std::uint64_t budget = before <= std::numeric_limits<std::uint64_t>::max() - options.retryExtraEvents
                                     ? before + options.retryExtraEvents
                                     : std::numeric_limits<std::uint64_t>::max();
    const std::size_t diagnosticsBefore = diagnostics.diagnostics().size();
    oracle.setDefault(defaultInput);
    oracle.beginRun();
    monitor.beginRun(runs + 1, {.eventBudget = budget, .searches = false});
    verdict.runs = runs + 1;
    try {
        const interp::RunResult retried = run(oracle, monitor);
        if (monitor.state() == SegmentMonitor::State::ErrorCalled && monitor.isAtTarget()) {
            ValidationResult confirmed = verdictOf(retried, monitor, program, options);
            confirmed.runs = runs + 1;
            return confirmed;
        }
    } catch (const std::bad_alloc&) {
        // The host's memory ran out in this run only: the verdict stays that of the run before, not 251.
        if (options.log != nullptr) {
            std::println(*options.log, "run {}: NITWIT ran out of memory", runs + 1);
        }
    }
    // Diagnostics of constraints first compiled in this run belong to no verdict.
    diagnostics.discardAfter(diagnosticsBefore);
    if (options.log != nullptr) {
        std::println(*options.log,
                     "run {}: with inputs {} by default, the witness was not confirmed either; the verdict is that of "
                     "run {}",
                     runs + 1, defaultInput, runs);
    }
    return verdict;
}

} // namespace

std::uint32_t restartLimit(const ValidationOptions& options, std::size_t waypoints) {
    const std::size_t limit = baseRestarts + restartsPerWaypoint * waypoints;
    return static_cast<std::uint32_t>(std::min<std::size_t>(limit, options.maxRestarts));
}

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
    WitnessOracle oracle(program.context, options.defaultInput, options.run.terms.maxInputs());
    SegmentMonitor monitor(witness.violationSequences.front(), program, options.errorFunction, constraints, oracle,
                           options.log, options.maxKeptAtoms);
    if (const std::optional<std::string>& inconsistency = monitor.inconsistency()) {
        return {.verdict = Verdict::Unsupported,
                .message = "the witness does not fit the program: " + *inconsistency,
                .runs = 0,
                .segmentsReached = 0};
    }
    std::size_t waypoints = 0;
    for (const witness::Segment& segment : witness.violationSequences.front().segments) {
        waypoints += segment.waypoints.size();
    }
    const std::uint32_t maxRestarts = restartLimit(options, waypoints);
    // Every confirmation comes from one complete run: a restart begins again at main with other inputs.
    for (std::uint32_t runs = 1;; ++runs) {
        oracle.beginRun();
        monitor.beginRun(runs);
        const interp::RunResult result = run(oracle, monitor);
        if (monitor.state() != SegmentMonitor::State::Restart) {
            ValidationResult verdict = verdictOf(result, monitor, program, options);
            verdict.runs = runs;
            if (isRetried(verdict, monitor, oracle, options)) {
                return retryWithOtherDefaults(std::move(verdict), runs, oracle, monitor, run, program, options,
                                              diagnostics);
            }
            return verdict;
        }
        if (runs > maxRestarts) {
            return {.verdict = Verdict::Unknown,
                    .message = std::format("the limit of {} restarts was reached: {}", maxRestarts, monitor.reason()),
                    .runs = runs,
                    .segmentsReached = monitor.segmentsPassed()};
        }
        for (const Change& change : monitor.changes()) {
            const ProvidedInput& input = oracle.input(change.input);
            if (options.log != nullptr) {
                std::println(*options.log, "run {}: fixing {} to {} ({})", runs,
                             describeInput(change.input, input, program, monitor),
                             toString(program.context, change.value, input.type), nameOf(change.method));
            }
            oracle.fix(input.key, change.value);
        }
        // Runs are deterministic: one with the inputs of an earlier run would end as it did.
        if (const std::optional<std::uint32_t> earlier = oracle.repeatedRun()) {
            return {.verdict = Verdict::LeftWitness,
                    .message = std::format("the execution left the witness: {}; the search proposes the inputs of "
                                           "run {} again",
                                           monitor.reason(), *earlier),
                    .runs = runs,
                    .segmentsReached = monitor.segmentsPassed()};
        }
    }
}

} // namespace nitwit::validate
