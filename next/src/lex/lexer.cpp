#include "lex/lexer.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iterator>
#include <span>
#include <string>
#include <system_error>
#include <utility>

#include "lex/literals.hpp"

namespace nitwit::lex {

namespace {

constexpr std::size_t npos = std::string_view::npos;

// Character classes of source bytes.
constexpr std::uint8_t identifierStartClass = 1; // letters, '_', '$', and bytes >= 0x80 (GCC accepts UTF-8)
constexpr std::uint8_t digitClass = 2;
constexpr std::uint8_t blankClass = 4; // whitespace other than '\n'

constexpr std::array<std::uint8_t, 256> charClasses = [] {
    std::array<std::uint8_t, 256> classes{};
    for (std::size_t c = 0; c < classes.size(); ++c) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$' || c >= 0x80) {
            classes[c] = identifierStartClass;
        } else if (c >= '0' && c <= '9') {
            classes[c] = digitClass;
        } else if (c == ' ' || c == '\t' || c == '\v' || c == '\f' || c == '\r') {
            classes[c] = blankClass;
        }
    }
    return classes;
}();

bool hasClass(char c, unsigned classes) {
    return (charClasses[static_cast<unsigned char>(c)] & classes) != 0;
}

bool isIdentifierStart(char c) {
    return hasClass(c, identifierStartClass);
}

bool isIdentifierContinue(char c) {
    return hasClass(c, identifierStartClass | digitClass);
}

bool isDigit(char c) {
    return hasClass(c, digitClass);
}

bool isBlank(char c) {
    return hasClass(c, blankClass);
}

struct Keyword {
    std::string_view spelling;
    TokenKind kind;
};

constexpr Keyword keywords[] = {
    {"auto", TokenKind::KwAuto},
    {"break", TokenKind::KwBreak},
    {"case", TokenKind::KwCase},
    {"char", TokenKind::KwChar},
    {"const", TokenKind::KwConst},
    {"__const", TokenKind::KwConst},
    {"__const__", TokenKind::KwConst},
    {"continue", TokenKind::KwContinue},
    {"default", TokenKind::KwDefault},
    {"do", TokenKind::KwDo},
    {"double", TokenKind::KwDouble},
    {"else", TokenKind::KwElse},
    {"enum", TokenKind::KwEnum},
    {"extern", TokenKind::KwExtern},
    {"float", TokenKind::KwFloat},
    {"for", TokenKind::KwFor},
    {"goto", TokenKind::KwGoto},
    {"if", TokenKind::KwIf},
    {"inline", TokenKind::KwInline},
    {"__inline", TokenKind::KwInline},
    {"__inline__", TokenKind::KwInline},
    {"int", TokenKind::KwInt},
    {"long", TokenKind::KwLong},
    {"register", TokenKind::KwRegister},
    {"restrict", TokenKind::KwRestrict},
    {"__restrict", TokenKind::KwRestrict},
    {"__restrict__", TokenKind::KwRestrict},
    {"return", TokenKind::KwReturn},
    {"short", TokenKind::KwShort},
    {"signed", TokenKind::KwSigned},
    {"__signed", TokenKind::KwSigned},
    {"__signed__", TokenKind::KwSigned},
    {"sizeof", TokenKind::KwSizeof},
    {"static", TokenKind::KwStatic},
    {"struct", TokenKind::KwStruct},
    {"switch", TokenKind::KwSwitch},
    {"typedef", TokenKind::KwTypedef},
    {"union", TokenKind::KwUnion},
    {"unsigned", TokenKind::KwUnsigned},
    {"void", TokenKind::KwVoid},
    {"volatile", TokenKind::KwVolatile},
    {"__volatile", TokenKind::KwVolatile},
    {"__volatile__", TokenKind::KwVolatile},
    {"while", TokenKind::KwWhile},
    {"_Alignas", TokenKind::KwAlignas},
    {"_Alignof", TokenKind::KwAlignof},
    {"__alignof", TokenKind::KwAlignof},
    {"__alignof__", TokenKind::KwAlignof},
    {"_Atomic", TokenKind::KwAtomic},
    {"_Bool", TokenKind::KwBool},
    {"_Complex", TokenKind::KwComplex},
    {"__complex", TokenKind::KwComplex},
    {"__complex__", TokenKind::KwComplex},
    {"_Generic", TokenKind::KwGeneric},
    {"_Imaginary", TokenKind::KwImaginary},
    {"_Noreturn", TokenKind::KwNoreturn},
    {"_Static_assert", TokenKind::KwStaticAssert},
    {"_Thread_local", TokenKind::KwThreadLocal},
    {"__thread", TokenKind::KwThreadLocal},
    {"asm", TokenKind::KwAsm},
    {"__asm", TokenKind::KwAsm},
    {"__asm__", TokenKind::KwAsm},
    {"__attribute", TokenKind::KwAttribute},
    {"__attribute__", TokenKind::KwAttribute},
    {"__extension__", TokenKind::KwExtension},
    {"typeof", TokenKind::KwTypeof},
    {"__typeof", TokenKind::KwTypeof},
    {"__typeof__", TokenKind::KwTypeof},
    {"__int128", TokenKind::KwInt128},
    {"_Float128", TokenKind::KwFloat128},
    {"__float128", TokenKind::KwFloat128},
    {"_Float32", TokenKind::KwFloat32},
    {"_Float64", TokenKind::KwFloat64},
    {"_Float32x", TokenKind::KwFloat32x},
    {"_Float64x", TokenKind::KwFloat64x},
    {"__real", TokenKind::KwReal},
    {"__real__", TokenKind::KwReal},
    {"__imag", TokenKind::KwImag},
    {"__imag__", TokenKind::KwImag},
    {"__label__", TokenKind::KwLabel},
    {"__auto_type", TokenKind::KwAutoType},
    {"__builtin_va_arg", TokenKind::KwBuiltinVaArg},
    {"__builtin_offsetof", TokenKind::KwBuiltinOffsetof},
    {"__builtin_types_compatible_p", TokenKind::KwBuiltinTypesCompatibleP},
    {"__builtin_choose_expr", TokenKind::KwBuiltinChooseExpr},
};

