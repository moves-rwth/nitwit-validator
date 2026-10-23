// nitwit: the NITWIT 4 command line (src/driver/README.md): validation of violation witnesses, the replay mode of the
// differential tests, and --version.

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <iostream>
#include <print>
#include <string_view>
#include <sys/prctl.h>
#include <vector>

#include "driver/command_line.hpp"
#include "driver/replay.hpp"
#include "driver/validation.hpp"
#include "support/large_stack.hpp"

namespace {

using namespace nitwit;

// The parser, sema and the interpreter recurse on the host stack (docs/architecture.md, "Calls").
constexpr std::size_t stackBytes = std::size_t{1} << 30;

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
    const std::string_view executable = argc > 0 ? argv[0] : "";
    const std::vector<std::string_view> arguments(argv + (argc > 0 ? 1 : 0), argv + argc);
    const std::expected<driver::CommandLine, int> commandLine =
        driver::parseCommandLine(arguments, driver::defaultDataModel(executable));
    if (!commandLine) {
        std::println(stderr, "{}",
                     commandLine.error() == driver::exitNoWitness ? "no witness given (-w)" : driver::usage);
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
