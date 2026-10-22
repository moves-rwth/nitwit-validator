// Program termination, the SV-COMP functions, integer conversions and arithmetic, rand and errno (C17 7.22, 7.5).

#include <array>
#include <cerrno>
#include <cstdint>
#include <format>
#include <string>
#include <vector>

#include "interp/library_call.hpp"

namespace nitwit::interp::library {

namespace {

// ---- Termination ----

/// exit, _Exit, _exit and quick_exit. The interpreter's stdout is unbuffered (as in tools/difftest), so the output
/// written before is kept by all of them; atexit is not supported, so there are no handlers to run.
LibraryResult exit(const Call& call) {
    LibraryResult result;
    result.kind = LibraryResult::Kind::Exit;
    result.status = call.intArgument(0);
    return result;
}

LibraryResult abort(const Call& call) {
    return ending(LibraryResult::Kind::Abort, std::format("{} was called", call.name()));
}

/// The string argument at `index`, or "(null)" for a null pointer, as glibc's message prints it.
std::string stringOrNull(const Call& call, std::size_t index) {
    const Address address = call.pointerArgument(index);
    return address == 0 ? "(null)" : call.readString(address);
}

/// The message glibc prints to stderr (which the interpreter discards) before it aborts; the function is optional.
LibraryResult assertionFailed(const Call& call, unsigned line, Address function) {
    const std::string assertion = stringOrNull(call, 0);
    const std::string file = stringOrNull(call, 1);
    const std::string functionPart = function == 0 ? "" : call.readString(function) + ": ";
    return ending(LibraryResult::Kind::Abort,
                  std::format("{}:{}: {}Assertion `{}' failed.", file, line, functionPart, assertion));
}

/// __assert_fail(assertion, file, line, function), which the assert macro calls (and sv-benchmarks' reach_error).
LibraryResult assertFail(const Call& call) {
    const auto line = static_cast<unsigned>(call.integerArgument(2, ScalarType::UnsignedInt));
    return assertionFailed(call, line, call.pointerArgument(3));
}

/// __assert(assertion, file, line), glibc's BSD form: __assert_fail without a function.
LibraryResult bsdAssert(const Call& call) {
    return assertionFailed(call, static_cast<unsigned>(call.intArgument(2)), 0);
}

// ---- errno (C17 7.5) ----

LibraryResult errnoLocation(const Call& call) {
    return call.returnPointer(call.objectAddress("errno"));
}

LibraryObject errnoObject(const TargetInfo& /*target*/) {
    return {.size = 4, .alignment = 4, .initialize = initializeZero};
}

// ---- Integer conversions (C17 7.22.1.4) ----

struct Parsed {
    UInt128 value = 0; // canonical bits of the result type
    Address end = 0;   // where the conversion stopped, for endptr
    bool rangeError = false;
};

/// The value of `c` as a digit, or 36 (no digit in any base) if it is none.
unsigned digitValue(char c) {
    if (c >= '0' && c <= '9') {
        return static_cast<unsigned>(c - '0');
    }
    if (c >= 'a' && c <= 'z') {
        return static_cast<unsigned>(c - 'a') + 10;
    }
    if (c >= 'A' && c <= 'Z') {
        return static_cast<unsigned>(c - 'A') + 10;
    }
    return 36;
}

/// strtol and its relatives as glibc implements them (stdlib/strtol_l.c), for the result type `type` and a base of 0
/// or 2 to 36. `binaryPrefix` accepts "0b" for the bases 0 and 2, as the C23 versions (__isoc23_strtol, ...) do.
/// The string is read only up to the first character that cannot continue the number, as glibc reads it.
Parsed parseInteger(const Call& call, Address address, unsigned base, ScalarType type, bool binaryPrefix) {
    ByteReader reader(call, address);
    Address position = address; // of `c`
    char c = static_cast<char>(reader.next());
    const auto advance = [&] {
        ++position;
        c = static_cast<char>(reader.next());
    };
    while (c == ' ' || (c >= '\t' && c <= '\r')) { // isspace in the C locale
        advance();
    }
    const bool negative = c == '-';
    if (c == '-' || c == '+') {
        advance();
    }
    bool anyDigit = false;
    bool prefix = false;
    if (c == '0') {
        advance();
        if ((base == 0 || base == 16) && (c == 'x' || c == 'X')) {
            base = 16;
            prefix = true;
        } else if (binaryPrefix && (base == 0 || base == 2) && (c == 'b' || c == 'B')) {
            base = 2;
            prefix = true;
        } else {
            base = base == 0 ? 8 : base;
            anyDigit = true; // the "0" is a digit of the number
        }
        if (prefix) {
            advance();
        }
    } else if (base == 0) {
        base = 10;
    }

    const value::IntegerFormat format = call.format(type);
    const UInt128 typeMax = (UInt128{1} << (format.isSigned ? format.bits - 1 : format.bits)) - 1;
    // The largest magnitude the result can have: with a minus sign and a signed type, that of the minimum.
    const UInt128 limit = format.isSigned && negative ? typeMax + 1 : typeMax;
    UInt128 magnitude = 0;
    bool rangeError = false;
    for (unsigned digit = digitValue(c); digit < base; digit = digitValue(c)) {
        anyDigit = true;
        if (magnitude > (limit - digit) / base) {
            rangeError = true;
        } else {
            magnitude = magnitude * base + digit;
        }
        advance();
    }
    if (!anyDigit) {
        // No digits after "0x" or "0b": the "0" is the number, and endptr points to the "x" or "b". Without any
        // digit, endptr is the argument.
        return {.value = 0, .end = prefix ? position - 1 : address, .rangeError = false};
    }
    UInt128 result = negative ? UInt128{0} - magnitude : magnitude; // unsigned types negate in the type
    if (rangeError) {
        result = format.isSigned && negative ? UInt128{0} - limit : limit;
    }
    return {.value = value::Value::integer(result, format).bits(), .end = position, .rangeError = rangeError};
}

/// strtol(nptr, endptr, base) and its relatives with result type `type`.
LibraryResult convert(const Call& call, ScalarType type, bool binaryPrefix) {
    const Address address = call.pointerArgument(0);
    const Address endPointer = call.pointerArgument(1);
    const int base = call.intArgument(2);
    if (base < 0 || base == 1 || base > 36) { // glibc leaves *endptr alone
        call.setErrno(EINVAL);
        return call.returnInteger(0, type);
    }
    const Parsed parsed = parseInteger(call, address, static_cast<unsigned>(base), type, binaryPrefix);
    if (parsed.rangeError) {
        call.setErrno(ERANGE);
    }
    if (endPointer != 0) {
        call.noteWrittenPointerValue(); // the end pointer is nptr plus the length of the number
        call.writeInteger(endPointer, parsed.end, ScalarType::Pointer);
    }
    return call.returnInteger(parsed.value, type);
}

LibraryResult strtol(const Call& call) {
    return convert(call, ScalarType::Long, false);
}

LibraryResult strtoul(const Call& call) {
    return convert(call, ScalarType::UnsignedLong, false);
}

/// Also strtoq, strtoimax (intmax_t has 64 bits in both data models).
LibraryResult strtoll(const Call& call) {
    return convert(call, ScalarType::LongLong, false);
}

LibraryResult strtoull(const Call& call) {
    return convert(call, ScalarType::UnsignedLongLong, false);
}

LibraryResult isoc23Strtol(const Call& call) {
    return convert(call, ScalarType::Long, true);
}

LibraryResult isoc23Strtoul(const Call& call) {
    return convert(call, ScalarType::UnsignedLong, true);
}

LibraryResult isoc23Strtoll(const Call& call) {
    return convert(call, ScalarType::LongLong, true);
}

LibraryResult isoc23Strtoull(const Call& call) {
    return convert(call, ScalarType::UnsignedLongLong, true);
}

/// atoi, atol and atoll: glibc's atoi is (int) strtol(nptr, NULL, 10), including errno on overflow of long.
LibraryResult toInteger(const Call& call, ScalarType parsedType, ScalarType resultType) {
    const Parsed parsed = parseInteger(call, call.pointerArgument(0), 10, parsedType, false);
    if (parsed.rangeError) {
        call.setErrno(ERANGE);
    }
    return call.returnInteger(parsed.value, resultType);
}

LibraryResult atoi(const Call& call) {
    return toInteger(call, ScalarType::Long, ScalarType::Int);
}

LibraryResult atol(const Call& call) {
    return toInteger(call, ScalarType::Long, ScalarType::Long);
}

LibraryResult atoll(const Call& call) {
    return toInteger(call, ScalarType::LongLong, ScalarType::LongLong);
}

// ---- Integer arithmetic (C17 7.22.6) ----

/// The magnitude, computed in the type: the minimum stays the minimum, as in glibc. C leaves that case undefined
/// (C17 7.22.6.1p2), but natively compiled programs continue with it.
LibraryResult absolute(const Call& call, ScalarType type) {
    const Int128 value = call.signedArgument(0, type);
    return call.returnInteger(static_cast<UInt128>(value < 0 ? -value : value), type);
}

LibraryResult abs(const Call& call) {
    return absolute(call, ScalarType::Int);
}

LibraryResult labs(const Call& call) {
    return absolute(call, ScalarType::Long);
}

/// Also imaxabs (intmax_t has 64 bits in both data models).
LibraryResult llabs(const Call& call) {
    return absolute(call, ScalarType::LongLong);
}

/// div, ldiv and lldiv return the structure {quot, rem} of two members of the type.
LibraryResult divide(const Call& call, ScalarType type) {
    const value::IntegerFormat format = call.format(type);
    const Int128 numerator = call.signedArgument(0, type);
    const Int128 denominator = call.signedArgument(1, type);
    if (denominator == 0) {
        call.undefinedBehavior(std::format("{} divides by zero", call.name()));
    }
    const Int128 quotient = numerator / denominator;
    if (quotient > (Int128{1} << (format.bits - 1)) - 1) {
        call.undefinedBehavior(std::format("the quotient of {} overflows", call.name()));
    }
    const std::size_t size = format.bits / 8u;
    std::vector<std::byte> bytes(2 * size);
    value::encodeInteger(value::Value::integer(static_cast<UInt128>(quotient), format), format,
                         std::span(bytes).first(size));
    value::encodeInteger(value::Value::integer(static_cast<UInt128>(numerator % denominator), format), format,
                         std::span(bytes).subspan(size));
    return LibraryResult::returning(value::Value::ofBytes(std::move(bytes)));
}

LibraryResult div(const Call& call) {
    return divide(call, ScalarType::Int);
}

LibraryResult ldiv(const Call& call) {
    return divide(call, ScalarType::Long);
}

LibraryResult lldiv(const Call& call) {
    return divide(call, ScalarType::LongLong);
}

// ---- rand (C17 7.22.2) ----
//
// glibc's rand() is random() with its default state of type 3 (stdlib/random_r.c): an additive feedback generator
// r[i] = r[i - 3] + r[i - 31] (mod 2^32) over 31 words, whose output is r[i] >> 1. srand(seed) sets the words to
// seed * 16807^i mod (2^31 - 1), computed as glibc does (Schrage's method, wrapping for seeds of 2^31 and above),
// and discards the first 310 outputs; seed 0 counts as 1. Without srand, the state is that of srand(1). So the
// interpreter produces the numbers of a native run.
//
// The state is the library object "__random_state" in interpreted memory: the 31 words, then the indices of the
// front and the rear word (glibc's fptr and rptr).

constexpr std::uint32_t randomDegree = 31;
constexpr std::uint32_t randomSeparation = 3;
constexpr std::uint64_t randomStateSize = (randomDegree + 2) * 4;

struct RandomState {
    std::array<std::uint32_t, randomDegree + 2> words{}; // the words, front, rear