// Keywords are found through a hash table with linear probing, built at compile time: a lookup hashes three
// characters of the name and compares it with the few keywords in its probe sequence.
constexpr std::size_t keywordSlots = 256; // a power of two, well above the number of keywords

constexpr std::size_t keywordHash(std::string_view name) {
    std::size_t hash = name.size();
    hash = hash * 31 + static_cast<unsigned char>(name.front());
    hash = hash * 31 + static_cast<unsigned char>(name[name.size() / 2]);
    hash = hash * 31 + static_cast<unsigned char>(name.back());
    return hash & (keywordSlots - 1);
}

// Each slot holds an index into `keywords` plus one; 0 marks an empty slot.
constexpr std::array<std::uint8_t, keywordSlots> keywordTable = [] {
    static_assert(std::size(keywords) < keywordSlots / 2);
    std::array<std::uint8_t, keywordSlots> table{};
    for (std::size_t i = 0; i < std::size(keywords); ++i) {
        std::size_t slot = keywordHash(keywords[i].spelling);
        while (table[slot] != 0) {
            slot = (slot + 1) & (keywordSlots - 1);
        }
        table[slot] = static_cast<std::uint8_t>(i + 1);
    }
    return table;
}();

// The keyword kind of an identifier spelling, or Identifier.
TokenKind keywordKind(std::string_view name) {
    for (std::size_t slot = keywordHash(name);; slot = (slot + 1) & (keywordSlots - 1)) {
        const std::uint8_t entry = keywordTable[slot];
        if (entry == 0) {
            return TokenKind::Identifier;
        }
        if (keywords[entry - 1].spelling == name) {
            return keywords[entry - 1].kind;
        }
    }
}

// The text of a stray character for a message, with non-printable bytes escaped.
std::string printable(std::string_view text) {
    std::string result;
    for (const char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte >= 0x20 && byte < 0x7F) {
            result += c;
        } else {
            result += std::format("\\x{:02x}", byte);
        }
    }
    return result;
}

// C17 6.10.4p3: the line number of #line is at most 2147483647. Line markers get the same limit here, although GCC
// accepts larger ones.
constexpr std::uint32_t maxLineNumber = 2147483647;

// Pairs of characters that start a punctuator longer than their first character, or a comment.
constexpr std::string_view joiningPairs[] = {
    "->", "++", "--", "<<", ">>", "<=", ">=", "==", "!=", "&&", "||", "*=", "/=", "%=",
    "+=", "-=", "&=", "^=", "|=", "..", "<:", ":>", "<%", "%>", "%:", "##", "//", "/*",
};

struct LiteralEnd {
    std::size_t end; // after the closing quote, or at the end of the line if there is none
    bool terminated;
};

