#include "validate/verdict.hpp"

#include <format>
#include <string>
#include <utility>

#include "support/assert.hpp"

namespace nitwit::validate {

namespace {

std::string describeEnd(const interp::RunResult& run, const ProgramInput& program) {
    const std::string where = run.location.isValid() ? " at " + program.sources.describe(run.location) : "";
    switch (run.termination) {
    case interp::Termination::Returned:
        return std::format("main returned {}", run.exitStatus);
    case interp::Termination::Exit:
        return std::format("the program called exit({})", run.exitStatus);
    case interp::Termination::Abort:
        return std::format("the program aborted{}", where);
    case interp::Termination::AssumeFailed:
        return std::format("__VERIFIER_assume failed{}, the execution is infeasible", where);
    case interp::Termination::Stopped:
        return "the run was stopped";
    case interp::Termination::UndefinedBehavior:
        return std::format("undefined behaviour{}: {}", where, run.message);
    case interp::Termination::Unsupported:
        return std::format("unsupported{}: {}", where, run.message);
    case interp::Termination::OutOfMemory:
        return std::format("out of memory{}: {}", where, run.message);
    case interp::Termination::LimitReached:
        return std::format("limit reached{}: {}", where, run.message);
    case interp::Termination::EndlessLoop:
        return std::format("the program entered an endless loop without effect{}, the execution is infeasible", where);
    }
    std::unreachable();
}

} // namespace

int exitCode(const ValidationResult& result) {
    switch (result.verdict) {
    case Verdict::Confirmed:
        return 0;
    case Verdict::LeftWitness:
        return 241;
    case Verdict::ProgramFinished:
        return 242;
    case Verdict::ErrorBeforeTarget:
        // Not 252 as in 3.x: BenchExec 3.35 maps 252 to ERROR, every version maps 241 to UNKNOWN (docs/exit-codes.md).
        return 241;
    case Verdict::TargetWithoutError:
        return 250;
    case Verdict::Unsupported:
    case Verdict::Unknown:
        return 4;
    case Verdict::OutOfMemory:
        return 251;
    }
    std::unreachable();
}

ValidationResult verdictOf(const interp::RunResult& run, const SegmentMonitor& monitor, const ProgramInput& program,
                           const ValidationOptions& options) {
    ValidationResult result;
    result.segmentsReached = monitor.segmentsPassed();
    const auto decided = [&](Verdict verdict, std::string message) {
        result.verdict = verdict;
        result.message = std::move(message);
        return result;
    };
    switch (monitor.state()) {
    case SegmentMonitor::State::Unsupported:
        return decided(Verdict::Unsupported, monitor.reason());
    case SegmentMonitor::State::Unknown:
        return decided(Verdict::Unknown, monitor.reason());
    case SegmentMonitor::State::Rejected:
        return decided(Verdict::LeftWitness, "the execution left the witness: " + monitor.reason());
    case SegmentMonitor::State::ErrorCalled:
        if (monitor.isAtTarget()) {
            return decided(Verdict::Confirmed, "the witness was followed to its target: " + monitor.reason());
        }
        return decided(Verdict::ErrorBeforeTarget, "the target was not reached: " + monitor.reason());
    case SegmentMonitor::State::Restart:
        NITWIT_UNREACHABLE("a run that asks for a restart has no verdict");
    case SegmentMonitor::State::Following:
        break;
    }

    const std::string end = describeEnd(run, program);
    // A refutation follows no witness (refute.hpp), so its one segment, which only the validator made, is not named.
    const bool namesSegments = !options.anyErrorCallIsTarget;
    const std::string segment =
        namesSegments ? std::format(" in segment {} of {}", monitor.segmentsPassed() + 1, monitor.segmentCount()) : "";
    switch (run.termination) {
    case interp::Termination::Returned:
    case interp::Termination::Exit:
        if (monitor.isTargetSegmentCurrent()) {
            return decided(Verdict::TargetWithoutError,
                           std::format("{}{} without calling '{}'",
                                       namesSegments ? "the target segment was reached, but " : "", end,
                                       options.errorFunction));
        }
        return decided(Verdict::ProgramFinished,
                       std::format("{}{} without calling '{}'", end, segment, options.errorFunction));
    case interp::Termination::Stopped:
        NITWIT_UNREACHABLE("only the monitor stops runs, and it says why");
    case interp::Termination::Unsupported:
        return decided(Verdict::Unsupported, end + segment);
    case interp::Termination::OutOfMemory:
        return decided(Verdict::OutOfMemory, end + segment);
    case interp::Termination::Abort:
    case interp::Termination::AssumeFailed:
    case interp::Termination::EndlessLoop:
    case interp::Termination::UndefinedBehavior:
    case interp::Termination::LimitReached:
        return decided(Verdict::Unknown, end + segment);
    }
    std::unreachable();
}

} // namespace nitwit::validate
