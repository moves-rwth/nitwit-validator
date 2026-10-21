#pragma once

// Decoding of the spellings of literal tokens (C17 6.4.4, 6.4.5). The parser and semantic analysis use the decoded
// values; choosing the type of a literal from its value and suffix happens in sema (C17 6.4.4.1p5).

#include <cstdint>
#include <expected>
#include <string>
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
/// combination of u/U and l/L/ll/LL. Fails on malformed spellings and on values above 2^64 - 1.
[[nodiscard]] std::expected<IntegerLiteral, Error> decodeIntegerLiteral(std::string_view spelling);

enum class FloatingSuffix : std::uint8_t { None, F, L, F128 }; // f/F; l/L; f128/F128/q/Q

struct FloatingLiteral {
    FloatingSuffix suffix = FloatingSuffix::None;
    // The value rounded once, directly to the precision of the literal's type: only the member for `suffix` is set
    // (`longDoubleValue` for L and F128).
    float floatValue = 0;
    double doubleValue = 0;
    long double longDoubleValue = 0;
};

/// Decodes decimal and hexadecimal floating constants. Rounding is to nearest, as GCC does.
[[nodiscard]] std::expected<FloatingLiteral, Error> decodeFloatingLiteral(std::string_view spelling);

enum class CharEncoding : std::uint8_t { Plain, Wide, Utf8, Utf16, Utf32 }; // none, L, u8, u, U

struct CharacterLiteral {
    CharEncoding encoding = CharEncoding::Plain;
    /// The value of the constant: for a plain constant the int value as GCC computes it on x86 (a single char is
    /// sign-extended; multi-character constants combine bytes big-endian); for prefixed constants the code unit.
    std::int64_t value = 0;
};

/// Decodes a character constant, including all escape sequences of C17 6.4.4.4 and GNU's `\e`.
[[nodiscard]] std::expected<CharacterLiteral, Error> decodeCharacterLiteral(std::string_view spelling);

struct StringLiteral {
    CharEncoding encoding = CharEncoding::Plain;
    /// The code units without the terminating zero: bytes for Plain and Utf8, UTF-16 code units for Utf16, 32-bit
    /// units for Wide and Utf32. A \u or \U escape in a narrow string is encoded as UTF-8.
    std::vector<std::uint32_t> codeUnits;
};

/// Decodes one string literal token. Concatenating adjacent literals is the parser's job (concatenateStrings).
[[nodiscard]] std::expected<StringLiteral, Error> decodeStringLiteral(std::string_view spelling);

/// Concatenates adjacent string literals (C17 6.4.5p5): a prefixed literal determines the encoding; mixing different
/// prefixes is an error.
[[nodiscard]] std::expected<StringLiteral, Error> concatenateStrings(const std::vector<StringLiteral>& parts);

} // namespace nitwit::lex