class Lexer {
public:
    Lexer(SourceManager& sources, FileId file, StringInterner& interner, DiagnosticEngine& diagnostics)
        : sources_(sources), file_(file), text_(sources.contents(file)), interner_(interner), diagnostics_(diagnostics),
          presumedFileName_(sources.name(file)) {}

    LexResult run();

private:
    bool skipTrivia();
    void skipNullCharacters();
    void skipLineComment();
    void skipBlockComment();
    void skipSplicesBetweenTokens();
    [[nodiscard]] bool joinsPreviousToken(char next) const;

    [[nodiscard]] bool startsDirective() const;
    bool handleDirective();
    void lineMarker(bool isLineDirective);
    void invalidLineMarker(std::size_t at, std::string message);
    void skipDirectiveBlanks();
    void skipRestOfDirective();
    [[nodiscard]] bool atEndOfDirective() const { return pos_ >= text_.size() || text_[pos_] == '\n'; }

    void lexToken();
    void lexIdentifier(std::size_t start);
    void lexNumber(std::size_t start);
    void lexQuoted(std::size_t start, std::size_t quote);
    void lexRawString(std::size_t start, std::size_t quote);
    void lexPunctuator(TokenKind kind, std::size_t length);
    void lexUnknown(std::size_t length);
    Token& addToken(TokenKind kind, std::size_t start);

    [[nodiscard]] char peek(std::size_t ahead) const {
        return pos_ + ahead < text_.size() ? text_[pos_ + ahead] : '\0';
    }
    [[nodiscard]] std::size_t spliceLength(std::size_t at) const;
    std::size_t skipSplices(std::size_t at);
    [[nodiscard]] bool isContinuedLine(std::size_t newline) const;
    LiteralEnd findLiteralEnd(std::size_t quote);
    [[nodiscard]] SourceLocation locationAt(std::size_t offset) const {
        return {file_, static_cast<std::uint32_t>(offset)};
    }

    SourceManager& sources_;
    FileId file_;
    std::string_view text_;
    StringInterner& interner_;
    DiagnosticEngine& diagnostics_;
    std::string presumedFileName_; // kept by line markers without a file name

    std::vector<Token> tokens_;
    std::size_t pos_ = 0;
    std::size_t lastTokenEnd_ = npos;
    std::size_t nextSpliceWarning_ = 0; // splices before this offset have been warned about
    bool atStartOfLine_ = true;         // no token yet on the current physical line
    bool atStartOfLogicalLine_ = true;  // no token since the last newline outside comments and splices
    bool hasLeadingSpace_ = false;
};

LexResult Lexer::run() {
    // The corpus averages 0.28 tokens per byte (at most 0.51), so most files need no reallocation.
    tokens_.reserve(text_.size() / 3 + 1);
    if (text_.starts_with("\xEF\xBB\xBF")) {
        pos_ = 3; // GCC ignores a UTF-8 byte order mark
    }
    bool needsPreprocessing = false;
    while (true) {
        if (!skipTrivia()) {
            needsPreprocessing = true;
            break;
        }
        if (pos_ >= text_.size()) {
            break;
        }
        lexToken();
    }
    // When lexing stopped at a directive, the end-of-file token is at the directive.
    tokens_.push_back(Token{.kind = TokenKind::EndOfFile,
                            .atStartOfLine = atStartOfLine_,
                            .hasLeadingSpace = hasLeadingSpace_,
                            .length = 0,
                            .location = locationAt(pos_),
                            .identifier = {}});
    // The tokens live as long as the program is validated, so a reserve of more than twice the need is given back.
    // Shrinking copies all tokens (a third of the lexing time), so a smaller excess is kept.
    if (tokens_.capacity() > 2 * tokens_.size()) {
        tokens_.shrink_to_fit();
    }
    return LexResult{.tokens = std::move(tokens_), .needsPreprocessing = needsPreprocessing};
}

// Skips whitespace, comments, backslash-newlines and the directives handled here. Returns false at a directive that
// needs the preprocessor, with pos_ at its '#'.
bool Lexer::skipTrivia() {
    while (pos_ < text_.size()) {
        switch (text_[pos_]) {
        case '\n':
            ++pos_;
            atStartOfLine_ = true;
            atStartOfLogicalLine_ = true;
            hasLeadingSpace_ = false;
            break;
        case ' ':
        case '\t':
        case '\v':
        case '\f':
        case '\r':
            ++pos_;
            hasLeadingSpace_ = true;
            break;
        case '\0':
            skipNullCharacters();
            break;
        case '/':
            if (peek(1) == '/') {
                skipLineComment();
            } else if (peek(1) == '*') {
                skipBlockComment();
            } else {
                return true;
            }
            break;
        case '\\':
            if (spliceLength(pos_) == 0) {
                return true;
            }
            skipSplicesBetweenTokens();
            break;
        case '#':
        case '%':
            if (!startsDirective()) {
                return true;
            }
            if (!handleDirective()) {
                return false;
            }
            break;
        default:
            return true;
        }
    }
    return true;
}

