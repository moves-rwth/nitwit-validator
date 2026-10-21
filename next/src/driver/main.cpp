// nitwit: the NITWIT 4 command line (src/driver/README.md). Stage 4 offers the replay mode of the differential tests
// and --version; witness validation follows in stage 6.

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <print>
#include <string_view>
#include <sys/prctl.h>

#include "driver/replay.hpp"
#include "support/large_stack.hpp"

namespace {

using namespace nitwit;

// The parser, sema and the interpreter recurse on the host stack (docs/architecture.md, "Calls").
constexpr std::size_t stackBytes = std::size_t{1} << 30;

constexpr std::string_view usage = "usage: nitwit --replay <inputs-file> [--input-log <log-file>] [-32|-64] <program>\n"
                                   "       nitwit --version\n"
                                   "Witness validation (-w) is not implemented yet.";

struct CommandLine {
    bool version = false;
    std::optional<driver::ReplayOptions> replay;
};

std::optional<CommandLine> parseCommandLine(int argc, char** argv) {
    CommandLine commandLine;
    driver::ReplayOptions replay;
    bool isReplay = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument = argv[i];
        const bool hasValue = i + 1 < argc;
        if (argument == "--version") {
            commandLine.version = true;
        } else if (argument == "--replay" && hasValue) {
            isReplay = true;
            replay.inputs = argv[++i];
        } else if (argument == "--input-log" && hasValue) {
            replay.inputLog = argv[++i];
        } else if (argument == "-32") {
            replay.dataModel = DataModel::ILP32;
        } else if (argument == "-64") {
            replay.dataModel = DataModel::LP64;
        } else if (replay.program.empty() && !argument.starts_with("-")) {
            replay.program = argument;
        } else {
            return std::nullopt;
        }
    }
    if (commandLine.version) {
        return commandLine;
    }
    if (!isReplay || replay.program.empty()) {
        return std::nullopt;
    }
    commandLine.replay = std::move(replay);
    return commandLine;
}

/// Ends the process by SIGABRT, as abort() does natively.
[[noreturn]] void abortProcess() {
    std::cout.flush();
    std::fflush(nullptr);
    std::signal(SIGABRT, SIG_DFL);
    std::abort();
}

} // namespace

int main(int argc, char** argv) {
    // No core dumps: abort() of the program is an outcome, not a crash (tools/difftest/README.md), and WSL's
    // core_pattern helper would collect one despite RLIMIT_CORE.
    prctl(PR_SET_DUMPABLE, 0);
    const std::optional<CommandLine> commandLine = parseCommandLine(argc, argv);
    if (!commandLine) {
        std::println(stderr, "{}", usage);
        return 3;
    }
    if (commandLine->version) {
        std::println("{}", NITWIT_VERSION);
        return 0;
    }
    std::ios::sync_with_stdio(false);
    driver::ReplayOutcome outcome;
    runWithLargeStack(stackBytes, [&] { outcome = driver::replay(*commandLine->replay); });
    if (outcome.abort) {
        abortProcess();
    }
    return outcome.exitCode;
}
