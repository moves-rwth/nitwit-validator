// Character classification and conversion (C17 7.4) in the C locale, and the glibc tables that the macros of glibc's
// <ctype.h> read through __ctype_b_loc, __ctype_tolower_loc and __ctype_toupper_loc.

#include <array>
#include <cstdint>
#include <format>
#include <string_view>

#include "interp/library_call.hpp"

namespace nitwit::interp::library {

namespace {

// glibc's class bits (<ctype.h>, _ISbit on a little-endian machine).
constexpr std::uint16_t upperBit = 0x100;
constexpr std::uint16_t lowerBit = 0x200;
constexpr std::uint16_t alphaBit = 0x400;
constexpr std::uint16_t digitBit = 0x800;
constexpr std::uint16_t xdigitBit = 0x1000;
constexpr std::uint16_t spaceBit = 0x2000;
constexpr std::uint16_t printBit = 0x4000;
constexpr std::uint16_t graphBit = 0x8000;
constexpr std::uint16_t blankBit = 0x1;
constexpr std::uint16_t cntrlBit = 0x2;
constexpr std::uint16_t punctBit = 0x4;
constexpr std::uint16_t alnumBit = 0x8;

// The tables cover the arguments -128..255, so that a plain char works as an index as well as EOF (-1) and an
// unsigned char. The pointer that __ctype_b_loc and friends point to points at the entry for 0.
constexpr int tableFirst = -128;
constexpr int tableEnd = 256;
constexpr std::size_t tableEntries = tableEnd - tableFirst;

/// The class bits of `c` in the C locale: ASCII only, nothing for EOF and the bytes above 127.
constexpr std::uint16_t classOf(int c) {
    if (c < 0 || c > 127) {
        return 0;
    }
    const bool upper = c >= 'A' && c <= 'Z';
    const bool lower = c >= 'a' && c <= 'z';
    const bool digit = c >= '0' && c <= '9';
    const bool xdigit = digit || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
    const bool space = c == ' ' || (c >= '\t' && c <= '\r');
    const bool print = c >= ' ' && c <= '~';
    const bool graph = print && c != ' ';
    const bool alnum = upper || lower || digit;
    std::uint16_t bits = 0;
    bits |= upper ? upperBit : 0;
    bits |= lower ? lowerBit : 0;
    bits |= upper || lower ? alphaBit : 0;
    bits |= digit ? digitBit : 0;
    bits |= xdigit ? xdigitBit : 0;
    bits |= space ? spaceBit : 0;
    bits |= print ? printBit : 0;
    bits |= graph ? graphBit : 0;
    bits |= c == ' ' || c == '\t' ? blankBit : 0;
    bits |= c < ' ' || c == 127 ? cntrlBit : 0;
    bits |= graph && !alnum ? punctBit : 0;
    bits |= alnum ? alnumBit : 0;
    return bits;
}

/// The entries of glibc's tolower and toupper tables. They map the negative arguments -128..-2 to the unsigned char
/// with the same bits; EOF stays EOF.
constexpr std::int32_t convertCase(int c, char from, char to) {
    if (c >= from && c < from + 26) {
        return c - from + to;
    }
    return c < -1 ? c + 256 : c;
}

constexpr std::int32_t lowerOf(int c) {
    return convertCase(c, 'A', 'a');
}

constexpr std::int32_t upperOf(int c) {
    return convertCase(c, 'a', 'A');
}

// ---- The library objects: the tables, and the pointers to their entry for 0 that the *_loc functions return ----

constexpr std::string_view classTable = "_nl_C_LC_CTYPE_class";
constexpr std::string_view lowerTable = "_nl_C_LC_CTYPE_tolower";
constexpr std::string_view upperTable = "_nl_C_LC_CTYPE_toupper";
constexpr std::string_view classPointer = "__ctype_b";
constexpr std::string_view lowerPointer = "__ctype_tolower";
constexpr std::string_view upperPointer = "__ctype_toupper";

/// A table with entries of `entrySize` bytes, `entry(c)` for c = -128..255, little-endian.
template <std::size_t entrySize, auto entry>
void initializeTable(LibraryContext& context, Address address) {
    std::array<std::byte, tableEntries * entrySize> bytes{};
    for (std::size_t i = 0; i < tableEntries; ++i) {
        const auto bits = static_cast<std::uint32_t>(entry(static_cast<int>(i) + tableFirst));
        for (std::size_t b = 0; b < entrySize; ++b) {
            bytes[i * entrySize + b] = static_cast<std::byte>(bits >> (8 * b));
        }
    }
    initializeObject(context, address, bytes);
}

template <std::size_t entrySize, auto entry>
LibraryObject table(const TargetInfo& /*target*/) {
    // Read-only, as in glibc's .rodata.
    return {.size = tableEntries * entrySize,
            .alignment = entrySize,
            .readOnly = true,
            .initialize = initializeTable<entrySize, entry>};
}

/// A pointer variable (__ctype_b, ...) that points to the entry for 0 of `tableName`.
template <const std::string_view& tableName, std::size_t entrySize>
LibraryObject pointerToTable(const TargetInfo& target) {
    const std::uint64_t size = target.layout(ScalarType::Pointer).size;
    return {.size = size, .alignment = size, .initialize = [](LibraryContext& context, Address address) {
                constexpr std::uint64_t entriesBeforeZero = -tableFirst;
                initializePointer(context, address, context.objectAddress(tableName) + entriesBeforeZero * entrySize);
            }};
}

// ---- The functions ----

/// The argument of a classification function: an unsigned char or EOF (C17 7.4p1). glibc's tables also cover the
/// negative values of a signed char.
int characterArgument(const Call& call) {
    const int c = call.intArgument(0);
    if (c < tableFirst || c >= tableEnd) {
        call.undefinedBehavior(
            std::format("{}({}): the argument is neither an unsigned char nor EOF (C17 7.4p1)", call.name(), c));
    }
    return c;
}

/// isalpha and friends return the class bit, as glibc's do, not 1.
template <std::uint16_t bit>
LibraryResult classify(const Call& call) {
    return call.returnInt(classOf(characterArgument(call)) & bit);
}

/// glibc's tolower and toupper return arguments outside the tables unchanged.
template <std::int32_t (*convert)(int)>
LibraryResult convertCharacter(const Call& call) {
    const int c = call.intArgument(0);
    return call.returnInt(c >= tableFirst && c < tableEnd ? convert(c) : c);
}

/// GCC expands __builtin_isdigit to (unsigned)(c - '0') <= 9: 1, not glibc's class bit, and no table lookup.
LibraryResult builtinIsdigit(const Call& call) {
    const auto offset = static_cast<unsigned>(call.intArgument(0)) - unsigned{'0'};
    return call.returnInt(offset <= 9 ? 1 : 0);
}

LibraryResult isascii(const Call& call) {
    return call.returnInt((call.intArgument(0) & ~0x7F) == 0 ? 1 : 0);
}

LibraryResult toascii(const Call& call) {
    return call.returnInt(call.intArgument(0) & 0x7F);
}

template <const std::string_view& pointer>
LibraryResult tableLocation(const Call& call) {
    return call.returnPointer(call.objectAddress(pointer));
}

/// MB_CUR_MAX: 1 in the C locale.
LibraryResult ctypeGetMbCurMax(const Call& call) {
    return call.returnSize(1);
}

constexpr FunctionEntry functions[] = {
    {"__builtin_isdigit", entryPoint<builtinIsdigit>}, // the other __builtin_ forms call the functions
    {"isalnum", entryPoint<classify<alnumBit>>},
    {"isalpha", entryPoint<classify<alphaBit>>},
    {"isblank", entryPoint<classify<blankBit>>},
    {"iscntrl", entryPoint<classify<cntrlBit>>},
    {"isdigit", entryPoint<classify<digitBit>>},
    {"isgraph", entryPoint<classify<graphBit>>},
    {"islower", entryPoint<classify<lowerBit>>},
    {"isprint", entryPoint<classify<printBit>>},
    {"ispunct", entryPoint<classify<punctBit>>},
    {"isspace", entryPoint<classify<spaceBit>>},
    {"isupper", entryPoint<classify<upperBit>>},
    {"isxdigit", entryPoint<classify<xdigitBit>>},
    {"tolower", entryPoint<convertCharacter<lowerOf>>},
    {"toupper", entryPoint<convertCharacter<upperOf>>},
    {"isascii", entryPoint<isascii>},
    {"toascii", entryPoint<toascii>},
    {"__ctype_b_loc", entryPoint<tableLocation<classPointer>>},
    {"__ctype_tolower_loc", entryPoint<tableLocation<lowerPointer>>},
    {"__ctype_toupper_loc", entryPoint<tableLocation<upperPointer>>},
    {"__ctype_get_mb_cur_max", entryPoint<ctypeGetMbCurMax>},
};

constexpr ObjectEntry objects[] = {
    {classTable, table<2, classOf>},
    {lowerTable, table<4, lowerOf>},
    {upperTable, table<4, upperOf>},
    {classPointer, pointerToTable<classTable, 2>},
    {lowerPointer, pointerToTable<lowerTable, 4>},
    {upperPointer, pointerToTable<upperTable, 4>},
};

} // namespace

std::span<const FunctionEntry> ctypeFunctions() {
    return functions;
}

std::span<const ObjectEntry> ctypeObjects() {
    return objects;
}

} // namespace nitwit::interp::library