// GCC ignores null characters outside literals, with a warning.
void Lexer::skipNullCharacters() {
    diagnostics_.warning(locationAt(pos_), "null character(s) ignored");
    while (pos_ < text_.size() && text_[pos_] == '\0') {
        ++pos_;
    }
    hasLeadingSpace_ = true;
}

// A line comment ends at the end of the line, but a backslash-newline continues it (C17 5.1.1.2p1, phase 2).
void Lexer::skipLineComment() {
    std::size_t newline = text_.find('\n', pos_ + 2);
    while (newline != npos && isContinuedLine(newline)) {
        newline = text_.find('\n', newline + 1);
    }
    pos_ = newline == npos ? text_.size() : newline;
    hasLeadingSpace_ = true;
}

// A block comment ends at the first '*' followed by '/', with or without backslash-newlines between them.
void Lexer::skipBlockComment() {
    const std::size_t start = pos_;
    for (std::size_t star = text_.find('*', start + 2); star != npos; star = text_.find('*', star + 1)) {
        std::size_t next = star + 1;
        while (const std::size_t length = spliceLength(next)) {
            next += length;
        }
        if (next < text_.size() && text_[next] == '/') {
            pos_ = next + 1;
            hasLeadingSpace_ = true;
            if (text_.substr(start, pos_ - start).contains('\n')) {
                atStartOfLine_ = true;
            }
            return;
        }
    }
    diagnostics_.error(locationAt(start), "unterminated comment");
    pos_ = text_.size();
}

// A backslash-newline between tokens is a line break of the physical line. If it joins the characters of two tokens,
// C makes them one token (C17 5.1.1.2p1, phase 2), which is not supported.
void Lexer::skipSplicesBetweenTokens() {
    const std::size_t next = skipSplices(pos_);
    if (pos_ == lastTokenEnd_ && next < text_.size() && joinsPreviousToken(text_[next])) {
        diagnostics_.error(locationAt(pos_), "backslash-newline inside a token is not supported");
    }
    pos_ = next;
    atStartOfLine_ = true;
    hasLeadingSpace_ = false;
}

// Whether the previous token and the character `next` would form one token if only backslash-newlines lie between
// them.
bool Lexer::joinsPreviousToken(char next) const {
    const Token& previous = tokens_.back();
    const std::string_view text = text_.substr(previous.location.offset, previous.length);
    const char last = text.back();
    if (isIdentifierContinue(last) && isIdentifierContinue(next)) {
        return true;
    }
    if (previous.is(TokenKind::IntegerLiteral) || previous.is(TokenKind::FloatingLiteral)) {
        // A pp-number continues with '.', and with a sign after an exponent letter (C17 6.4.8).
        const bool afterExponent = last == 'e' || last == 'E' || last == 'p' || last == 'P';
        if (next == '.' || (afterExponent && (next == '+' || next == '-'))) {
            return true;
        }
    }
    if (previous.is(TokenKind::Period) && isDigit(next)) {
        return true;
    }
    if (previous.is(TokenKind::Identifier) && (text == "L" || text == "u" || text == "U" || text == "u8")
        && (next == '"' || next == '\'')) {
        return true;
    }
    const std::array<char, 2> pair{last, next};
    return std::ranges::find(joiningPairs, std::string_view(pair.data(), pair.size())) != std::end(joiningPairs);
}

// A directive starts with '#' or '%:' as the first token of a logical line; '##' and '%:%:' are other tokens
// (C17 6.10p2).
bool Lexer::startsDirective() const {
    if (!atStartOfLogicalLine_) {
        return false;
    }
    if (text_[pos_] == '#') {
        return peek(1) != '#';
    }
    return peek(1) == ':' && !(peek(2) == '%' && peek(3) == ':');
}

