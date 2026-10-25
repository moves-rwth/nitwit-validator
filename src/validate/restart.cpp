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

/// What the runs of a validation share.
struct Runs {
    const ProgramInput& program;
    const ValidationOptions& options;
    const RunFunction& run;
    WitnessOracle& oracle;
    SegmentMonitor& monitor;
    std::uint32_t maxRestarts; // per phase
};

/// How a phase of runs ended.
struct PhaseEnd {
    ValidationResult verdict; // of its final run, or of the restart limit or the repeated run that ended it
    std::uint64_t events = 0; // of all its runs
};

/// The verdict when the search after the current run proposes the inputs of run `earlier` again.
ValidationResult repeatedRunVerdict(const SegmentMonitor& monitor, std::uint32_t earlier) {
    if (monitor.isAtGuard()) {
        // The run would end at the guard, as the program does there.
        return {.verdict = Verdict::Unknown,
                .message = std::format("{}; the search proposes the inputs of run {} again", monitor.reason(), earlier),
                .runs = 0,
                .segmentsReached = monitor.segmentsPassed()};
    }
    return {.verdict = Verdict::LeftWitness,
            .message = std::format("the execution left the witness: {}; the search proposes the inputs of run {} again",
                                   monitor.reason(), earlier),
            .runs = 0,
            .segmentsReached = monitor.segmentsPassed()};
}

/// Runs the program from run `firstRun` on, restarting with the inputs the search finds, until a run ends without
/// asking for a restart, the phase has made Runs::maxRestarts restarts, or the search proposes the inputs of an
/// earlier run with the same default (validate/README.md, "Search"). All runs of the phase together may take
/// `eventBudget` events; none for no limit.
PhaseEnd runPhase(const Runs& runs, std::uint32_t firstRun, std::optional<std::uint64_t> eventBudget) {
    SegmentMonitor& monitor = runs.monitor;
    std::uint64_t events = 0;
    // Every confirmation comes from one complete run: a restart begins again at main with other inputs.
    for (std::uint32_t run = firstRun;; ++run) {
        runs.oracle.beginRun();
        // A run that asked for a restart stayed within the budget.
        monitor.beginRun(run, eventBudget ? std::optional(*eventBudget - events) : std::nullopt);
        const interp::RunResult result = runs.run(runs.oracle, monitor);
        events += monitor.events();
        const auto ended = [&](ValidationResult verdict) {
            verdict.runs = run;
            return PhaseEnd{.verdict = std::move(verdict), .events = events};
        };
        if (monitor.state() != SegmentMonitor::State::Restart) {
            return ended(verdictOf(result, monitor, runs.program, runs.options));
        }
        if (run - firstRun >= runs.maxRestarts) {
            return ended(
                {.verdict = Verdict::Unknown,
                 .message = std::format("the limit of {} restarts was reached: {}", runs.maxRestarts, monitor.reason()),
                 .runs = 0,
                 .segmentsReached = monitor.segmentsPassed()});
        }
        for (const Change& change : monitor.changes()) {
            const ProvidedInput& input = runs.oracle.input(change.input);
            if (runs.options.log != nullptr) {
                std::println(*runs.options.log, "run {}: fixing {} to {} ({})", run,
                             describeInput(change.input, input, runs.program, monitor),
                             toString(runs.program.context, change.value, input.type), nameOf(change.method));
            }
            runs.oracle.fix(input.key, change.value);
        }
        // Runs are deterministic: one with the inputs of an earlier run would end as it did.
        if (const std::optional<std::uint32_t> earlier = runs.oracle.repeatedRun()) {
            return ended(repeatedRunVerdict(monitor, *earlier));
        }
    }
}

/// Whether the retry phase may confirm what `verdict`, the verdict of the first phase, does not (validate/README.md,
/// "Defaults"): its final run ended by itself, at the error function elsewhere, or at a guard whose restart the limit
/// or a repeated run prevented, with every waypoint it evaluated passing, so the search had nothing to solve, and only
/// the inputs between the waypoints decided its path; and some of those had the default.
bool isRetried(const ValidationResult& verdict, const SegmentMonitor& monitor, const WitnessOracle& oracle,
               const ValidationOptions& options) {
    const bool endedWithoutFailedWaypoint =
        monitor.state() == SegmentMonitor::State::Following
        || (monitor.state() == SegmentMonitor::State::ErrorCalled && !monitor.isAtTarget())
        || (monitor.state() == SegmentMonitor::State::Restart && monitor.isAtGuard());
    return verdict.verdict != Verdict::Confirmed && options.retryDefaultInput
           && *options.retryDefaultInput != options.defaultInput && oracle.gaveDefault() && endedWithoutFailedWaypoint;
}

/// The retry phase after the first phase ended in `first`, and never more: a validation like the first phase with
/// other defaults, the fixed inputs keeping their values, with its own restart limit, and with the events of the first
/// phase and ValidationOptions::retryExtraEvents for all its runs together. Its confirmation, or else the verdict of
/// the first phase.
ValidationResult retryWithOtherDefaults(const Runs& runs, PhaseEnd first, DiagnosticEngine& diagnostics) {
    const ValidationOptions& options = runs.options;
    const std::int64_t defaultInput = *options.retryDefaultInput;
    const std::uint32_t lastRun = first.verdict.runs;
    const std::uint64_t budget = first.events <= std::numeric_limits<std::uint64_t>::max() - options.retryExtraEvents
                                     ? first.events + options.retryExtraEvents
                                     : std::numeric_limits<std::uint64_t>::max();
    if (options.log != nullptr) {
        std::println(*options.log,
                     "run {}: no waypoint failed; validating again with inputs {} by default, within {} events",
                     lastRun, defaultInput, budget);
    }
    const std::size_t diagnosticsBefore = diagnostics.diagnostics().size();
    runs.oracle.setDefault(defaultInput);
    ValidationResult verdict = std::move(first.verdict);
    try {
        PhaseEnd retried = runPhase(runs, lastRun + 1, budget);
        if (retried.verdict.verdict == Verdict::Confirmed) {
            return std::move(retried.verdict);
        }
        verdict.runs = retried.verdict.runs;
    } catch (const std::bad_alloc&) {
        // The host's memory ran out in this phase only: the verdict stays that of the first phase, not 251.
        verdict.runs = runs.monitor.run();
        if (options.log != nullptr) {
            std::println(*options.log, "run {}: NITWIT ran out of memory", verdict.runs);
        }
    }
    // Diagnostics of constraints first compiled in this phase belong to no verdict.
    diagnostics.discardAfter(diagnosticsBefore);
    if (options.log != nullptr) {
        std::println(*options.log,
                     "run {}: with inputs {} by default, the witness was not confirmed either; the verdict is that of "
                     "run {}",
                     verdict.runs, defaultInput, lastRun);
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
    const Runs runs{.program = program,
                    .options = options,
                    .run = run,
                    .oracle = oracle,
                    .monitor = monitor,
                    .maxRestarts = restartLimit(options, waypoints)};
    PhaseEnd first = runPhase(runs, 1, std::nullopt);
    if (isRetried(first.verdict, monitor, oracle, options)) {
        return retryWithOtherDefaults(runs, std::move(first), diagnostics);
    }
    return std::move(first.verdict);
}

} // namespace nitwit::validate
