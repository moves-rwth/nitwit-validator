#include "lex/literals.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

#include "support/assert.hpp"

namespace nitwit::lex {

namespace {

std::unexpected<Error> failure(std::string message) {
    return std::unexpected(Error{std::move(message)});
}

// The value of a hexadecimal digit, or 16 for any other character.
unsigned digitValue(char c) {
    if (c >= '0' && c <= '9') {
        return static_cast<unsigned>(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return static_cast<unsigned>(c - 'a' + 10);
    }
    if (c >= 'A' && c <= 'F') {
        return static_cast<unsigned>(c - 'A' + 10);
    }
    return 16;
}

bool isDecimalDigit(char c) {
    return c >= '0' && c <= '9';
}

bool isHexDigit(char c) {
    return digitValue(c) < 16;
}

// ---- Integer constants (C17 6.4.4.1)

// The suffix: u or U, and l, L, ll or LL, in either order.
bool parseIntegerSuffix(std::string_view suffix, IntegerLiteral& literal) {
    std::size_t i = 0;
    const auto takeUnsigned = [&] {
        if (i < suffix.size() && (suffix[i] == 'u' || suffix[i] == 'U')) {
            literal.hasUnsignedSuffix = true;
            ++i;
        }
    };
    takeUnsigned();
    if (i < suffix.size() && (suffix[i] == 'l' || suffix[i] == 'L')) {
        const bool isLongLong = i + 1 < suffix.size() && suffix[i + 1] == suffix[i]; // ll or LL, but not lL
        literal.longCount = isLongLong ? 2 : 1;
        i += literal.longCount;
    }
    if (!literal.hasUnsignedSuffix) {
        takeUnsigned();
    }
    return i == suffix.size();
}

// ---- Floating constants (C17 6.4.4.2)

struct FloatingParts {
    bool isHex = false;
    std::string_view number;   // the input of from_chars: significand and exponent, without the prefix 0x
    std::string_view whole;    // the digits before the point
    std::string_view fraction; // the digits after the point
    std::int64_t exponent = 0; // saturated far beyond the range of any floating type
    std::string_view suffix;
};

std::expected<FloatingParts, Error> splitFloating(std::string_view spelling) {
    FloatingParts parts;
    std::size_t i = 0;
    if (spelling.starts_with("0x") || spelling.starts_with("0X")) {
        parts.isHex = true;
        i = 2;
    }
    const std::size_t numberStart = i;
    const auto isMantissaDigit = parts.isHex ? isHexDigit : isDecimalDigit;
    const auto digits = [&] {
        const std::size_t start = i;
        while (i < spelling.size() && isMantissaDigit(spelling[i])) {
            ++i;
        }
        return spelling.substr(start, i - start);
    };
    parts.whole = digits();
    if (i < spelling.size() && spelling[i] == '.') {
        ++i;
        parts.fraction = digits();
    }
    if (parts.whole.empty() && parts.fraction.empty()) {
        return failure(std::format("floating constant '{}' has no digits", spelling));
    }

    const auto isExponentLetter = [&](char c) {
        return parts.isHex ? c == 'p' || c == 'P' : c == 'e' || c == 'E';
    };
    if (i < spelling.size() && isExponentLetter(spelling[i])) {
        ++i;
        const bool isNegative = i < spelling.size() && spelling[i] == '-';
        if (i < spelling.size() && (spelling[i] == '+' || spelling[i] == '-')) {
            ++i;
        }
        const std::size_t exponentStart = i;
        for (; i < spelling.size() && isDecimalDigit(spelling[i]); ++i) {
            parts.exponent = std::min<std::int64_t>(parts.exponent * 10 + (spelling[i] - '0'), 1'000'000'000'000);
        }
        if (i == exponentStart) {
            return failure(std::format("exponent of floating constant '{}' has no digits", spelling));
        }
        if (isNegative) {
            parts.exponent = -parts.exponent;
        }
    } else if (parts.isHex) {
        return failure(std::format("hexadecimal floating constant '{}' has no exponent", spelling));
    }
    parts.number = spelling.substr(numberStart, i - numberStart);
    parts.suffix = spelling.substr(i);
    return parts;
}

std::optional<FloatingSuffix> parseFloatingSuffix(std::string_view suffix) {
    if (suffix.empty()) {
        return FloatingSuffix::None;
    }
    if (suffix == "f" || suffix == "F") {
        return FloatingSuffix::F;
    }
    if (suffix == "l" || suffix == "L") {
        return FloatingSuffix::L;
    }
    if (suffix == "f128" || suffix == "F128" || suffix == "q" || suffix == "Q") {
        return FloatingSuffix::F128;
    }
    return std::nullopt;
}

// Whether the magnitude of a nonzero constant is at least 1, judged from its first nonzero digit and its exponent. It
// tells overflow from underflow, where the magnitude is far from 1.
bool isAtLeastOne(const FloatingParts& parts) {
    std::int64_t digitPower = 0; // the power of the base of the first nonzero digit
    if (const std::size_t inWhole = parts.whole.find_first_not_of('0'); inWhole != std::string_view::npos) {
        digitPower = static_cast<std::int64_t>(parts.whole.size() - inWhole) - 1;
    } else if (const std::size_t inFraction = parts.fraction.find_first_not_of('0');
               inFraction != std::string_view::npos) {
        digitPower = -static_cast<std::int64_t>(inFraction) - 1;
    } else {
        return false;
    }
    // A hexadecimal digit is four bits, and the exponent of a hexadecimal constant is binary.
    return digitPower * (parts.isHex ? 4 : 1) + parts.exponent >= 0;
}

// Rounds the constant once, to nearest, directly to T.
template <typename T>
std::expected<T, Error> convertFloating(const FloatingParts& parts) {
    T value{};
    const char* first = parts.number.data();
    const char* last = first + parts.number.size();
    const auto result =
        std::from_chars(first, last, value, parts.isHex ? std::chars_format::hex : std::chars_format::general);
    if (result.ec == std::errc::result_out_of_range) {
        // from_chars reports values that round to zero or to infinity as out of range. GCC uses them with a warning.
        return isAtLeastOne(parts) ? std::numeric_limits<T>::infinity() : T{0};
    }
    if (result.ec != std::errc{} || result.ptr != last) {
        return failure(std::format("invalid floating constant '{}'", parts.number));
    }
    return value;
}

// ---- Character constants and string literals (C17 6.4.4.4, 6.4.5)

// The width of a code unit: char for no prefix and u8, char16_t for u, char32_t for U, and wchar_t (a 32-bit int on
// x86 Linux) for L.
unsigned codeUnitBits(CharEncoding encoding) {
    switch (encoding) {
    case CharEncoding::Plain:
    case CharEncoding::Utf8:
        return 8;
    case CharEncoding::Utf16:
        return 16;
    case CharEncoding::Wide:
    case CharEncoding::Utf32:
        return 32;
    }
    NITWIT_UNREACHABLE("invalid character encoding");
}

bool isNarrow(CharEncoding encoding) {
    return codeUnitBits(encoding) == 8;
}

void appendCodePoint(std::vector<std::uint32_t>& units, std::uint32_t codePoint, CharEncoding encoding) {
    switch (encoding) {
    case CharEncoding::Plain:
    case CharEncoding::Utf8:
        if (codePoint < 0x80) {
            units.push_back(codePoint);
        } else if (codePoint < 0x800) {
            units.push_back(0xC0 | (codePoint >> 6));
            units.push_back(0x80 | (codePoint & 0x3F));
        } else if (codePoint < 0x10000) {
            units.push_back(0xE0 | (codePoint >> 12));
            units.push_back(0x80 | ((codePoint >> 6) & 0x3F));
            units.push_back(0x80 | (codePoint & 0x3F));
        } else {
            units.push_back(0xF0 | (codePoint >> 18));
            units.push_back(0x80 | ((codePoint >> 12) & 0x3F));
            units.push_back(0x80 | ((codePoint >> 6) & 0x3F));
            units.push_back(0x80 | (codePoint & 0x3F));
        }
        return;
    case CharEncoding::Utf16:
        if (codePoint >= 0x10000) {
            const std::uint32_t offset = codePoint - 0x10000;
            units.push_back(0xD800 | (offset >> 10));
            units.push_back(0xDC00 | (offset & 0x3FF));
        } else {
            units.push_back(codePoint);
        }
        return;
    case CharEncoding::Wide:
    case CharEncoding::Utf32:
        units.push_back(codePoint);
        return;
    }
}

// Decodes the UTF-8 sequence at text[i] and advances i past it; nullopt if the sequence is not valid UTF-8.
std::optional<std::uint32_t> decodeUtf8(std::string_view text, std::size_t& i) {
    const auto lead = static_cast<unsigned char>(text[i]);
    std::size_t length = 1;
    std::uint32_t codePoint = lead;
    std::uint32_t minimum = 0;
    if (lead >= 0xF0 && lead < 0xF8) {
        length = 4;
        codePoint = lead & 0x07u;
        minimum = 0x10000;
    } else if (lead >= 0xE0 && lead < 0xF0) {
        length = 3;
        codePoint = lead & 0x0Fu;
        minimum = 0x800;
    } else if (lead >= 0xC0 && lead < 0xE0) {
        length = 2;
        codePoint = lead & 0x1Fu;
        minimum = 0x80;
    } else if (lead >= 0x80) {
        return std::nullopt;
    }
    if (text.size() - i < length) {
        return std::nullopt;
    }
    for (std::size_t k = 1; k < length; ++k) {
        const auto byte = static_cast<unsigned char>(text[i + k]);
        if ((byte & 0xC0) != 0x80) {
            return std::nullopt;
        }
        codePoint = (codePoint << 6) | (byte & 0x3Fu);
    }
    if (codePoint < minimum || codePoint > 0x10FFFF || (codePoint >= 0xD800 && codePoint <= 0xDFFF)) {
        return std::nullopt; // overlong, beyond Unicode, or a surrogate
    }
    i += length;
    return codePoint;
}

struct Escape {
    std::uint32_t value = 0;
    bool isCodePoint = false; // a character, encoded like a source character; otherwise one code unit
};

// A universal character name (C17 6.4.3) with `digits` hexadecimal digits at text[i].
std::expected<Escape, Error> decodeUniversalCharacter(std::string_view text, std::size_t& i, std::size_t digits) {
    std::uint32_t value = 0;
    for (std::size_t k = 0; k < digits; ++k) {
        if (i >= text.size() || !isHexDigit(text[i])) {
            return failure(std::format("incomplete universal character name: \\{} needs {} hexadecimal digits",
                                       digits == 4 ? 'u' : 'U', digits));
        }
        value = value * 16 + digitValue(text[i++]);
    }
    // C17 6.4.3p2: no character below U+00A0 other than $, @ and `, and no surrogate. GCC also rejects values beyond
    // U+10FFFF.
    if ((value < 0xA0 && value != '$' && value != '@' && value != '`') || (value >= 0xD800 && value <= 0xDFFF)
        || value > 0x10FFFF) {
        return failure(
            std::format("\\{}{:0{}X} is not a valid universal character", digits == 4 ? 'u' : 'U', value, digits));
    }
    return Escape{.value = value, .isCodePoint = true};
}

// The escape sequence whose backslash is at text[i] (C17 6.4.4.4); advances i past it.
std::expected<Escape, Error> decodeEscape(std::string_view text, std::size_t& i, unsigned unitBits) {
    const std::uint64_t maxUnit = (std::uint64_t{1} << unitBits) - 1;
    ++i;
    if (i >= text.size()) {
        return failure("incomplete escape sequence");
    }
    const char c = text[i++];
    switch (c) {
    case '\'':
    case '"':
    case '?':
    case '\\':
        return Escape{.value = static_cast<std::uint32_t>(c)};
    case 'a':
        return Escape{.value = 0x07};
    case 'b':
        return Escape{.value = 0x08};
    case 'f':
        return Escape{.value = 0x0C};
    case 'n':
        return Escape{.value = 0x0A};
    case 'r':
        return Escape{.value = 0x0D};
    case 't':
        return Escape{.value = 0x09};
    case 'v':
        return Escape{.value = 0x0B};
    case 'e': // GNU
    case 'E':
        return Escape{.value = 0x1B};
    case 'u':
        return decodeUniversalCharacter(text, i, 4);
    case 'U':
        return decodeUniversalCharacter(text, i, 8);
    case 'x': {
        const std::size_t start = i;
        std::uint64_t value = 0;
        for (; i < text.size() && isHexDigit(text[i]); ++i) {
            value = std::min(value * 16 + digitValue(text[i]), maxUnit + 1); // saturates, so it cannot wrap
        }
        if (i == start) {
            return failure("\\x used with no following hexadecimal digits");
        }
        if (value > maxUnit) {
            return failure("hexadecimal escape sequence out of range");
        }
        return Escape{.value = static_cast<std::uint32_t>(value)};
    }
    default:
        break;
    }
    if (c >= '0' && c <= '7') {
        auto value = static_cast<std::uint32_t>(c - '0');
        for (int k = 0; k < 2 && i < text.size() && text[i] >= '0' && text[i] <= '7'; ++k) {
            value = value * 8 + static_cast<std::uint32_t>(text[i++] - '0');
        }
        if (value > maxUnit) {
            return failure("octal escape sequence out of range");
        }
        return Escape{.value = value};
    }
    // GCC accepts an unknown escape sequence with a warning and uses the character itself.
    --i;
    const auto codePoint = decodeUtf8(text, i);
    if (!codePoint) {
        return failure("invalid UTF-8 after a backslash");
    }
    return Escape{.value = *codePoint, .isCodePoint = true};
}

struct LiteralText {
    CharEncoding encoding = CharEncoding::Plain;
    std::string body; // between the quotes, without backslash-newlines
};

// Splits a literal into its encoding prefix and its body, and removes the backslash-newlines from the body
// (C17 5.1.1.2p1, phase 2).
std::expected<LiteralText, Error> splitLiteral(std::string_view spelling, char quote) {
    LiteralText text;
    std::size_t prefixLength = 1;
    if (spelling.starts_with("u8")) {
        text.encoding = CharEncoding::Utf8;
        prefixLength = 2;
    } else if (spelling.starts_with('L')) {
        text.encoding = CharEncoding::Wide;
    } else if (spelling.starts_with('u')) {
        text.encoding = CharEncoding::Utf16;
    } else if (spelling.starts_with('U')) {
        text.encoding = CharEncoding::Utf32;
    } else {
        prefixLength = 0;
    }
    if (spelling.size() < prefixLength + 2 || spelling[prefixLength] != quote || spelling.back() != quote) {
        return failure(
            std::format("malformed {} {}", quote == '"' ? "string literal" : "character constant", spelling));
    }
    const std::string_view body = spelling.substr(prefixLength + 1, spelling.size() - prefixLength - 2);
    text.body.reserve(body.size());
    for (std::size_t i = 0; i < body.size(); ++i) {
        if (body[i] == '\\' && body.substr(i + 1).starts_with('\n')) {
            i += 1;
        } else if (body[i] == '\\' && body.substr(i + 1).starts_with("\r\n")) {
            i += 2;
        } else {
            text.body += body[i];
        }
    }
    return text;
}

// Appends the code units of a literal body, decoded in `encoding`, to `units`.
std::expected<void, Error> decodeBody(std::string_view body, CharEncoding encoding, char quote,
                                      std::vector<std::uint32_t>& units) {
    std::size_t i = 0;
    while (i < body.size()) {
        const char c = body[i];
        if (c == '\\') {
            const auto escape = decodeEscape(body, i, codeUnitBits(encoding));
            if (!escape) {
                return std::unexpected(escape.error());
            }
            if (escape->isCodePoint) {
                appendCodePoint(units, escape->value, encoding);
            } else {
                units.push_back(escape->value);
            }
        } else if (c == quote || c == '\n') {
            return failure(c == '\n' ? "newline in literal" : std::format("unescaped {} in literal", quote));
        } else if (isNarrow(encoding) || static_cast<unsigned char>(c) < 0x80) {
            units.push_back(static_cast<unsigned char>(c));
            ++i;
        } else {
            // A wide literal holds the code points of the source characters, which are UTF-8 (GCC's default).
            const auto codePoint = decodeUtf8(body, i);
            if (!codePoint) {
                return failure("invalid UTF-8 in wide literal");
            }
            appendCodePoint(units, *codePoint, encoding);
        }
    }
    return {};
}

} // namespace

std::expected<IntegerLiteral, Error> decodeIntegerLiteral(std::string_view spelling) {
    if (spelling.empty() || !isDecimalDigit(spelling[0])) {
        return failure(std::format("invalid integer constant '{}'", spelling));
    }
    unsigned base = 10;
    std::size_t i = 0;
    if (spelling.starts_with("0x") || spelling.starts_with("0X")) {
        base = 16;
        i = 2;
    } else if (spelling.starts_with("0b") || spelling.starts_with("0B")) {
        base = 2;
        i = 2;
    } else if (spelling.starts_with('0')) {
        base = 8; // including 0 itself (C17 6.4.4.1p1)
    }

    IntegerLiteral literal;
    literal.isDecimal = base == 10;
    const std::size_t digitsStart = i;
    bool tooLarge = false;
    for (; i < spelling.size(); ++i) {
        const unsigned digit = digitValue(spelling[i]);
        if (digit >= base) {
            if (digit < 10) {
                return failure(
                    std::format("invalid digit '{}' in {} constant", spelling[i], base == 8 ? "octal" : "binary"));
            }
            break;
        }
        if (literal.value > (std::numeric_limits<std::uint64_t>::max() - digit) / base) {
            tooLarge = true;
        } else {
            literal.value = literal.value * base + digit;
        }
    }
    if (i == digitsStart) {
        return failure(std::format("integer constant '{}' has no digits", spelling));
    }
    if (!parseIntegerSuffix(spelling.substr(i), literal)) {
        return failure(std::format("invalid suffix '{}' on integer constant", spelling.substr(i)));
    }
    if (tooLarge) {
        return failure(std::format("integer constant '{}' is too large", spelling));
    }
    return literal;
}

std::expected<FloatingLiteral, Error> decodeFloatingLiteral(std::string_view spelling) {
    const auto parts = splitFloating(spelling);
    if (!parts) {
        return std::unexpected(parts.error());
    }
    const auto suffix = parseFloatingSuffix(parts->suffix);
    if (!suffix) {
        return failure(std::format("invalid suffix '{}' on floating constant", parts->suffix));
    }

    FloatingLiteral literal;
    literal.suffix = *suffix;
    switch (*suffix) {
    case FloatingSuffix::None: {
        const auto value = convertFloating<double>(*parts);
        if (!value) {
            return std::unexpected(value.error());
        }
        literal.doubleValue = *value;
        break;
    }
    case FloatingSuffix::F: {
        const auto value = convertFloating<float>(*parts);
        if (!value) {
            return std::unexpected(value.error());
        }
        literal.floatValue = *value;
        break;
    }
    case FloatingSuffix::L:
    case FloatingSuffix::F128: {
        const auto value = convertFloating<long double>(*parts);
        if (!value) {
            return std::unexpected(value.error());
        }
        literal.longDoubleValue = *value;
        break;
    }
    }
    return literal;
}

std::expected<CharacterLiteral, Error> decodeCharacterLiteral(std::string_view spelling) {
    const auto text = splitLiteral(spelling, '\'');
    if (!text) {
        return std::unexpected(text.error());
    }
    std::vector<std::uint32_t> units;
    if (const auto decoded = decodeBody(text->body, text->encoding, '\'', units); !decoded) {
        return std::unexpected(decoded.error());
    }
    if (units.empty()) {
        return failure("empty character constant");
    }

    CharacterLiteral literal;
    literal.encoding = text->encoding;
    switch (text->encoding) {
    case CharEncoding::Plain:
        if (units.size() == 1) {
            literal.value = static_cast<std::int8_t>(units.front()); // plain char is signed on x86
        } else {
            // GCC: an int whose bytes are the chars, the first one most significant; beyond four chars, it warns
            // and keeps the last four.
            std::uint32_t value = 0;
            for (const std::uint32_t unit : units) {
                value = (value << 8) | unit;
            }
            literal.value = static_cast<std::int32_t>(value);
        }
        break;
    case CharEncoding::Utf8:
        if (units.size() > 1) {
            return failure("u8 character constant does not fit in one code unit");
        }
        literal.value = units.front();
        break;
    case CharEncoding::Utf16:
    case CharEncoding::Utf32:
        // GCC warns about more than one code unit and uses the last one.
        literal.value = units.back();
        break;
    case CharEncoding::Wide:
        literal.value = static_cast<std::int32_t>(units.back()); // wchar_t is int
        break;
    }
    return literal;
}

std::expected<StringLiteral, Error> decodeStringLiteral(std::span<const std::string_view> spellings) {
    StringLiteral result;
    std::vector<LiteralText> parts;
    parts.reserve(spellings.size());
    for (const std::string_view spelling : spellings) {
        auto part = splitLiteral(spelling, '"');
        if (!part) {
            return std::unexpected(part.error());
        }
        // C17 6.4.5p5: a prefix applies to the whole string. GCC rejects different prefixes.
        if (part->encoding != CharEncoding::Plain) {
            if (result.encoding != CharEncoding::Plain && result.encoding != part->encoding) {
                return failure("concatenation of string literals with different encoding prefixes");
            }
            result.encoding = part->encoding;
        }
        parts.push_back(std::move(*part));
    }
    for (const LiteralText& part : parts) {
        if (const auto decoded = decodeBody(part.body, result.encoding, '"', result.codeUnits); !decoded) {
            return std::unexpected(decoded.error());
        }
    }
    return result;
}

} // namespace nitwit::lex