// Handles the directive at pos_. Returns false, with pos_ at the '#', if the directive needs the preprocessor.
bool Lexer::handleDirective() {
    const std::size_t start = pos_;
    const bool atStartOfLine = atStartOfLine_; // comments inside the directive must not change the flags of a Pragma
    const bool hasLeadingSpace = hasLeadingSpace_;
    pos_ += text_[pos_] == '#' ? 1u : 2u;
    skipDirectiveBlanks();
    if (atEndOfDirective()) {
        return true; // the null directive (C17 6.10.7)
    }
    if (isDigit(text_[pos_])) {
        lineMarker(false);
        return true;
    }
    const std::size_t nameStart = pos_;
    while (pos_ < text_.size() && isIdentifierContinue(text_[pos_])) {
        ++pos_;
    }
    const std::string_view name = text_.substr(nameStart, pos_ - nameStart);
    if (name == "pragma") {
        skipRestOfDirective();
        if (text_[pos_ - 1] == '\r') {
            --pos_; // leave the "\r\n" line break to skipTrivia
        }
        atStartOfLine_ = atStartOfLine;
        hasLeadingSpace_ = hasLeadingSpace;
        addToken(TokenKind::Pragma, start);
        return true;
    }
    if (name == "ident") {
        skipRestOfDirective();
        return true;
    }
    if (name == "line") {
        skipDirectiveBlanks();
        lineMarker(true);
        return true;
    }
    pos_ = start;
    return false;
}

// A line marker `# 12 "a.c" 1 3` (GCC's output) or `#line 12 "a.c"` (C17 6.10.4), with pos_ at the line number: the
// line after the directive is line 12 of "a.c". Without a file name, the presumed file name stays the same.
void Lexer::lineMarker(bool isLineDirective) {
    const std::size_t numberStart = pos_;
    while (pos_ < text_.size() && isDigit(text_[pos_])) {
        ++pos_;
    }
    if (pos_ == numberStart || (pos_ < text_.size() && isIdentifierContinue(text_[pos_]))) {
        return invalidLineMarker(numberStart, "invalid line number in line marker");
    }
    std::uint32_t line = 0;
    if (std::from_chars(text_.data() + numberStart, text_.data() + pos_, line).ec != std::errc{}
        || line > maxLineNumber) {
        return invalidLineMarker(numberStart, "line number out of range");
    }

    skipDirectiveBlanks();
    std::string fileName = presumedFileName_;
    if (!atEndOfDirective()) {
        if (text_[pos_] != '"') {
            return invalidLineMarker(pos_, "invalid file name in line marker");
        }
        const LiteralEnd literal = findLiteralEnd(pos_);
        if (!literal.terminated) {
            return invalidLineMarker(pos_, "missing terminating '\"' character");
        }
        const std::string_view quotedName = text_.substr(pos_, literal.end - pos_);
        const auto decoded = decodeStringLiteral(std::span(&quotedName, 1));
        if (!decoded) {
            return invalidLineMarker(pos_, decoded.error().message);
        }
        fileName.clear();
        for (const std::uint32_t unit : decoded->codeUnits) {
            fileName += static_cast<char>(unit);
        }
        pos_ = literal.end;
        skipDirectiveBlanks();
    }
    if (!isLineDirective) {
        // GCC's flags: 1 entering a file, 2 returning to a file, 3 system header, 4 extern "C".
        while (!atEndOfDirective() && isDigit(text_[pos_])) {
            ++pos_;
            skipDirectiveBlanks();
        }
    }
    if (!atEndOfDirective()) {
        return invalidLineMarker(pos_, "extra tokens at end of line marker");
    }

    const std::uint32_t nextLine = sources_.lineColumn(locationAt(pos_)).line + 1;
    sources_.addLineMarker(file_, nextLine, line, fileName);
    presumedFileName_ = std::move(fileName);
}

void Lexer::invalidLineMarker(std::size_t at, std::string message) {
    diagnostics_.error(locationAt(at), std::move(message));
    skipRestOfDirective();
}

// Skips blanks, comments and backslash-newlines inside a directive.
void Lexer::skipDirectiveBlanks() {
    while (pos_ < text_.size()) {
        const char c = text_[pos_];
        if (isBlank(c)) {
            ++pos_;
        } else if (c == '/' && peek(1) == '*') {
            skipBlockComment();
        } else if (c == '/' && peek(1) == '/') {
            skipLineComment();
        } else if (spliceLength(pos_) > 0) {
            pos_ = skipSplices(pos_);
        } else {
            return;
        }
    }
}

