#include "driver/validation.hpp"

#include <chrono>
#include <expected>
#include <format>
#include <fstream>
#include <iterator>
#include <new>
#include <print>
#include <regex>
#include <utility>

#include "driver/frontend.hpp"
#include "validate/concrete.hpp"
#include "validate/validator.hpp"
#include "witness/parse.hpp"

namespace nitwit::driver {

namespace {

std::optional<std::string> readFile(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return std::nullopt;
    }
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

// The result line, as in 3.x: VALIDATED, FAILED, UNKNOWN or ERROR (the exit codes BenchExec maps to ERROR), then the
// reason (driver/README.md, "Output").
int finish(std::ostream& out, std::string_view outcome, std::string_view reason, int exitCode) {
    std::println(out, "{}: {}", outcome, reason);
    return exitCode;
}

std::string_view outcomeOf(validate::Verdict verdict) {
    switch (verdict) {
    case validate::Verdict::Confirmed:
        return "VALIDATED";
    case validate::Verdict::LeftWitness:
    case validate::Verdict::ProgramFinished:
    case validate::Verdict::ErrorBeforeTarget:
    case validate::Verdict::TargetWithoutError:
        return "FAILED";
    case validate::Verdict::Unsupported:
    case validate::Verdict::Unknown:
    case validate::Verdict::OutOfMemory:
        return "UNKNOWN";
    }
    std::unreachable();
}

} // namespace

std::optional<std::string> errorFunctionOf(std::string_view property) {
    // SV-COMP's property unreach-call, with any blanks: CHECK( init(main()), LTL(G ! call(reach_error())) ).
    static const std::regex unreachCall(R"(\s*CHECK\s*\(\s*init\s*\(\s*main\s*\(\s*\)\s*\)\s*,\s*LTL\s*\(\s*G\s*!\s*)"
                                        R"(call\s*\(\s*([A-Za-z_][A-Za-z_0-9]*)\s*\(\s*\)\s*\)\s*\)\s*\)\s*)");
    std::match_results<std::string_view::const_iterator> match;
    if (!std::regex_match(property.begin(), property.end(), match, unreachCall)) {
        return std::nullopt;
    }
    return match[1].str();
}

namespace {

// driver/README.md, "Concrete witnesses": writes the witness of the run that confirmed; what happened, for stderr.
// Neither the verdict nor the exit code depends on it.
std::string writeConcreteWitness(const ValidationCommand& command, const validate::ProgramInput& program,
                                 const validate::ValidationOptions& options, const validate::ConfirmingRun& run) {
    const validate::ConcreteTask task{
        .programPath = command.program,
        .specification = std::format("CHECK( init(main()), LTL(G ! call({}())) )", options.errorFunction),
        .producerVersion = NITWIT_VERSION,
        .creationTime =
            std::format("{:%FT%TZ}", std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now())),
        .maxInputs = validate::ConcreteTask{}.maxInputs,
        .check = command.checksWitness};
    std::expected<validate::ConcreteWitness, std::string> witness;
    try {
        witness = validate::writeConcreteWitness(run, program, task, options);
    } catch (const std::bad_alloc&) {
        return "no witness written: NITWIT ran out of memory";
    }
    if (!witness) {
        return "no witness written: " + witness.error();
    }
    std::ofstream stream(command.witnessOutput, std::ios::binary | std::ios::trunc);
    stream << witness->text;
    stream.close();
    if (!stream) {
        return std::format("no witness written: cannot write '{}'", command.witnessOutput);
    }
    const std::string note = witness->undefinedBehavior.empty()
                                 ? ""
                                 : std::format("; the run continued after undefined behaviour ({}), which the witness "
                                               "follows as GCC-compiled code on x86 does",
                                               witness->undefinedBehavior);
    return std::format("wrote a{} concrete witness with {} input{} to '{}'{}",
                       command.checksWitness ? "" : "n unchecked", witness->inputs, witness->inputs == 1 ? "" : "s",
                       command.witnessOutput, note);
}

// The steps of driver/README.md.
int runSteps(const ValidationCommand& command, std::ostream& out, std::ostream& log) {
    // Step 1: the files; the program is read by the front end.
    const std::optional<std::string> witnessText = readFile(command.witness);
    if (!witnessText) {
        return finish(out, "ERROR", std::format("cannot read the witness '{}'", command.witness), 3);
    }
    std::string errorFunction = command.errorFunction;
    if (errorFunction.empty() && !command.propertyFile.empty()) {
        const std::optional<std::string> property = readFile(command.propertyFile);
        if (!property) {
            return finish(out, "ERROR", std::format("cannot read the property file '{}'", command.propertyFile), 3);
        }
        const std::optional<std::string> function = errorFunctionOf(*property);
        if (!function) {
            return finish(out, "UNKNOWN", "NITWIT only supports the property unreach-call", 4);
        }
        errorFunction = *function;
    }
    if (errorFunction.empty()) {
        errorFunction = "reach_error";
    }

    // Steps 2 and 3: preprocessing if needed, lexing, parsing, analysis, lowering.
    std::expected<std::unique_ptr<LoadedProgram>, FrontendError> loaded =
        loadProgram(command.program, command.dataModel);
    if (!loaded) {
        for (const std::string& message : loaded.error().messages) {
            std::println(out, "{}", message);
        }
        if (loaded.error().kind == FrontendError::Kind::Unreadable) {
            return finish(out, "ERROR", std::format("cannot read the program '{}'", command.program), 3);
        }
        return finish(out, "UNKNOWN", "NITWIT cannot analyze the program", 4);
    }

    // Step 4: the witness.
    if (witness::detectFormat(*witnessText) == witness::WitnessFormat::Graphml) {
        return finish(out, "UNKNOWN", "witnesses in format 1.0 (GraphML) are not supported yet", 4);
    }
    const std::expected<witness::YamlWitness, Error> witness = witness::parseYamlWitness(*witnessText);
    if (!witness) {
        return finish(out, "ERROR", std::format("the witness cannot be parsed: {}", witness.error().message), 2);
    }

    // Step 5: validation.
    LoadedProgram& program = **loaded;
    DiagnosticEngine diagnostics(program.sources);
    const validate::ProgramInput input{.sources = program.sources,
                                       .file = program.file,
                                       .originalFile = program.originalFile,
                                       .context = program.context,
                                       .unit = *program.unit,
                                       .program = program.program};
    validate::ValidationOptions options;
    options.errorFunction = errorFunction;
    options.retryDefaultInput = 1;
    options.strictInputs = command.strictInputs;
    options.log = command.verbose ? &log : nullptr;
    validate::ConfirmingRun confirming;
    if (!command.witnessOutput.empty()) {
        options.confirmingRun = &confirming;
    }
    const validate::ValidationResult result = validate::validate(*witness, input, options, diagnostics);
    for (const Diagnostic& diagnostic : diagnostics.diagnostics()) {
        std::println(out, "{}", diagnostics.format(diagnostic));
    }
    const int exitCode = finish(out, outcomeOf(result.verdict), result.message, validate::exitCode(result));
    if (!command.witnessOutput.empty()) {
        out.flush(); // the result first: writing the concrete witness takes another run
        options.log = nullptr;
        const std::string written = result.verdict == validate::Verdict::Confirmed
                                        ? writeConcreteWitness(command, input, options, confirming)
                                        : std::string("no witness written: the witness was not confirmed");
        std::println(log, "nitwit: {}", written);
    }
    return exitCode;
}

} // namespace

int validateWitness(const ValidationCommand& command, std::ostream& out, std::ostream& log) {
    // The interpreter limits the program's memory, but the host's can run out first (validate/README.md, "Known gaps").
    try {
        return runSteps(command, out, log);
    } catch (const std::bad_alloc&) {
        return finish(out, "UNKNOWN", "NITWIT ran out of memory", 251);
    }
}

} // namespace nitwit::driver
