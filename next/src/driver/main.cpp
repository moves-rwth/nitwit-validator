// nitwit: the NITWIT 4 command line (src/driver/README.md): validation of violation witnesses, the replay mode of the
// differential tests, and --version.

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <iostream>
#include <optional>
#include <print>
#include <string_view>
#include <sys/prctl.h>

#include "driver/replay.hpp"
#include "driver/validation.hpp"
#include "support/large_stack.hpp"

namespace {

using namespace nitwit;

// The parser, sema and the interpreter recurse on the host stack (docs/architecture.md, "Calls").
constexpr std::size_t stackBytes = std::size_t{1} << 30;

// docs/exit-codes.md
constexpr int exitUsage = 3;
constexpr int exitNoWitness = 240;

constexpr std::string_view usage =
    "usage: nitwit [-32|-64] -w|--witness <witness> [-p|--property <property-file> | -e|--error-function <name>] [-v] "
    "<program>\n"
    "       nitwit --replay <inputs-file> [--input-log <log-file>] [-32|-64] <program>\n"
    "       nitwit --version";

struct CommandLine {
    bool version = false;
    std::optional<driver::ReplayOptions> replay;
    std::optional<driver::ValidationCommand> validation;
};

/// The command line, or the exit code for a wrong one: 240 for a program without witness, 3 otherwise.
std::expected<CommandLine, int> parseCommandLine(int argc, char** argv) {
    CommandLine commandLine;
    driver::ReplayOptions replay;
    driver::ValidationCommand validation;
    bool isReplay = false;
    bool isValidation = false;
    std::string program;
    DataModel dataModel = DataModel::ILP32;
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
        } else if ((argument == "-w" || argument == "--witness") && hasValue) {
            isValidation = true;
            validation.witness = argv[++i];
        } else if ((argument == "-p" || argument == "--property") && hasValue && validation.propertyFile.empty()) {
            isValidation = true;
            validation.propertyFile = argv[++i];
        } else if ((argument == "-e" || argument == "--error-function") && hasValue
                   && validation.errorFunction.empty()) {
            isValidation = true;
            validation.errorFunction = argv[++i];
        } else if (argument == "-v") {
            isValidation = true;
            validation.verbose = true;
        } else if (argument == "-32") {
            dataModel = DataModel::ILP32;
        } else if (argument == "-64") {
            dataModel = DataModel::LP64;
        } else if (program.empty() && !argument.starts_with("-")) {
            program = argument;
        } else {
            return std::unexpected(exitUsage);
        }
    }
    if (commandLine.version) {
        return commandLine;
    }
    const bool hasBothSources = !validation.propertyFile.empty() && !validation.errorFunction.empty();
    const bool hasLogWithoutReplay = !replay.inputLog.empty() && !isReplay;
    if (program.empty() || (isReplay && isValidation) || hasBothSources || hasLogWithoutReplay) {
        return std::unexpected(exitUsage);
    }
    if (isReplay) {
        replay.program = program;
        replay.dataModel = dataModel;
        commandLine.replay = std::move(replay);
    } else if (!validation.witness.empty()) {
        validation.program = program;
        validation.dataModel = dataModel;
        commandLine.validation = std::move(validation);
    } else {
        return std::unexpected(exitNoWitness);
    }
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
    const std::expected<CommandLine, int> commandLine = parseCommandLine(argc, argv);
    if (!commandLine) {
        std::println(stderr, "{}", commandLine.error() == exitNoWitness ? "no witness given (-w)" : usage);
        return commandLine.error();
    }
    if (commandLine->version) {
        std::println("{}", NITWIT_VERSION);
        return 0;
    }
    std::ios::sync_with_stdio(false);
    if (commandLine->validation) {
        int exitCode = 0;
        runWithLargeStack(stackBytes,
                          [&] { exitCode = driver::validateWitness(*commandLine->validation, std::cout, std::cerr); });
        std::cout.flush();
        return exitCode;
    }
    driver::ReplayOutcome outcome;
    runWithLargeStack(stackBytes, [&] { outcome = driver::replay(*commandLine->replay); });
    if (outcome.abort) {
        abortProcess();
    }
    return outcome.exitCode;
}