// Skips to the end of the directive: the first newline that is not part of a backslash-newline, a comment or a quoted
// text.
void Lexer::skipRestOfDirective() {
    while (!atEndOfDirective()) {
        const char c = text_[pos_];
        if (c == '"' || c == '\'') {
            pos_ = findLiteralEnd(pos_).end;
        } else if (c == '/' && (peek(1) == '*' || peek(1) == '/')) {
            skipDirectiveBlanks();
        } else if (spliceLength(pos_) > 0) {
            pos_ = skipSplices(pos_);
        } else {
            ++pos_;
        }
    }
}

void Lexer::lexToken() {
    const std::size_t start = pos_;
    switch (text_[pos_]) {
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
        return lexNumber(start);
    case '.':
        if (isDigit(peek(1))) {
            return lexNumber(start);
        }
        if (peek(1) == '.' && peek(2) == '.') {
            return lexPunctuator(TokenKind::Ellipsis, 3);
        }
        return lexPunctuator(TokenKind::Period, 1);
    case '"':
    case '\'':
        return lexQuoted(start, start);
    case 'R':
        if (peek(1) == '"') {
            return lexRawString(start, start + 1);
        }
        return lexIdentifier(start);
    case 'L':
    case 'U':
        if (peek(1) == '"' || peek(1) == '\'') {
            return lexQuoted(start, start + 1);
        }
        if (peek(1) == 'R' && peek(2) == '"') {
            return lexRawString(start, start + 2);
        }
        return lexIdentifier(start);
    case 'u':
        if (peek(1) == '"' || peek(1) == '\'') {
            return lexQuoted(start, start + 1);
        }
        if (peek(1) == 'R' && peek(2) == '"') {
            return lexRawString(start, start + 2);
        }
        if (peek(1) == '8' && (peek(2) == '"' || peek(2) == '\'')) {
            return lexQuoted(start, start + 2);
        }
        if (peek(1) == '8' && peek(2) == 'R' && peek(3) == '"') {
            return lexRawString(start, start + 3);
        }
        return lexIdentifier(start);
    case '[':
        return lexPunctuator(TokenKind::LeftSquare, 1);
    case ']':
        return lexPunctuator(TokenKind::RightSquare, 1);
    case '(':
        return lexPunctuator(TokenKind::LeftParen, 1);
    case ')':
        return lexPunctuator(TokenKind::RightParen, 1);
    case '{':
        return lexPunctuator(TokenKind::LeftBrace, 1);
    case '}':
        return lexPunctuator(TokenKind::RightBrace, 1);
    case '~':
        return lexPunctuator(TokenKind::Tilde, 1);
    case '?':
        return lexPunctuator(TokenKind::Question, 1);
    case ';':
        return lexPunctuator(TokenKind::Semicolon, 1);
    case ',':
        return lexPunctuator(TokenKind::Comma, 1);
    case '-':
        if (peek(1) == '>') {
            return lexPunctuator(TokenKind::Arrow, 2);
        }
        if (peek(1) == '-') {
            return lexPunctuator(TokenKind::MinusMinus, 2);
        }
        if (peek(1) == '=') {
            return lexPunctuator(TokenKind::MinusEqual, 2);
        }
        return lexPunctuator(TokenKind::Minus, 1);
    case '+':
        if (peek(1) == '+') {
            return lexPunctuator(TokenKind::PlusPlus, 2);
        }
        if (peek(1) == '=') {
            return lexPunctuator(TokenKind::PlusEqual, 2);
        }
        return lexPunctuator(TokenKind::Plus, 1);
    case '&':
        if (peek(1) == '&') {
            return lexPunctuator(TokenKind::AmpAmp, 2);
        }
        if (peek(1) == '=') {
            return lexPunctuator(TokenKind::AmpEqual, 2);
        }
        return lexPunctuator(TokenKind::Amp, 1);
    case '|':
        if (peek(1) == '|') {
            return lexPunctuator(TokenKind::PipePipe, 2);
        }
        if (peek(1) == '=') {
            return lexPunctuator(TokenKind::PipeEqual, 2);
        }
        return lexPunctuator(TokenKind::Pipe, 1);
    case '*':
        return peek(1) == '=' ? lexPunctuator(TokenKind::StarEqual, 2) : lexPunctuator(TokenKind::Star, 1);
    case '/':
        return peek(1) == '=' ? lexPunctuator(TokenKind::SlashEqual, 2) : lexPunctuator(TokenKind::Slash, 1);
    case '!':
        return peek(1) == '=' ? lexPunctuator(TokenKind::ExclaimEqual, 2) : lexPunctuator(TokenKind::Exclaim, 1);
    case '=':
        return peek(1) == '=' ? lexPunctuator(TokenKind::EqualEqual, 2) : lexPunctuator(TokenKind::Equal, 1);
    case '^':
        return peek(1) == '=' ? lexPunctuator(TokenKind::CaretEqual, 2) : lexPunctuator(TokenKind::Caret, 1);
    case ':':
        return peek(1) == '>' ? lexPunctuator(TokenKind::RightSquare, 2) : lexPunctuator(TokenKind::Colon, 1);
    case '<':
        if (peek(1) == '<') {
            return peek(2) == '=' ? lexPunctuator(TokenKind::LessLessEqual, 3) : lexPunctuator(TokenKind::LessLess, 2);
        }
        if (peek(1) == '=') {
            return lexPunctuator(TokenKind::LessEqual, 2);
        }
        if (peek(1) == ':') {
            return lexPunctuator(TokenKind::LeftSquare, 2);
        }
        if (peek(1) == '%') {
            return lexPunctuator(TokenKind::LeftBrace, 2);
        }
        return lexPunctuator(TokenKind::Less, 1);
    case '>':
        if (peek(1) == '>') {
            return peek(2) == '=' ? lexPunctuator(TokenKind::GreaterGreaterEqual, 3)
                                  : lexPunctuator(TokenKind::GreaterGreater, 2);
        }
        return peek(1) == '=' ? lexPunctuator(TokenKind::GreaterEqual, 2) : lexPunctuator(TokenKind::Greater, 1);
    case '%':
        if (peek(1) == '=') {
            return lexPunctuator(TokenKind::PercentEqual, 2);
        }
        if (peek(1) == '>') {
            return lexPunctuator(TokenKind::RightBrace, 2);
        }
        if (peek(1) == ':') {
            // The digraphs of '#' and '##' outside a directive.
            return lexUnknown(peek(2) == '%' && peek(3) == ':' ? 4 : 2);
        }
        return lexPunctuator(TokenKind::Percent, 1);
    case '#':
        return lexUnknown(peek(1) == '#' ? 2 : 1);
    default:
        break;
    }
    if (isIdentifierStart(text_[start])) {
        return lexIdentifier(start);
    }
    return lexUnknown(1);
}