    std::uint32_t& front() { return words[randomDegree]; }
    std::uint32_t& rear() { return words[randomDegree + 1]; }

    std::int32_t next() {
        // Indices out of range can only come from a program that overwrote the state; keep them in range.
        front() %= randomDegree;
        rear() %= randomDegree;
        words[front()] += words[rear()];
        const auto result = static_cast<std::int32_t>(words[front()] >> 1);
        if (++front() == randomDegree) {
            front() = 0;
            ++rear();
        } else if (++rear() == randomDegree) {
            rear() = 0;
        }
        return result;
    }

    static RandomState seeded(std::uint32_t seed) {
        RandomState state;
        if (seed == 0) {
            seed = 1;
        }
        state.words[0] = seed;
        auto word = static_cast<std::int32_t>(seed);
        for (std::uint32_t i = 1; i < randomDegree; ++i) {
            const std::int64_t high = word / 127773;
            const std::int64_t low = word % 127773;
            word = static_cast<std::int32_t>(16807 * low - 2836 * high);
            if (word < 0) {
                word += 2147483647;
            }
            state.words[i] = static_cast<std::uint32_t>(word);
        }
        state.front() = randomSeparation;
        state.rear() = 0;
        for (std::uint32_t i = 0; i < 10 * randomDegree; ++i) {
            state.next();
        }
        return state;
    }
};

using RandomStateBytes = std::array<std::byte, randomStateSize>;

RandomStateBytes encode(const RandomState& state) {
    RandomStateBytes bytes{};
    for (std::size_t i = 0; i < state.words.size(); ++i) {
        for (std::size_t b = 0; b < 4; ++b) {
            bytes[i * 4 + b] = static_cast<std::byte>(state.words[i] >> (8 * b));
        }
    }
    return bytes;
}

RandomState decode(const RandomStateBytes& bytes) {
    RandomState state;
    for (std::size_t i = 0; i < state.words.size(); ++i) {
        for (std::size_t b = 0; b < 4; ++b) {
            state.words[i] |= std::to_integer<std::uint32_t>(bytes[i * 4 + b]) << (8 * b);
        }
    }
    return state;
}

LibraryObject randomStateObject(const TargetInfo& /*target*/) {
    return {.size = randomStateSize, .alignment = 4, .initialize = [](LibraryContext& context, Address address) {
                initializeObject(context, address, encode(RandomState::seeded(1)));
            }};
}

std::int32_t nextRandom(const Call& call) {
    const Address address = call.objectAddress("__random_state");
    RandomStateBytes bytes{};
    call.read(address, bytes);
    RandomState state = decode(bytes);
    const std::int32_t result = state.next();
    call.write(address, encode(state));
    return result;
}

LibraryResult rand(const Call& call) {
    return call.returnInt(nextRandom(call));
}

LibraryResult random(const Call& call) {
    return call.returnInteger(static_cast<UInt128>(Int128{nextRandom(call)}), ScalarType::Long);
}

/// srand and srandom.
LibraryResult srand(const Call& call) {
    const auto seed = static_cast<std::uint32_t>(call.integerArgument(0, ScalarType::UnsignedInt));
    call.write(call.objectAddress("__random_state"), encode(RandomState::seeded(seed)));
    return Call::returnVoid();
}

constexpr FunctionEntry functions[] = {
    {"__VERIFIER_error", entryPoint<abort>},
    {"__VERIFIER_atomic_begin", entryPoint<doNothing>},
    {"__VERIFIER_atomic_end", entryPoint<doNothing>},
    {"__assert_fail", entryPoint<assertFail>},
    {"__assert", entryPoint<bsdAssert>},
    {"__assert_perror_fail", entryPoint<abort>},
    {"abort", entryPoint<abort>},
    {"exit", entryPoint<exit>},
    {"_Exit", entryPoint<exit>},
    {"_exit", entryPoint<exit>},
    {"quick_exit", entryPoint<exit>},
    {"__errno_location", entryPoint<errnoLocation>},
    {"strtol", entryPoint<strtol>},
    {"strtoul", entryPoint<strtoul>},
    {"strtoll", entryPoint<strtoll>},
    {"strtoull", entryPoint<strtoull>},
    {"strtoq", entryPoint<strtoll>},
    {"strtouq", entryPoint<strtoull>},
    {"strtoimax", entryPoint<strtoll>},
    {"strtoumax", entryPoint<strtoull>},
    {"__isoc23_strtol", entryPoint<isoc23Strtol>},
    {"__isoc23_strtoul", entryPoint<isoc23Strtoul>},
    {"__isoc23_strtoll", entryPoint<isoc23Strtoll>},
    {"__isoc23_strtoull", entryPoint<isoc23Strtoull>},
    {"__isoc23_strtoimax", entryPoint<isoc23Strtoll>},
    {"__isoc23_strtoumax", entryPoint<isoc23Strtoull>},
    {"atoi", entryPoint<atoi>},
    {"atol", entryPoint<atol>},
    {"atoll", entryPoint<atoll>},
    {"abs", entryPoint<abs>},
    {"labs", entryPoint<labs>},
    {"llabs", entryPoint<llabs>},
    {"imaxabs", entryPoint<llabs>},
    {"div", entryPoint<div>},
    {"ldiv", entryPoint<ldiv>},
    {"lldiv", entryPoint<lldiv>},
    {"imaxdiv", entryPoint<lldiv>},
    {"rand", entryPoint<rand>},
    {"random", entryPoint<random>},
    {"srand", entryPoint<srand>},
    {"srandom", entryPoint<srand>},
};

constexpr ObjectEntry objects[] = {
    {"errno", errnoObject},
    {"__random_state", randomStateObject},
};

} // namespace

std::span<const FunctionEntry> stdlibFunctions() {
    return functions;
}

std::span<const ObjectEntry> stdlibObjects() {
    return objects;
}

} // namespace nitwit::interp::library
