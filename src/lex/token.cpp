#include "lex/token.hpp"

#include <utility>

#include "support/assert.hpp"

namespace nitwit::lex {

std::string_view spelling(TokenKind kind) {
    switch (kind) {
    case TokenKind::EndOfFile:
        return "end of file";
    case TokenKind::Unknown:
        return "unknown token";
    case TokenKind::Pragma:
        return "#pragma";
    case TokenKind::Identifier:
        return "identifier";
    case TokenKind::IntegerLiteral:
        return "integer literal";
    case TokenKind::FloatingLiteral:
        return "floating literal";
    case TokenKind::CharacterLiteral:
        return "character literal";
    case TokenKind::StringLiteral:
        return "string literal";

    case TokenKind::KwAuto:
        return "auto";
    case TokenKind::KwBreak:
        return "break";
    case TokenKind::KwCase:
        return "case";
    case TokenKind::KwChar:
        return "char";
    case TokenKind::KwConst:
        return "const";
    case TokenKind::KwContinue:
        return "continue";
    case TokenKind::KwDefault:
        return "default";
    case TokenKind::KwDo:
        return "do";
    case TokenKind::KwDouble:
        return "double";
    case TokenKind::KwElse:
        return "else";
    case TokenKind::KwEnum:
        return "enum";
    case TokenKind::KwExtern:
        return "extern";
    case TokenKind::KwFloat:
        return "float";
    case TokenKind::KwFor:
        return "for";
    case TokenKind::KwGoto:
        return "goto";
    case TokenKind::KwIf:
        return "if";
    case TokenKind::KwInline:
        return "inline";
    case TokenKind::KwInt:
        return "int";
    case TokenKind::KwLong:
        return "long";
    case TokenKind::KwRegister:
        return "register";
    case TokenKind::KwRestrict:
        return "restrict";
    case TokenKind::KwReturn:
        return "return";
    case TokenKind::KwShort:
        return "short";
    case TokenKind::KwSigned:
        return "signed";
    case TokenKind::KwSizeof:
        return "sizeof";
    case TokenKind::KwStatic:
        return "static";
    case TokenKind::KwStruct:
        return "struct";
    case TokenKind::KwSwitch:
        return "switch";
    case TokenKind::KwTypedef:
        return "typedef";
    case TokenKind::KwUnion:
        return "union";
    case TokenKind::KwUnsigned:
        return "unsigned";
    case TokenKind::KwVoid:
        return "void";
    case TokenKind::KwVolatile:
        return "volatile";
    case TokenKind::KwWhile:
        return "while";
    case TokenKind::KwAlignas:
        return "_Alignas";
    case TokenKind::KwAlignof:
        return "_Alignof";
    case TokenKind::KwAtomic:
        return "_Atomic";
    case TokenKind::KwBool:
        return "_Bool";
    case TokenKind::KwComplex:
        return "_Complex";
    case TokenKind::KwGeneric:
        return "_Generic";
    case TokenKind::KwImaginary:
        return "_Imaginary";
    case TokenKind::KwNoreturn:
        return "_Noreturn";
    case TokenKind::KwStaticAssert:
        return "_Static_assert";
    case TokenKind::KwThreadLocal:
        return "_Thread_local";

    case TokenKind::KwAsm:
        return "asm";
    case TokenKind::KwAttribute:
        return "__attribute__";
    case TokenKind::KwExtension:
        return "__extension__";
    case TokenKind::KwTypeof:
        return "typeof";
    case TokenKind::KwInt128:
        return "__int128";
    case TokenKind::KwFloat128:
        return "_Float128";
    case TokenKind::KwFloat32:
        return "_Float32";
    case TokenKind::KwFloat64:
        return "_Float64";
    case TokenKind::KwFloat32x:
        return "_Float32x";
    case TokenKind::KwFloat64x:
        return "_Float64x";
    case TokenKind::KwReal:
        return "__real__";
    case TokenKind::KwImag:
        return "__imag__";
    case TokenKind::KwLabel:
        return "__label__";
    case TokenKind::KwAutoType:
        return "__auto_type";
    case TokenKind::KwBuiltinVaArg:
        return "__builtin_va_arg";
    case TokenKind::KwBuiltinOffsetof:
        return "__builtin_offsetof";
    case TokenKind::KwBuiltinTypesCompatibleP:
        return "__builtin_types_compatible_p";
    case TokenKind::KwBuiltinChooseExpr:
        return "__builtin_choose_expr";

    case TokenKind::LeftSquare:
        return "[";
    case TokenKind::RightSquare:
        return "]";
    case TokenKind::LeftParen:
        return "(";
    case TokenKind::RightParen:
        return ")";
    case TokenKind::LeftBrace:
        return "{";
    case TokenKind::RightBrace:
        return "}";
    case TokenKind::Period:
        return ".";
    case TokenKind::Arrow:
        return "->";
    case TokenKind::PlusPlus:
        return "++";
    case TokenKind::MinusMinus:
        return "--";
    case TokenKind::Amp:
        return "&";
    case TokenKind::Star:
        return "*";
    case TokenKind::Plus:
        return "+";
    case TokenKind::Minus:
        return "-";
    case TokenKind::Tilde:
        return "~";
    case TokenKind::Exclaim:
        return "!";
    case TokenKind::Slash:
        return "/";
    case TokenKind::Percent:
        return "%";
    case TokenKind::LessLess:
        return "<<";
    case TokenKind::GreaterGreater:
        return ">>";
    case TokenKind::Less:
        return "<";
    case TokenKind::Greater:
        return ">";
    case TokenKind::LessEqual:
        return "<=";
    case TokenKind::GreaterEqual:
        return ">=";
    case TokenKind::EqualEqual:
        return "==";
    case TokenKind::ExclaimEqual:
        return "!=";
    case TokenKind::Caret:
        return "^";
    case TokenKind::Pipe:
        return "|";
    case TokenKind::AmpAmp:
        return "&&";
    case TokenKind::PipePipe:
        return "||";
    case TokenKind::Question:
        return "?";
    case TokenKind::Colon:
        return ":";
    case TokenKind::Semicolon:
        return ";";
    case TokenKind::Ellipsis:
        return "...";
    case TokenKind::Equal:
        return "=";
    case TokenKind::StarEqual:
        return "*=";
    case TokenKind::SlashEqual:
        return "/=";
    case TokenKind::PercentEqual:
        return "%=";
    case TokenKind::PlusEqual:
        return "+=";
    case TokenKind::MinusEqual:
        return "-=";
    case TokenKind::LessLessEqual:
        return "<<=";
    case TokenKind::GreaterGreaterEqual:
        return ">>=";
    case TokenKind::AmpEqual:
        return "&=";
    case TokenKind::CaretEqual:
        return "^=";
    case TokenKind::PipeEqual:
        return "|=";
    case TokenKind::Comma:
        return ",";
    }
    NITWIT_UNREACHABLE("invalid token kind");
}

bool isKeyword(TokenKind kind) {
    return std::to_underlying(kind) >= std::to_underlying(TokenKind::KwAuto)
           && std::to_underlying(kind) <= std::to_underlying(TokenKind::KwBuiltinChooseExpr);
}

bool isPunctuator(TokenKind kind) {
    return std::to_underlying(kind) >= std::to_underlying(TokenKind::LeftSquare)
           && std::to_underlying(kind) <= std::to_underlying(TokenKind::Comma);
}

} // namespace nitwit::lex