void Lexer::lexIdentifier(std::size_t start) {
    std::size_t end = start + 1;
    while (end < text_.size() && isIdentifierContinue(text_[end])) {
        ++end;
    }
    pos_ = end;
    const std::string_view name = text_.substr(start, end - start);
    const TokenKind kind = keywordKind(name);
    Token& token = addToken(kind, start);
    if (kind == TokenKind::Identifier) {
        token.identifier = interner_.intern(name);
    }
}

// A preprocessing number (C17 6.4.8): a digit, or '.' and a digit, followed by identifier characters, '.', and signs
// after e, E, p or P. It is a floating constant if it has a '.' or an exponent (C17 6.4.4.2): e or E in a decimal
// number, p or P in a hexadecimal one.
void Lexer::lexNumber(std::size_t start) {
    const bool isHex = text_[start] == '0' && (peek(1) == 'x' || peek(1) == 'X');
    bool isFloating = text_[start] == '.';
    std::size_t end = start + 1;
    for (; end < text_.size(); ++end) {
        const char c = text_[end];
        if (c == '+' || c == '-') {
            const char previous = text_[end - 1];
            if (previous != 'e' && previous != 'E' && previous != 'p' && previous != 'P') {
                break;
            }
        } else if (c == '.') {
            isFloating = true;
        } else if (!isIdentifierContinue(c)) {
            break;
        } else if (isHex ? (c == 'p' || c == 'P') : (c == 'e' || c == 'E')) {
            isFloating = true;
        }
    }
    pos_ = end;
    addToken(isFloating ? TokenKind::FloatingLiteral : TokenKind::IntegerLiteral, start);
}

// A string literal or character constant whose prefix starts at `start` and whose opening quote is at `quote`.
void Lexer::lexQuoted(std::size_t start, std::size_t quote) {
    const bool isString = text_[quote] == '"';
    const LiteralEnd literal = findLiteralEnd(quote);
    pos_ = literal.end;
    if (literal.terminated) {
        addToken(isString ? TokenKind::StringLiteral : TokenKind::CharacterLiteral, start);
        return;
    }
    if (text_[pos_ - 1] == '\r') {
        --pos_; // leave the "\r\n" line break to skipTrivia
    }
    diagnostics_.error(locationAt(start),
                       isString ? "missing terminating '\"' character" : "missing terminating ' character");
    addToken(TokenKind::Unknown, start);
}

