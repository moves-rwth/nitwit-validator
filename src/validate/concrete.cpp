#include "validate/concrete.hpp"

#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>

#include "ast/casting.hpp"
#include "interp/observer.hpp"
#include "sema/types.hpp"
#include "support/assert.hpp"
#include "support/sha256.hpp"
#include "validate/location.hpp"
#include "validate/monitor.hpp"
#include "validate/values.hpp"
#include "witness/parse.hpp"
#include "witness/write.hpp"

namespace nitwit::validate {

namespace {

// Witness format 2.2, "function_return": the constraint is an ext_c_expression, a C expression with \result.
constexpr std::string_view formatVersion = "2.2";
constexpr std::string_view resultFormat = "ext_c_expression";

/// A hexadecimal floating constant (C17 6.4.4.2) with exactly the value `number`, e.g. -0x1.8p+1.
template <typename T>
std::string hexadecimal(T number) {
    const std::string digits = std::format("{:a}", number); // without the prefix 0x
    return digits.starts_with('-') ? "-0x" + digits.substr(1) : "0x" + digits;
}

template <typename T>
std::string floatingConstraint(T number, std::string_view suffix) {
    if (std::isnan(number)) {
        return "\\result != \\result"; // no relation with a constant holds for NaN only
    }
    if (std::isinf(number)) {
        // Only +∞ is greater than the largest finite value, and only −∞ less than its negation.
        const std::string maximum = hexadecimal(std::numeric_limits<T>::max()) + std::string(suffix);
        return number > 0 ? std::format("\\result > {}", maximum) : std::format("\\result < -{}", maximum);
    }
    return std::format("\\result == {}{}", hexadecimal(number), suffix);
}

/// The location of the evaluation point `at` of waypoints of `type`, as a witness gives it, if it identifies that point
/// and no other. Otherwise a validation would evaluate the waypoint at another event, maybe one that comes first.
std::expected<witness::Location, std::string> waypointLocation(const LocationMap& locations, SourceLocation at,
                                                               witness::WaypointType type,
                                                               const std::string& fileName) {
    const std::optional<LineColumn> position = locations.witnessPosition(at);
    if (!position) {
        return std::unexpected(std::format("{} is in a header or in text that the preprocessor changed, which a "
                                           "witness cannot point to",
                                           locations.describe(at)));
    }
    witness::Location location{
        .fileName = fileName, .line = position->line, .column = position->column, .function = ""};
    const Place place = locations.place(location, type);
    if (place.offsets().size() != 1 || place.offsets().front() != at.offset) {
        return std::unexpected(std::format("{} does not identify one {} point", locations.describe(at), nameOf(type)));
    }
    if (!place.functions().empty()) {
        location.function = std::string(place.functions().front()->name().text());
    }
    return location;
}

/// The segments of the concrete witness of `run`: a function_return waypoint for each input, then the target.
std::expected<std::vector<witness::Segment>, std::string> segmentsOf(const ConfirmingRun& run,
                                                                     const ProgramInput& program,
                                                                     const LocationMap& locations,
                                                                     const ConcreteTask& task) {
    NITWIT_ASSERT(run.errorCall != nullptr, "a confirming run calls the error function");
    if (run.requests > run.inputs.size()) {
        return std::unexpected(std::format("the run took {} inputs, more than the {} that NITWIT records", run.requests,
                                           run.inputs.size()));
    }
    if (run.inputs.size() > task.maxInputs) {
        return std::unexpected(std::format("the run took {} inputs, more than the {} that a concrete witness gives",
                                           run.inputs.size(), task.maxInputs));
    }
    std::vector<witness::Segment> segments;
    segments.reserve(run.inputs.size() + 1);
    for (std::size_t i = 0; i < run.inputs.size(); ++i) {
        const ProvidedInput& input = run.inputs[i];
        const auto describe = [&] {
            return describeInput(static_cast<value::InputId>(i), input, program, locations);
        };
        const auto* call = ast::dyn_cast<ast::CallExpr>(input.key.site);
        if (call == nullptr || input.function == nullptr) {
            return std::unexpected(
                std::format("{} reads uninitialized memory, whose value a witness cannot give", describe()));
        }
        // __VERIFIER_nondet_memory fills memory with inputs: they are not the call's result.
        if (program.context.canonical(call->type()).unqualified() != program.context.canonical(input.type)) {
            return std::unexpected(
                std::format("{} is not the result of the call, which a witness cannot give", describe()));
        }
        std::optional<std::string> constraint = resultConstraint(program.context, input.value, input.type);
        if (!constraint) {
            return std::unexpected(std::format("{} has the value {}, which no constant gives", describe(),
                                               toString(program.context, input.value, input.type)));
        }
        std::expected<witness::Location, std::string> location = waypointLocation(
            locations, call->rightParenLocation(), witness::WaypointType::FunctionReturn, task.programPath);
        if (!location) {
            return std::unexpected(std::format("{}: {}", describe(), location.error()));
        }
        segments.push_back({.waypoints = {{.type = witness::WaypointType::FunctionReturn,
                                           .action = witness::WaypointAction::Follow,
                                           .location = std::move(*location),
                                           .constraint = witness::Constraint{.value = std::move(*constraint),
                                                                             .format = std::string(resultFormat)},
                                           .threadId = std::nullopt}}});
    }
    // The target is the start of the innermost statement or full expression that contains the call (Witness-Format.md,
    // "Target"); its function is that of the call's `)`, where function_enter is evaluated.
    std::expected<witness::Location, std::string> target = waypointLocation(
        locations, locations.enclosingStart(run.errorCall), witness::WaypointType::Target, task.programPath);
    if (!target) {
        return std::unexpected(std::format("the call of the error function at {}", target.error()));
    }
    if (const auto end = waypointLocation(locations, run.errorCall->rightParenLocation(),
                                          witness::WaypointType::FunctionEnter, task.programPath)) {
        target->function = end->function;
    }
    segments.push_back({.waypoints = {{.type = witness::WaypointType::Target,
                                       .action = witness::WaypointAction::Follow,
                                       .location = std::move(*target),
                                       .constraint = std::nullopt,
                                       .threadId = std::nullopt}}});
    return segments;
}

/// An RFC 9562 UUID of version 8 (custom) from the first 128 bits of a SHA-256 digest.
std::string uuidOf(const std::string& digest) {
    std::string hex = digest.substr(0, 32);
    hex[12] = '8';
    const auto variant = static_cast<unsigned>(std::stoul(hex.substr(16, 1), nullptr, 16));
    hex[16] = "89ab"[variant & 3u];
    return std::format("{}-{}-{}-{}-{}", hex.substr(0, 8), hex.substr(8, 4), hex.substr(12, 4), hex.substr(16, 4),
                       hex.substr(20, 12));
}

/// Whether a validation of the written witness with `options` and strict inputs confirms it, as a validation of the
/// file would: the witness is read back from its text.
std::expected<void, std::string> checkStrictly(const std::string& text, const ProgramInput& program,
                                               const ValidationOptions& options) {
    const std::expected<witness::YamlWitness, Error> parsed = witness::parseYamlWitness(text);
    if (!parsed) {
        return std::unexpected("NITWIT cannot read the witness it wrote: " + parsed.error().message);
    }
    ValidationOptions strict = options;
    strict.strictInputs = true;
    strict.retryDefaultInput.reset();
    strict.confirmingRun = nullptr;
    strict.log = nullptr;
    DiagnosticEngine diagnostics(program.sources);
    const ValidationResult result = validate(*parsed, program, strict, diagnostics);
    if (result.verdict != Verdict::Confirmed) {
        return std::unexpected("a validation with strict inputs does not confirm it: " + result.message);
    }
    return {};
}

} // namespace

std::optional<std::string> resultConstraint(ast::AstContext& context, const value::Value& value, ast::QualType type) {
    if (sema::isPointer(context, type)) {
        return value.kind() == value::Value::Kind::Integer && value.bits() == 0
                   ? std::optional<std::string>("\\result == 0")
                   : std::nullopt;
    }
    if (sema::isInteger(context, type)) {
        if (value.kind() != value::Value::Kind::Integer) {
            return std::nullopt;
        }
        if (!sema::integerFormat(context, type).isSigned) {
            if (value.bits() > std::numeric_limits<std::uint64_t>::max()) {
                return std::nullopt;
            }
            return std::format("\\result == {}U", static_cast<std::uint64_t>(value.bits()));
        }
        const Int128 number = value.signedBits();
        constexpr std::int64_t minimum = std::numeric_limits<std::int64_t>::min();
        if (number < minimum || number > std::numeric_limits<std::int64_t>::max()) {
            return std::nullopt;
        }
        if (number == minimum) {
            return "\\result == -9223372036854775807 - 1"; // 9223372036854775808 has no signed type (C17 6.4.4.1p6)
        }
        return std::format("\\result == {}", static_cast<std::int64_t>(number));
    }
    if (!sema::isFloating(context, type)) {
        return std::nullopt;
    }
    switch (sema::floatFormat(context, type)) {
    case value::FloatFormat::Float:
        return value.kind() == value::Value::Kind::Float ? std::optional(floatingConstraint(value.asFloat(), "f"))
                                                         : std::nullopt;
    case value::FloatFormat::Double:
        return value.kind() == value::Value::Kind::Double ? std::optional(floatingConstraint(value.asDouble(), ""))
                                                          : std::nullopt;
    case value::FloatFormat::LongDouble:
        return value.kind() == value::Value::Kind::LongDouble
                   ? std::optional(floatingConstraint(value.asLongDouble(), "L"))
                   : std::nullopt;
    }
    std::unreachable();
}

std::expected<ConcreteWitness, std::string> writeConcreteWitness(const ConfirmingRun& run, const ProgramInput& program,
                                                                 const ConcreteTask& task,
                                                                 const ValidationOptions& options) {
    const LocationMap locations(program.sources, program.file, program.originalFile, program.unit);
    std::expected<std::vector<witness::Segment>, std::string> segments = segmentsOf(run, program, locations, task);
    if (!segments) {
        return std::unexpected(std::move(segments.error()));
    }
    witness::ViolationSequence sequence;
    sequence.metadata = {
        .formatVersion = std::string(formatVersion),
        .producer = {.name = "NITWIT", .version = task.producerVersion},
        .task = {.inputFiles = {task.programPath},
                 .specification = task.specification,
                 .dataModel = program.context.target().dataModel() == DataModel::LP64 ? "LP64" : "ILP32",
                 .language = "C"}};
    sequence.segments = std::move(*segments);
    // Waypoints refer to the original program, whose bytes the hash covers.
    const FileId original = program.originalFile != FileId::None ? program.originalFile : program.file;
    const std::string programHash = sha256Hex(program.sources.contents(original));
    // Named by its content, so that a validation writes the same witness every time, except for its creation time
    // (docs/guidelines.md, "Determinism").
    const std::string uuid = uuidOf(sha256Hex(
        witness::writeYamlWitness(sequence, {.uuid = {}, .creationTime = {}, .inputFileHashes = {programHash}})));
    std::string text = witness::writeYamlWitness(
        sequence, {.uuid = uuid, .creationTime = task.creationTime, .inputFileHashes = {programHash}});
    if (task.check) {
        if (const std::expected<void, std::string> checked = checkStrictly(text, program, options); !checked) {
            return std::unexpected(checked.error());
        }
    }
    ConcreteWitness written{.text = std::move(text), .inputs = run.inputs.size(), .undefinedBehavior = {}};
    if (run.firstUndefined) {
        written.undefinedBehavior = std::format("{} at {}", interp::describe(run.firstUndefined->kind),
                                                locations.describe(run.firstUndefined->expr->location()));
    }
    return written;
}

} // namespace nitwit::validate
