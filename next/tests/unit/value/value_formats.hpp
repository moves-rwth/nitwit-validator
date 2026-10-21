#pragma once

// Integer formats and value constructors shared by the tests of the value module.

#include <array>
#include <format>
#include <ostream>
#include <string>

#include "value/value.hpp"

namespace nitwit::value::test {

constexpr IntegerFormat s8{.bits = 8, .isSigned = true};
constexpr IntegerFormat u8{.bits = 8, .isSigned = false};
constexpr IntegerFormat s16{.bits = 16, .isSigned = true};
constexpr IntegerFormat u16{.bits = 16, .isSigned = false};
constexpr IntegerFormat s32{.bits = 32, .isSigned = true};
constexpr IntegerFormat u32{.bits = 32, .isSigned = false};
constexpr IntegerFormat s64{.bits = 64, .isSigned = true};
constexpr IntegerFormat u64{.bits = 64, .isSigned = false};
constexpr IntegerFormat s128{.bits = 128, .isSigned = true};
constexpr IntegerFormat u128{.bits = 128, .isSigned = false};

constexpr std::array allFormats{s8, u8, s16, u16, s32, u32, s64, u64, s128, u128};

inline std::string formatName(IntegerFormat format) {
    return std::format("{}{}", format.isSigned ? 's' : 'u', format.bits);
}

/// An integer of `format` from a mathematical value; out-of-range values wrap.
inline Value integer(Int128 value, IntegerFormat format) {
    return Value::integer(static_cast<UInt128>(value), format);
}

/// The largest value of `format` in canonical form.
inline UInt128 maxBits(IntegerFormat format) {
    const unsigned valueBits = format.isSigned ? format.bits - 1u : format.bits;
    return valueBits == 128 ? ~UInt128{0} : (UInt128{1} << valueBits) - 1;
}

/// The smallest value of `format` in canonical form.
inline UInt128 minBits(IntegerFormat format) {
    return format.isSigned ? ~UInt128{0} << (format.bits - 1) : 0;
}

} // namespace nitwit::value::test

namespace nitwit::value {

/// Makes GoogleTest print formats as "s32" instead of their bytes.
inline void PrintTo(IntegerFormat format, std::ostream* out) {
    *out << test::formatName(format);
}

} // namespace nitwit::value