// GCC accepts raw string literals, R"delimiter(...)delimiter", in C as an extension; NITWIT does not. The whole
// literal becomes one Unknown token, so that its contents do not turn into tokens.
void Lexer::lexRawString(std::size_t start, std::size_t quote) {
    diagnostics_.error(locationAt(start), "raw string literals are not supported");
    std::size_t end = text_.find('\n', quote);
    if (const std::size_t open = text_.find('(', quote); open < end) {
        const std::string closing = std::format("){}\"", text_.substr(quote + 1, open - quote - 1));
        if (const std::size_t close = text_.find(closing, open + 1); close != npos) {
            end = close + closing.size();
        }
    }
    pos_ = std::min(end, text_.size());
    if (pos_ < text_.size() && text_[pos_] == '\n' && text_[pos_ - 1] == '\r') {
        --pos_; // leave the "\r\n" line break to skipTrivia
    }
    addToken(TokenKind::Unknown, start);
}

void Lexer::lexPunctuator(TokenKind kind, std::size_t length) {
    const std::size_t start = pos_;
    pos_ += length;
    addToken(kind, start);
}

void Lexer::lexUnknown(std::size_t length) {
    const std::size_t start = pos_;
    pos_ += length;
    diagnostics_.error(locationAt(start), std::format("stray '{}' in program", printable(text_.substr(start, length))));
    addToken(TokenKind::Unknown, start);
}

Token& Lexer::addToken(TokenKind kind, std::size_t start) {
    Token& token = tokens_.emplace_back();
    token.kind = kind;
    token.atStartOfLine = atStartOfLine_;
    token.hasLeadingSpace = hasLeadingSpace_;
    token.length = static_cast<std::uint32_t>(pos_ - start);
    token.location = locationAt(start);
    atStartOfLine_ = false;
    atStartOfLogicalLine_ = false;
    hasLeadingSpace_ = false;
    lastTokenEnd_ = pos_;
    return token;
}

// The length of the backslash-newline at `at`, or 0 if there is none. Like GCC, this accepts blanks between the
// backslash and the newline, which covers "\\\r\n".
std::size_t Lexer::spliceLength(std::size_t at) const {
    if (at >= text_.size() || text_[at] != '\\') {
        return 0;
    }
    std::size_t end = at + 1;
    while (end < text_.size() && isBlank(text_[end])) {
        ++end;
    }
    return end < text_.size() && text_[end] == '\n' ? end + 1 - at : 0;
}

// Skips the backslash-newlines at `at`, with a warning about blanks before the newline as in GCC.
std::size_t Lexer::skipSplices(std::size_t at) {
    while (const std::size_t length = spliceLength(at)) {
        if (length > 2 && text_.substr(at, length) != "\\\r\n" && at >= nextSpliceWarning_) {
            diagnostics_.warning(locationAt(at), "backslash and newline separated by space");
            nextSpliceWarning_ = at + length;
        }
        at += length;
    }
    return at;
}

// Whether the newline at `newline` belongs to a backslash-newline.
bool Lexer::isContinuedLine(std::size_t newline) const {
    std::size_t i = newline;
    while (i > 0 && isBlank(text_[i - 1])) {
        --i;
    }
    return i > 0 && text_[i - 1] == '\\';
}

// Finds the end of the string literal or character constant whose opening quote is at `quote`. Backslash-newlines are
// removed before escape sequences are recognized (C17 5.1.1.2p1, phases 2 and 3).
LiteralEnd Lexer::findLiteralEnd(std::size_t quote) {
    const char delimiter = text_[quote];
    std::size_t i = quote + 1;
    while (true) {
        i = skipSplices(i);
        if (i >= text_.size() || text_[i] == '\n') {
            return {i, false};
        }
        const char c = text_[i++];
        if (c == delimiter) {
            return {i, true};
        }
        if (c == '\\') {
            i = skipSplices(i);
            if (i >= text_.size() || text_[i] == '\n') {
                return {i, false};
            }
            ++i;
        }
    }
}

} // namespace

LexResult lexFile(SourceManager& sources, FileId file, StringInterner& interner, DiagnosticEngine& diagnostics) {
    return Lexer(sources, file, interner, diagnostics).run();
}

std::string_view spellingOf(const SourceManager& sources, const Token& token) {
    return sources.contents(token.location.file).substr(token.location.offset, token.length);
}

} // namespace nitwit::lex
