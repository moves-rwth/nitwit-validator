#pragma once

// Decoding of the spellings of literal tokens (C17 6.4.4, 6.4.5). The parser and semantic analysis use the decoded
// values; choosing the type of a literal from its value and suffix happens in sema (C17 6.4.4.1p5).

#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <vector>

#include "support/error.hpp"

namespace nitwit::lex {

struct IntegerLiteral {
    std::uint64_t value = 0;
    bool isDecimal = false; // decimal constants without suffix u never get an unsigned type (C17 6.4.4.1p5)
    bool hasUnsignedSuffix = false;
    std::uint8_t longCount = 0; // 0, 1 (l, L) or 2 (ll, LL)
};

/// Decodes decimal, octal, hexadecimal and binary (0b, a GNU extension) integer constants with any suffix
/// combination of u/U and l/L/ll/LL. Fails on malformed spellings, on values above 2^64 - 1, and on imaginary
/// constants (suffix i or j, a GNU extension), which are not supported.
[[nodiscard]] std::expected<IntegerLiteral, Error> decodeIntegerLiteral(std::string_view spelling);

// The suffixes f/F; l/L; f128/F128/q/Q; f32/F32; f64/F64; f32x/F32x; f64x/F64x.
enum class FloatingSuffix : std::uint8_t { None, F, L, F128, F32, F64, F32x, F64x };

struct FloatingLiteral {
    FloatingSuffix suffix = FloatingSuffix::None;
    // The value rounded once, directly to the precision of the literal's type. Only one member is set: `floatValue`
    // for F and F32, `doubleValue` for None, F64 and F32x, `longDoubleValue` for L, F64x and F128. F128 values are
    // rounded to the x87 long double, not to binary128.
    float floatValue = 0;
    double doubleValue = 0;
    long double longDoubleValue = 0;
};

/// Decodes decimal and hexadecimal floating constants. Rounding is to nearest, as GCC does. Values beyond the range
/// of the type become infinity or zero, which GCC accepts with a warning. The suffix w (__float80) and imaginary
/// constants (suffix i or j) are not supported.
[[nodiscard]] std::expected<FloatingLiteral, Error> decodeFloatingLiteral(std::string_view spelling);

enum class CharEncoding : std::uint8_t { Plain, Wide, Utf8, Utf16, Utf32 }; // none, L, u8, u, U

struct CharacterLiteral {
    CharEncoding encoding = CharEncoding::Plain;
    /// The value of the constant: for a plain constant the int value as GCC computes it on x86 (a single char is
    /// sign-extended; multi-character constants combine bytes big-endian); for prefixed constants the code unit.
    std::int64_t value = 0;
};

/// Decodes a character constant, including all escape sequences of C17 6.4.4.4 and GNU's `\e`. An octal or
/// hexadecimal escape whose value does not fit in a code unit is an error here, while GCC only warns.
[[nodiscard]] std::expected<CharacterLiteral, Error> decodeCharacterLiteral(std::string_view spelling);

struct StringLiteral {
    CharEncoding encoding = CharEncoding::Plain;
    /// The code units without the terminating zero: bytes for Plain and Utf8, UTF-16 code units for Utf16, 32-bit
    /// units for Wide and Utf32. A \u or \U escape in a narrow string is encoded as UTF-8.
    std::vector<std::uint32_t> codeUnits;
};

/// Decodes one or more adjacent string literal tokens into one string (C17 6.4.5p5). The prefixed tokens determine
/// the encoding, and mixing different prefixes is an error. Every token is then decoded in that encoding, as GCC
/// does: in `L"a" "\xff"`, the escape gives the wide code unit 0xFF. Escapes out of range are errors, as in
/// decodeCharacterLiteral.
[[nodiscard]] std::expected<StringLiteral, Error> decodeStringLiteral(std::span<const std::string_view> spellings);

} // namespace nitwit::lex
