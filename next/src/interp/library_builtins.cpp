// GCC's integer and miscellaneous builtins ("Other Built-in Functions Provided by GCC", "Built-in Functions to Perform
// Arithmetic with Overflow Checking"), and the byte-order functions of <arpa/inet.h>. The __builtin_ forms of the C
// library functions are found by findLibraryFunction.

#include <bit>
#include <cstdint>
#include <format>
#include <utility>

#include "interp/library_call.hpp"

namespace nitwit::interp::library {

namespace {

/// __builtin_expect(value, expected) returns `value`; an unmodified copy, so it keeps the origin. Also
/// __builtin_expect_with_probability and __builtin_assume_aligned (pointer, alignment, ...).
template <ScalarType type>
LibraryResult passThrough(const Call& call) {
    value::Value result = value::Value::integer(call.integerArgument(0, type), call.format(type));
    result.setOrigin(call.argument(0).origin());
    return LibraryResult::returning(std::move(result));
}

LibraryResult unreachable(const Call& call) {
    call.undefinedBehavior(std::format("{} was reached", call.name()));
}

LibraryResult trap(const Call& call) {
    return ending(LibraryResult::Kind::Abort, std::format("{} was called", call.name()));
}

/// 0, as GCC at -O0 gives for arguments that are not constant expressions. The library sees only values, so a
/// constant argument must be folded before the call to give 1.
LibraryResult constantP(const Call& call) {
    return call.returnInt(0);
}

/// GCC at -O0 knows no object sizes: (size_t)-1 for the types 0 and 1 (maximum), 0 for 2 and 3 (minimum).
LibraryResult objectSize(const Call& call) {
    const int type = call.intArgument(1);
    return call.returnSize((type & 2) == 0 ? ~std::uint64_t{0} : 0);
}

// ---- Bits ----

/// The bits of the argument as `type`, without sign extension, and the width of `type`.
template <ScalarType type>
std::pair<UInt128, unsigned> argumentBits(const Call& call) {
    const unsigned width = call.format(type).bits;
    const UInt128 mask = width == 128 ? ~UInt128{0} : (UInt128{1} << width) - 1;
    return {call.integerArgument(0, type) & mask, width};
}

template <ScalarType type>
LibraryResult byteSwap(const Call& call) {
    const auto [bits, width] = argumentBits<type>(call);
    UInt128 swapped = 0;
    for (unsigned byte = 0; byte < width / 8; ++byte) {
        swapped = (swapped << 8) | ((bits >> (8 * byte)) & 0xFF);
    }
    return call.returnInteger(swapped, type);
}

/// Undefined behaviour for 0 (GCC's manual on __builtin_clz and __builtin_ctz).
template <ScalarType type>
LibraryResult countLeadingZeros(const Call& call) {
    const auto [bits, width] = argumentBits<type>(call);
    if (bits == 0) {
        call.undefinedBehavior(std::format("{} of 0", call.name()));
    }
    const auto high = static_cast<std::uint64_t>(bits >> 64);
    const auto low = static_cast<std::uint64_t>(bits);
    const int zerosIn128 = high != 0 ? std::countl_zero(high) : 64 + std::countl_zero(low);
    return call.returnInt(zerosIn128 - static_cast<int>(128 - width));
}

template <ScalarType type>
LibraryResult countTrailingZeros(const Call& call) {
    const auto [bits, width] = argumentBits<type>(call);
    if (bits == 0) {
        call.undefinedBehavior(std::format("{} of 0", call.name()));
    }
    const auto high = static_cast<std::uint64_t>(bits >> 64);
    const auto low = static_cast<std::uint64_t>(bits);
    return call.returnInt(low != 0 ? std::countr_zero(low) : 64 + std::countr_zero(high));
}

int popcount(UInt128 bits) {
    return std::popcount(static_cast<std::uint64_t>(bits >> 64)) + std::popcount(static_cast<std::uint64_t>(bits));
}

template <ScalarType type>
LibraryResult popcount(const Call& call) {
    return call.returnInt(popcount(argumentBits<type>(call).first));
}

template <ScalarType type>
LibraryResult parity(const Call& call) {
    return call.returnInt(popcount(argumentBits<type>(call).first) % 2);
}

/// ffs (also glibc's function): one plus the index of the least significant 1 bit, or 0.
template <ScalarType type>
LibraryResult findFirstSet(const Call& call) {
    const auto [bits, width] = argumentBits<type>(call);
    if (bits == 0) {
        return call.returnInt(0);
    }
    const auto high = static_cast<std::uint64_t>(bits >> 64);
    const auto low = static_cast<std::uint64_t>(bits);
    return call.returnInt(1 + (low != 0 ? std::countr_zero(low) : 64 + std::countr_zero(high)));
}

/// __builtin_clrsb: the number of bits after the sign bit that equal it.
template <ScalarType type>
LibraryResult leadingRedundantSignBits(const Call& call) {
    const auto [bits, width] = argumentBits<type>(call);
    const bool negative = ((bits >> (width - 1)) & 1) != 0;
    int count = 0;
    for (int bit = static_cast<int>(width) - 2; bit >= 0 && (((bits >> bit) & 1) != 0) == negative; --bit) {
        ++count;
    }
    return call.returnInt(count);
}

// ---- Arithmetic with overflow checking ----

enum class Operation : std::uint8_t { Add, Sub, Mul };

/// __builtin_sadd_overflow(a, b, &result) and the other typed forms: store the result wrapped to `type` and return
/// whether the exact result does not fit. 128 bits hold the exact result of every 64-bit operation.
template <Operation operation, ScalarType type>
LibraryResult overflow(const Call& call) {
    const value::IntegerFormat format = call.format(type);
    const Address result = call.pointerArgument(2);
    UInt128 bits = 0;
    bool overflowed = false;
    if (format.isSigned) {
        const Int128 a = call.signedArgument(0, type);
        const Int128 b = call.signedArgument(1, type);
        const Int128 exact = operation == Operation::Add ? a + b : operation == Operation::Sub ? a - b : a * b;
        const Int128 limit = Int128{1} << (format.bits - 1);
        overflowed = exact < -limit || exact >= limit;
        bits = static_cast<UInt128>(exact);
    } else {
        const UInt128 a = call.integerArgument(0, type);
        const UInt128 b = call.integerArgument(1, type);
        bits = operation == Operation::Add ? a + b : operation == Operation::Sub ? a - b : a * b;
        overflowed = operation == Operation::Sub ? a < b : (bits >> format.bits) != 0;
    }
    call.writeInteger(result, bits, type);
    return call.returnInteger(overflowed ? 1 : 0, ScalarType::Bool);
}

constexpr FunctionEntry functions[] = {
    {"__builtin_expect", entryPoint<passThrough<ScalarType::Long>>},
    {"__builtin_expect_with_probability", entryPoint<passThrough<ScalarType::Long>>},
    {"__builtin_assume_aligned", entryPoint<passThrough<ScalarType::Pointer>>},
    {"__builtin_unreachable", entryPoint<unreachable>},
    {"__builtin_trap", entryPoint<trap>},
    {"__builtin_prefetch", entryPoint<doNothing>},
    {"__builtin_constant_p", entryPoint<constantP>},
    {"__builtin_object_size", entryPoint<objectSize>},
    {"__builtin_dynamic_object_size", entryPoint<objectSize>},
    {"__builtin_bswap16", entryPoint<byteSwap<ScalarType::UnsignedShort>>},
    {"__builtin_bswap32", entryPoint<byteSwap<ScalarType::UnsignedInt>>},
    {"__builtin_bswap64", entryPoint<byteSwap<ScalarType::UnsignedLongLong>>},
    {"__builtin_bswap128", entryPoint<byteSwap<ScalarType::UnsignedInt128>>},
    // x86 is little-endian, so network byte order is the swapped order.
    {"htons", entryPoint<byteSwap<ScalarType::UnsignedShort>>},
    {"ntohs", entryPoint<byteSwap<ScalarType::UnsignedShort>>},
    {"htonl", entryPoint<byteSwap<ScalarType::UnsignedInt>>},
    {"ntohl", entryPoint<byteSwap<ScalarType::UnsignedInt>>},
    {"__builtin_clz", entryPoint<countLeadingZeros<ScalarType::UnsignedInt>>},
    {"__builtin_clzl", entryPoint<countLeadingZeros<ScalarType::UnsignedLong>>},
    {"__builtin_clzll", entryPoint<countLeadingZeros<ScalarType::UnsignedLongLong>>},
    {"__builtin_ctz", entryPoint<countTrailingZeros<ScalarType::UnsignedInt>>},
    {"__builtin_ctzl", entryPoint<countTrailingZeros<ScalarType::UnsignedLong>>},
    {"__builtin_ctzll", entryPoint<countTrailingZeros<ScalarType::UnsignedLongLong>>},
    {"__builtin_clrsb", entryPoint<leadingRedundantSignBits<ScalarType::Int>>},
    {"__builtin_clrsbl", entryPoint<leadingRedundantSignBits<ScalarType::Long>>},
    {"__builtin_clrsbll", entryPoint<leadingRedundantSignBits<ScalarType::LongLong>>},
    {"__builtin_popcount", entryPoint<popcount<ScalarType::UnsignedInt>>},
    {"__builtin_popcountl", entryPoint<popcount<ScalarType::UnsignedLong>>},
    {"__builtin_popcountll", entryPoint<popcount<ScalarType::UnsignedLongLong>>},
    {"__builtin_parity", entryPoint<parity<ScalarType::UnsignedInt>>},
    {"__builtin_parityl", entryPoint<parity<ScalarType::UnsignedLong>>},
    {"__builtin_parityll", entryPoint<parity<ScalarType::UnsignedLongLong>>},
    {"ffs", entryPoint<findFirstSet<ScalarType::Int>>},
    {"ffsl", entryPoint<findFirstSet<ScalarType::Long>>},
    {"ffsll", entryPoint<findFirstSet<ScalarType::LongLong>>},
    {"__builtin_sadd_overflow", entryPoint<overflow<Operation::Add, ScalarType::Int>>},
    {"__builtin_saddl_overflow", entryPoint<overflow<Operation::Add, ScalarType::Long>>},
    {"__builtin_saddll_overflow", entryPoint<overflow<Operation::Add, ScalarType::LongLong>>},
    {"__builtin_uadd_overflow", entryPoint<overflow<Operation::Add, ScalarType::UnsignedInt>>},
    {"__builtin_uaddl_overflow", entryPoint<overflow<Operation::Add, ScalarType::UnsignedLong>>},
    {"__builtin_uaddll_overflow", entryPoint<overflow<Operation::Add, ScalarType::UnsignedLongLong>>},
    {"__builtin_ssub_overflow", entryPoint<overflow<Operation::Sub, ScalarType::Int>>},
    {"__builtin_ssubl_overflow", entryPoint<overflow<Operation::Sub, ScalarType::Long>>},
    {"__builtin_ssubll_overflow", entryPoint<overflow<Operation::Sub, ScalarType::LongLong>>},
    {"__builtin_usub_overflow", entryPoint<overflow<Operation::Sub, ScalarType::UnsignedInt>>},
    {"__builtin_usubl_overflow", entryPoint<overflow<Operation::Sub, ScalarType::UnsignedLong>>},
    {"__builtin_usubll_overflow", entryPoint<overflow<Operation::Sub, ScalarType::UnsignedLongLong>>},
    {"__builtin_smul_overflow", entryPoint<overflow<Operation::Mul, ScalarType::Int>>},
    {"__builtin_smull_overflow", entryPoint<overflow<Operation::Mul, ScalarType::Long>>},
    {"__builtin_smulll_overflow", entryPoint<overflow<Operation::Mul, ScalarType::LongLong>>},
    {"__builtin_umul_overflow", entryPoint<overflow<Operation::Mul, ScalarType::UnsignedInt>>},
    {"__builtin_umull_overflow", entryPoint<overflow<Operation::Mul, ScalarType::UnsignedLong>>},
    {"__builtin_umulll_overflow", entryPoint<overflow<Operation::Mul, ScalarType::UnsignedLongLong>>},
};

} // namespace

std::span<const FunctionEntry> builtinFunctions() {
    return functions;
}

} // namespace nitwit::interp::library
