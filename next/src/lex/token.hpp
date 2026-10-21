#pragma once

// Tokens of preprocessed C17 with the GNU extensions found in sv-benchmarks.

#include <cstdint>
#include <string_view>

#include "support/interner.hpp"
#include "support/source_manager.hpp"

namespace nitwit::lex {

enum class TokenKind : std::uint8_t {
    EndOfFile,
    Unknown, // a character that starts no token; the lexer reports it

    Identifier,
    IntegerLiteral,   // spelling decoded by decodeIntegerLiteral
    FloatingLiteral,  // spelling decoded by decodeFloatingLiteral
    CharacterLiteral, // spelling decoded by decodeCharacterLiteral, including the prefix L, u, U or u8
    StringLiteral,    // spelling decoded by decodeStringLiteral, including the prefix

    // Keywords of C17. GNU spellings map to the same kind, e.g. `__inline__` and `__inline` to KwInline.
    KwAuto,
    KwBreak,
    KwCase,
    KwChar,
    KwConst, // also __const, __const__
    KwContinue,
    KwDefault,
    KwDo,
    KwDouble,
    KwElse,
    KwEnum,
    KwExtern,
    KwFloat,
    KwFor,
    KwGoto,
    KwIf,
    KwInline, // also __inline, __inline__
    KwInt,
    KwLong,
    KwRegister,
    KwRestrict, // also __restrict, __restrict__
    KwReturn,
    KwShort,
    KwSigned, // also __signed, __signed__
    KwSizeof,
    KwStatic,
    KwStruct,
    KwSwitch,
    KwTypedef,
    KwUnion,
    KwUnsigned,
    KwVoid,
    KwVolatile, // also __volatile, __volatile__
    KwWhile,
    KwAlignas,      // _Alignas
    KwAlignof,      // _Alignof, also __alignof, __alignof__
    KwAtomic,       // _Atomic
    KwBool,         // _Bool
    KwComplex,      // _Complex, also __complex__
    KwGeneric,      // _Generic
    KwImaginary,    // _Imaginary
    KwNoreturn,     // _Noreturn
    KwStaticAssert, // _Static_assert
    KwThreadLocal,  // _Thread_local, also __thread

    // GNU extensions.
    KwAsm,                     // asm, __asm, __asm__
    KwAttribute,               // __attribute, __attribute__
    KwExtension,               // __extension__
    KwTypeof,                  // typeof, __typeof, __typeof__
    KwInt128,                  // __int128
    KwFloat128,                // _Float128, __float128
    KwFloat32,                 // _Float32
    KwFloat64,                 // _Float64
    KwFloat32x,                // _Float32x
    KwFloat64x,                // _Float64x
    KwReal,                    // __real, __real__
    KwImag,                    // __imag, __imag__
    KwLabel,                   // __label__
    KwAutoType,                // __auto_type
    KwBuiltinVaArg,            // __builtin_va_arg
    KwBuiltinOffsetof,         // __builtin_offsetof
    KwBuiltinTypesCompatibleP, // __builtin_types_compatible_p
    KwBuiltinChooseExpr,       // __builtin_choose_expr

    // Punctuators; digraphs map to the kind of the token they stand for, e.g. `<:` to LeftSquare.
    LeftSquare,          // [
    RightSquare,         // ]
    LeftParen,           // (
    RightParen,          // )
    LeftBrace,           // {
    RightBrace,          // }
    Period,              // .
    Arrow,               // ->
    PlusPlus,            // ++
    MinusMinus,          // --
    Amp,                 // &
    Star,                // *
    Plus,                // +
    Minus,               // -
    Tilde,               // ~
    Exclaim,             // !
    Slash,               // /
    Percent,             // %
    LessLess,            // <<
    GreaterGreater,      // >>
    Less,                // <
    Greater,             // >
    LessEqual,           // <=
    GreaterEqual,        // >=
    EqualEqual,          // ==
    ExclaimEqual,        // !=
    Caret,               // ^
    Pipe,                // |
    AmpAmp,              // &&
    PipePipe,            // ||
    Question,            // ?
    Colon,               // :
    Semicolon,           // ;
    Ellipsis,            // ...
    Equal,               // =
    StarEqual,           // *=
    SlashEqual,          // /=
    PercentEqual,        // %=
    PlusEqual,           // +=
    MinusEqual,          // -=
    LessLessEqual,       // <<=
    GreaterGreaterEqual, // >>=
    AmpEqual,            // &=
    CaretEqual,          // ^=
    PipeEqual,           // |=
    Comma,               // ,
};

/// The canonical spelling of a keyword or punctuator, or a description such as "identifier" for the other kinds.
[[nodiscard]] std::string_view spelling(TokenKind kind);

[[nodiscard]] bool isKeyword(TokenKind kind);

struct Token {
    TokenKind kind = TokenKind::EndOfFile;
    bool atStartOfLine = false;   // first token of its physical line
    bool hasLeadingSpace = false; // preceded by whitespace or a comment on the same line
    std::uint32_t length = 0;     // bytes of the spelling in the source
    SourceLocation location;      // first byte of the spelling
    Symbol identifier;            // the name, for TokenKind::Identifier only

    [[nodiscard]] bool is(TokenKind k) const { return kind == k; }
    [[nodiscard]] SourceRange range() const { return {location, {location.file, location.offset + length}}; }
};

} // namespace nitwit::lex
