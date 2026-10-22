#include "driver/replay.hpp"

#include <algorithm>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <fstream>
#include <iostream>
#include <print>
#include <sstream>

#include "ast/casting.hpp"
#include "driver/frontend.hpp"
#include "interp/interpreter.hpp"
#include "sema/types.hpp"
#include "support/int128.hpp"

namespace nitwit::driver {

namespace {

/// The longest input line, without its line break (tools/difftest/harness.c).
constexpr std::size_t maxLineLength = 255;

bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

bool isHexDigit(char c) {
    return isDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

std::string_view withoutSign(std::string_view text) {
    if (!text.empty() && (text.front() == '+' || text.front() == '-')) {
        text.remove_prefix(1);
    }
    return text;
}

/// [+-]digits
bool isDecimalInteger(std::string_view text) {
    text = withoutSign(text);
    return !text.empty() && std::ranges::all_of(text, isDigit);
}

/// A C99 hexadecimal floating constant as %a prints it: [+-]0x<hex>[.<hex>]p[+-]<decimal>.
bool isHexFloat(std::string_view text) {
    text = withoutSign(text);
    if (text.size() < 3 || text[0] != '0' || (text[1] != 'x' && text[1] != 'X') || !isHexDigit(text[2])) {
        return false;
    }
    std::size_t at = 2;
    while (at < text.size() && isHexDigit(text[at])) {
        ++at;
    }
    if (at < text.size() && text[at] == '.') {
        ++at;
        while (at < text.size() && isHexDigit(text[at])) {
            ++at;
        }
    }
    if (at >= text.size() || (text[at] != 'p' && text[at] != 'P')) {
        return false;
    }
    return isDecimalInteger(text.substr(at + 1));
}

/// inf, -inf, nan, -nan.
bool isSpecialFloat(std::string_view text) {
    if (!text.empty() && text.front() == '-') {
        text.remove_prefix(1);
    }
    return text == "inf" || text == "nan";
}

std::string decimal(UInt128 value) {
    std::string digits;
    do {
        digits.push_back(static_cast<char>('0' + static_cast<int>(value % 10)));
        value /= 10;
    } while (value != 0);
    std::ranges::reverse(digits);
    return digits;
}

std::string decimal(Int128 value) {
    return value < 0 ? "-" + decimal(UInt128{0} - static_cast<UInt128>(value)) : decimal(static_cast<UInt128>(value));
}

std::string_view terminationName(interp::Termination termination) {
    switch (termination) {
    case interp::Termination::UndefinedBehavior:
        return "undefined behaviour";
    case interp::Termination::Unsupported:
        return "unsupported";
    case interp::Termination::OutOfMemory:
        return "out of memory";
    case interp::Termination::LimitReached:
        return "limit reached";
    default:
        return "the run ended";
    }
}

} // namespace

// ---- ReplayOracle ----

ReplayOracle::ReplayOracle(ast::AstContext& context, std::string inputsPath, std::string logPath)
    : context_(context), inputsPath_(std::move(inputsPath)), logPath_(std::move(logPath)) {}

ReplayOracle::~ReplayOracle() {
    if (log_ != nullptr) {
        std::fclose(log_);
    }
}

value::Value ReplayOracle::provide(const interp::InputRequest& request) {
    const ast::QualType type = request.type;
    // Native runs have no counterpart for uninitialized reads and unknown functions (interp/README.md, "Replay mode").
    if (request.kind != interp::InputRequest::Kind::NondetCall) {
        if (sema::isFloating(context_, type)) {
            switch (sema::floatFormat(context_, type)) {
            case value::FloatFormat::Float:
                return value::Value::ofFloat(0.0F);
            case value::FloatFormat::Double:
                return value::Value::ofDouble(0.0);
            case value::FloatFormat::LongDouble:
                return value::Value::ofLongDouble(0.0L);
            }
        }
        return value::Value::integer(0, sema::integerFormat(context_, type));
    }
    const std::string_view function = request.function != nullptr ? request.function->name().text() : "nondet";
    // The harness's function returns its own type, which decides the conversion and the log; the program then sees
    // that value in the type it declared.
    const bool isFloating = sema::isFloating(context_, type);
    ast::QualType returned = type;
    if (const std::optional<ast::QualType> harness = harnessType(function);
        harness && sema::isFloating(context_, *harness) == isFloating) {
        returned = *harness;
    }
    if (isFloating) {
        return value::convertFloat(floating(returned, function), sema::floatFormat(context_, returned),
                                   sema::floatFormat(context_, type));
    }
    const value::Value value = integer(returned, function);
    if (isBool(type)) {
        return value::toBool(value);
    }
    return value::convertInteger(value, sema::integerFormat(context_, type));
}

std::optional<ast::QualType> ReplayOracle::harnessType(std::string_view function) const {
    using ast::BuiltinKind;
    static constexpr std::pair<std::string_view, BuiltinKind> builtins[] = {
        {"char", BuiltinKind::Char},
        {"short", BuiltinKind::Short},
        {"int", BuiltinKind::Int},
        {"long", BuiltinKind::Long},
        {"longlong", BuiltinKind::LongLong},
        {"loff_t", BuiltinKind::LongLong},
        {"bool", BuiltinKind::Bool},
        {"uchar", BuiltinKind::UnsignedChar},
        {"u8", BuiltinKind::UnsignedChar},
        {"ushort", BuiltinKind::UnsignedShort},
        {"u16", BuiltinKind::UnsignedShort},
        {"uint", BuiltinKind::UnsignedInt},
        {"unsigned", BuiltinKind::UnsignedInt},
        {"u32", BuiltinKind::UnsignedInt},
        {"ulong", BuiltinKind::UnsignedLong},
        {"sector_t", BuiltinKind::UnsignedLong},
        {"pthread_t", BuiltinKind::UnsignedLong},
        {"size_t", BuiltinKind::UnsignedLong}, // size_t is unsigned long in both data models
        {"ulonglong", BuiltinKind::UnsignedLongLong},
        {"float", BuiltinKind::Float},
        {"double", BuiltinKind::Double},
        {"memory", BuiltinKind::UnsignedChar}, // per byte
    };
    constexpr std::string_view prefix = "__VERIFIER_nondet_";
    if (!function.starts_with(prefix)) {
        return std::nullopt;
    }
    const std::string_view suffix = function.substr(prefix.size());
    for (const auto& [name, kind] : builtins) {
        if (suffix == name) {
            return context_.builtin(kind);
        }
    }
    if (suffix == "pointer" || suffix == "pchar" || suffix == "charp") {
        return context_.pointerTo(context_.builtin(BuiltinKind::Char));
    }
    if (suffix == "uint128" && context_.target().hasInt128()) {
        return context_.builtin(BuiltinKind::UnsignedInt128);
    }
    return std::nullopt;
}

bool ReplayOracle::isBool(ast::QualType type) const {
    const auto* builtin = ast::dyn_cast<ast::BuiltinType>(context_.canonical(type).type());
    return builtin != nullptr && builtin->builtinKind() == ast::BuiltinKind::Bool;
}

// Integer lines are read modulo 2^128 (2^64 without __int128) and converted to the type as a C cast does.
value::Value ReplayOracle::integer(ast::QualType type, std::string_view function) {
    const value::IntegerFormat format = sema::integerFormat(context_, type);
    UInt128 raw = 0;
    const std::optional<std::string> line = nextLine(function);
    if (line) {
        if (!isDecimalInteger(*line)) {
            badInput(function, "expected a decimal integer");
        }
        for (const char digit : withoutSign(*line)) {
            raw = raw * 10 + static_cast<unsigned>(digit - '0');
        }
        if (line->front() == '-') {
            raw = UInt128{0} - raw;
        }
        if (!context_.target().hasInt128()) {
            raw &= ~std::uint64_t{0};
        }
    }
    const value::Value result = value::Value::integer(isBool(type) ? UInt128{raw != 0} : raw, format); // C17 6.3.1.2
    std::string text;
    if (format.bits == 128) {
        text = format.isSigned ? decimal(result.signedBits()) : decimal(result.bits());
    } else if (format.isSigned) {
        text = std::to_string(static_cast<long long>(result.signedBits()));
    } else {
        text = std::to_string(static_cast<unsigned long long>(result.bits()));
    }
    log(function, text, !line.has_value());
    return result;
}

// Floating lines are converted to double with strtod, then to the type.
value::Value ReplayOracle::floating(ast::QualType type, std::string_view function) {
    double raw = 0;
    const std::optional<std::string> line = nextLine(function);
    if (line) {
        if (!isDecimalInteger(*line) && !isHexFloat(*line) && !isSpecialFloat(*line)) {
            badInput(function, "expected a decimal integer, a hexadecimal float, inf or nan");
        }
        raw = std::strtod(line->c_str(), nullptr);
    }
    const value::FloatFormat format = sema::floatFormat(context_, type);
    const value::Value result = value::convertFloat(value::Value::ofDouble(raw), value::FloatFormat::Double, format);
    const value::Value logged = value::convertFloat(result, format, value::FloatFormat::Double);
    std::array<char, 64> text{};
    std::snprintf(text.data(), text.size(), "%a", logged.asDouble());
    log(function, text.data(), !line.has_value());
    return result;
}

std::optional<std::string> ReplayOracle::nextLine(std::string_view function) {
    if (!isOpened_) {
        isOpened_ = true;
        if (!inputsPath_.empty()) {
            std::ifstream file(inputsPath_, std::ios::binary);
            if (!file) {
                badInput(function, "cannot open the file");
            }
            std::ostringstream contents;
            contents << file.rdbuf();
            const std::string text = contents.str();
            for (std::size_t start = 0; start < text.size();) {
                const std::size_t end = text.find('\n', start);
                lines_.push_back(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
                start = end == std::string::npos ? text.size() : end + 1;
            }
        }
    }
    if (nextLine_ >= lines_.size()) {
        return std::nullopt;
    }
    std::string line = lines_[nextLine_++];
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    if (line.size() > maxLineLength) {
        badInput(function, "line too long");
    }
    return line;
}

void ReplayOracle::badInput(std::string_view function, std::string_view message) const {
    // Like the harness: the run ends at once, with the output flushed.
    std::cout.flush();
    std::println(stderr, "{}bad input: {}, line {} of NITWIT_INPUTS: {}", marker, function, nextLine_, message);
    std::fflush(stderr);
    std::_Exit(exitBadInput);
}

void ReplayOracle::log(std::string_view function, std::string_view value, bool isDefault) {
    if (!isLogOpened_) {
        isLogOpened_ = true;
        if (!logPath_.empty()) {
            log_ = std::fopen(logPath_.c_str(), "a");
        }
    }
    if (log_ != nullptr) {
        std::println(log_, "{} {}{}", function, value, isDefault ? " default" : "");
        std::fflush(log_);
    }
}

// ---- ReplayObserver ----

namespace {

/// Whether the program defines the function only inline (C17 6.7.4p7: every declaration says `inline`, none `extern`
/// or `static`), which provides no external definition. GCC at -O0 calls the external one, the harness's.
bool hasOnlyInlineDefinition(const ast::FunctionDecl* function) {
    for (const ast::FunctionDecl* decl = function; decl != nullptr; decl = decl->previous()) {
        if (!decl->isInline() || decl->storage() == ast::StorageClass::Extern
            || decl->storage() == ast::StorageClass::Static) {
            return false;
        }
    }
    return true;
}

} // namespace

interp::Directive ReplayObserver::afterArguments(const interp::CallEvent& event, interp::EvaluationContext&) {
    if (event.callee == nullptr || (event.callee->body() != nullptr && !hasOnlyInlineDefinition(event.callee))) {
        return interp::Directive::Continue; // the program's own definition wins over the harness's weak one
    }
    const std::string_view name = event.callee->name().text();
    hasTrapped_ = name == "__builtin_trap";
    if (name == "__assert_fail" || name == "__VERIFIER_error" || name == "reach_error") {
        errorFunction_ = std::string(name);
        return interp::Directive::Stop;
    }
    return interp::Directive::Continue;
}

// ---- replay ----

ReplayOutcome replay(const ReplayOptions& options) {
    auto loaded = loadProgram(options.program, options.dataModel);
    if (!loaded) {
        for (const std::string& message : loaded.error().messages) {
            std::println(stderr, "nitwit: {}", message);
        }
        return {.exitCode = loaded.error().kind == FrontendError::Kind::Unreadable ? 3 : 4, .abort = false};
    }
    ReplayOracle oracle((*loaded)->context, options.inputs, options.inputLog);
    ReplayObserver observer;
    interp::RunOptions runOptions;
    runOptions.output = &std::cout;
    if (options.terms == ReplayOptions::Terms::None) {
        runOptions.terms.maxTerms = 0; // replay has no use for terms
    }
    runOptions.checkTerms = options.terms == ReplayOptions::Terms::Check;
    interp::Interpreter interpreter((*loaded)->program, oracle, observer, runOptions);
    const interp::RunResult result = interpreter.run();
    std::cout.flush();
    switch (result.termination) {
    case interp::Termination::Returned:
    case interp::Termination::Exit:
        return {.exitCode = result.exitStatus & 0xFF, .abort = false}; // the status the process reports
    case interp::Termination::Stopped:
        if (observer.errorFunction()) {
            std::println(stderr, "{}error {}", marker, *observer.errorFunction());
            return {.exitCode = exitError, .abort = false};
        }
        break;
    case interp::Termination::AssumeFailed:
        std::println(stderr, "{}assume", marker);
        return {.exitCode = exitAssume, .abort = false};
    case interp::Termination::Abort:
        if (observer.hasTrapped()) {
            // GCC compiles __builtin_trap to an invalid instruction (ud2): the native run ends by SIGILL.
            std::fflush(nullptr);
            std::signal(SIGILL, SIG_DFL);
            std::raise(SIGILL);
        }
        return {.exitCode = 0, .abort = true};
    default:
        break;
    }
    const std::string where =
        result.location.isValid() ? std::format(" at {}", (*loaded)->sources.describe(result.location)) : "";
    std::println(stderr, "nitwit: {}{}: {}", terminationName(result.termination), where, result.message);
    return {.exitCode = result.termination == interp::Termination::OutOfMemory ? 251 : 4, .abort = false};
}

} // namespace nitwit::driver
