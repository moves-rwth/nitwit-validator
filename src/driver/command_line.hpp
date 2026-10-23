#pragma once

// The command line of nitwit (driver/README.md, "Command line").

#include <expected>
#include <optional>
#include <span>
#include <string_view>

#include "driver/replay.hpp"
#include "driver/validation.hpp"
#include "support/target.hpp"

namespace nitwit::driver {

// docs/exit-codes.md
inline constexpr int exitUsage = 3;
inline constexpr int exitNoWitness = 240;

inline constexpr std::string_view usage =
    "usage: nitwit [-32|-64] -w|--witness <witness> [-p|--property <property-file> | -e|--error-function <name>] [-v] "
    "<program>\n"
    "       nitwit --replay <inputs-file> [--input-log <log-file>] [-32|-64] <program>\n"
    "       nitwit --version";

struct CommandLine {
    bool version = false;
    std::optional<ReplayOptions> replay;
    std::optional<ValidationCommand> validation;
};

/// The data model without -32 and -64 for the executable's path (argv[0]): LP64 if its file name is exactly
/// "nitwit64" (bin/nitwit64 of the release archive), ILP32 otherwise.
[[nodiscard]] DataModel defaultDataModel(std::string_view executable);

/// The arguments after the program name, or the exit code for a wrong command line: 240 for a program without witness
/// (and without --replay), 3 otherwise. Options given twice are wrong, as are -32 and -64 together. `defaultModel`
/// applies without -32 and -64.
[[nodiscard]] std::expected<CommandLine, int> parseCommandLine(std::span<const std::string_view> arguments,
                                                               DataModel defaultModel);

} // namespace nitwit::driver
