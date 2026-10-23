#include "driver/command_line.hpp"

#include <string>
#include <utility>

namespace nitwit::driver {

DataModel defaultDataModel(std::string_view executable) {
    // The file name: everything after the last '/' (without one, npos + 1 is 0).
    const std::string_view name = executable.substr(executable.rfind('/') + 1);
    return name == "nitwit64" ? DataModel::LP64 : DataModel::ILP32;
}

std::expected<CommandLine, int> parseCommandLine(std::span<const std::string_view> arguments, DataModel defaultModel) {
    CommandLine commandLine;
    ReplayOptions replay;
    ValidationCommand validation;
    bool isReplay = false;
    bool isValidation = false;
    bool hasDataModel = false;
    std::string program;
    DataModel dataModel = defaultModel;
    // Each option with a value may be given once; its value is the next argument.
    const auto takeValue = [&](std::size_t& i, std::string& into) {
        if (i + 1 >= arguments.size() || !into.empty()) {
            return false;
        }
        into = arguments[++i];
        return !into.empty();
    };
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        const std::string_view argument = arguments[i];
        bool isRight = true;
        if (argument == "--version") {
            commandLine.version = true;
        } else if (argument == "--replay") {
            isReplay = true;
            isRight = takeValue(i, replay.inputs);
        } else if (argument == "--input-log") {
            isRight = takeValue(i, replay.inputLog);
        } else if (argument == "--terms" || argument == "--check-terms") {
            // Hidden options for testing the terms of the interpreter.
            isRight = replay.terms == ReplayOptions::Terms::None;
            replay.terms = argument == "--terms" ? ReplayOptions::Terms::Record : ReplayOptions::Terms::Check;
        } else if (argument == "-w" || argument == "--witness") {
            isValidation = true;
            isRight = takeValue(i, validation.witness);
        } else if (argument == "-p" || argument == "--property") {
            isValidation = true;
            isRight = takeValue(i, validation.propertyFile);
        } else if (argument == "-e" || argument == "--error-function") {
            isValidation = true;
            isRight = takeValue(i, validation.errorFunction);
        } else if (argument == "-v") {
            isValidation = true;
            validation.verbose = true;
        } else if (argument == "-32" || argument == "-64") {
            isRight = !hasDataModel;
            hasDataModel = true;
            dataModel = argument == "-32" ? DataModel::ILP32 : DataModel::LP64;
        } else if (program.empty() && !argument.empty() && !argument.starts_with("-")) {
            program = argument;
        } else {
            isRight = false;
        }
        if (!isRight) {
            return std::unexpected(exitUsage);
        }
    }
    if (commandLine.version) {
        return commandLine;
    }
    const bool hasBothSources = !validation.propertyFile.empty() && !validation.errorFunction.empty();
    const bool hasReplayOptionsOnly =
        (!replay.inputLog.empty() || replay.terms != ReplayOptions::Terms::None) && !isReplay;
    if (program.empty() || (isReplay && isValidation) || hasBothSources || hasReplayOptionsOnly) {
        return std::unexpected(exitUsage);
    }
    if (isReplay) {
        replay.program = std::move(program);
        replay.dataModel = dataModel;
        commandLine.replay = std::move(replay);
    } else if (!validation.witness.empty()) {
        validation.program = std::move(program);
        validation.dataModel = dataModel;
        commandLine.validation = std::move(validation);
    } else {
        return std::unexpected(exitNoWitness);
    }
    return commandLine;
}

} // namespace nitwit::driver
