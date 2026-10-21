#include "parse/parser.hpp"

#include <algorithm>
#include <charconv>
#include <format>
#include <system_error>
#include <utility>

#include "ast/casting.hpp"
#include "lex/lexer.hpp"
#include "parse/parser_impl.hpp"
#include "support/assert.hpp"

namespace nitwit::parse {

namespace detail {

namespace {

// The parser recurses once per nesting level of parentheses, blocks, declarators and the like.
constexpr std::size_t maxNestingDepth = 10000;

constexpr std::size_t maxDescribedLength = 40;

bool isBlank(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v';
}

bool isWordCharacter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

// A cursor over the text of a pragma, which may contain comments and backslash-newlines.
class PragmaText {
public:
    explicit PragmaText(std::string_view text) : text_(text) {}

    void skipBlanks() {
        while (pos_ < text_.size()) {
            const std::string_view rest = text_.substr(pos_);
            if (isBlank(rest.front())) {
                ++pos_;
            } else if (rest.starts_with("\\\n")) {
                pos_ += 2;
            } else if (rest.starts_with("\\\r\n")) {
                pos_ += 3;
            } else if (rest.starts_with("/*")) {
                const std::size_t end = rest.find("*/", 2);
                pos_ = end == std::string_view::npos ? text_.size() : pos_ + end + 2;
            } else {
                return;
            }
        }
    }

    std::string_view word() {
        skipBlanks();
        const std::size_t start = pos_;
        while (pos_ < text_.size() && isWordCharacter(text_[pos_])) {
            ++pos_;
        }
        return text_.substr(start, pos_ - start);
    }

    bool consume(char c) {
        skipBlanks();
        if (pos_ < text_.size() && text_[pos_] == c) {
            ++pos_;
            return true;
        }
        return false;
    }

    [[nodiscard]] std::string_view rest() const { return text_.substr(pos_); }

private:
    std::string_view text_;
    std::size_t pos_ = 0;
};

std::optional<std::uint64_t> parseNumber(std::string_view text) {
    std::uint64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

} // namespace

Parser::DepthGuard::DepthGuard(Parser& parser) : parser_(parser) {
    if (++parser_.depth_ > maxNestingDepth) {
        parser_.fail(parser_.token().location, std::format("nesting is deeper than {} levels", maxNestingDepth));
    }
}

Parser::Parser(std::span<const lex::Token> tokens, ast::AstContext& context, const SourceManager& sources,
               DiagnosticEngine& diagnostics)
    : tokens_(tokens), context_(context), sources_(sources), diagnostics_(diagnostics), interner_(context.interner()) {
    NITWIT_ASSERT(!tokens_.empty() && tokens_.back().is(TokenKind::EndOfFile), "tokens must end with EndOfFile");
    scopes_.emplace_back().kind = ScopeKind::File;
    declarePredefinedTypedef("__builtin_va_list", ast::BuiltinKind::VaList);
    declarePredefinedTypedef("__int128_t", ast::BuiltinKind::Int128);
    declarePredefinedTypedef("__uint128_t", ast::BuiltinKind::UnsignedInt128);
}

// ---- Tokens and diagnostics ----

const lex::Token& Parser::peek(std::size_t ahead) const {
    std::size_t index = pos_;
    for (; ahead > 0 && !tokens_[index].is(TokenKind::EndOfFile); --ahead) {
        do {
            ++index;
        } while (tokens_[index].is(TokenKind::Pragma));
    }
    return tokens_[index];
}

const lex::Token& Parser::consume() {
    const lex::Token& current = tokens_[pos_];
    if (!current.is(TokenKind::EndOfFile)) {
        previousEnd_ = current.range().end;
        ++pos_;
        skipPragmas();
    }
    return current;
}

bool Parser::tryConsume(TokenKind kind) {
    if (!at(kind)) {
        return false;
    }
    consume();
    return true;
}

const lex::Token& Parser::expect(TokenKind kind) {
    if (!at(kind)) {
        failExpected(std::format("'{}'", lex::spelling(kind)));
    }
    return consume();
}

// Pragmas take effect where they are in the token stream, when the parser moves past them.
void Parser::skipPragmas() {
    while (tokens_[pos_].is(TokenKind::Pragma)) {
        handlePragma(tokens_[pos_]);
        ++pos_;
    }
}

std::string Parser::describe(const lex::Token& token) const {
    std::string_view text = lex::spellingOf(sources_, token);
    switch (token.kind) {
    case TokenKind::EndOfFile:
        return "end of file";
    case TokenKind::Identifier:
    case TokenKind::IntegerLiteral:
    case TokenKind::FloatingLiteral:
    case TokenKind::CharacterLiteral:
    case TokenKind::StringLiteral: {
        const bool shortened = text.size() > maxDescribedLength;
        return std::format("{} '{}{}'", lex::spelling(token.kind), text.substr(0, maxDescribedLength),
                           shortened ? "..." : "");
    }
    default:
        return std::format("'{}'", text);
    }
}

void Parser::fail(SourceLocation location, std::string message) {
    diagnostics_.error(location, std::move(message));
    throw ParseError{};
}

void Parser::failExpected(std::string_view what) {
    fail(token().location, std::format("expected {}, found {}", what, describe(token())));
}

void Parser::warning(SourceLocation location, std::string message) {
    diagnostics_.warning(location, std::move(message));
}

// ---- Pragmas ----

void Parser::handlePragma(const lex::Token& pragma) {
    PragmaText text(lex::spellingOf(sources_, pragma));
    if (!text.consume('#')) {
        text.consume('%');
        text.consume(':');
    }
    if (text.word() != "pragma") {
        return;
    }
    const std::string_view name = text.word();
    if (name == "pack") {
        handlePackPragma(text.rest(), pragma.location);
    } else if (name == "weak") {
        handleWeakPragma(text.rest(), pragma.location);
    }
}

// GCC's `#pragma pack`: `pack(N)`, `pack()`, `pack(push[, N])`, `pack(pop[, N])`; GCC also accepts an identifier after
// push and pop, which is ignored here.
void Parser::handlePackPragma(std::string_view arguments, SourceLocation location) {
    PragmaText text(arguments);
    std::vector<std::string_view> items;
    bool wellFormed = text.consume('(');
    if (wellFormed && !text.consume(')')) {
        while (true) {
            const std::string_view item = text.word();
            if (item.empty()) {
                wellFormed = false;
                break;
            }
            items.push_back(item);
            if (text.consume(')')) {
                break;
            }
            if (!text.consume(',')) {
                wellFormed = false;
                break;
            }
        }
    }
    if (!wellFormed) {
        return warning(location, "malformed '#pragma pack', ignored");
    }
    if (items.empty()) {
        packAlignment_ = 0;
        return;
    }
    const std::string_view action = items.front();
    if (action != "push" && action != "pop") {
        if (items.size() != 1) {
            return warning(location, "malformed '#pragma pack', ignored");
        }
        return setPackAlignment(action, location);
    }
    if (action == "push") {
        packStack_.push_back(packAlignment_);
    } else if (packStack_.empty()) {
        warning(location, "'#pragma pack(pop)' without a matching '#pragma pack(push)'");
    } else {
        packAlignment_ = packStack_.back();
        packStack_.pop_back();
    }
    if (items.size() > 1 && parseNumber(items.back())) {
        setPackAlignment(items.back(), location);
    }
}

void Parser::setPackAlignment(std::string_view value, SourceLocation location) {
    const std::optional<std::uint64_t> alignment = parseNumber(value);
    if (!alignment
        || !(*alignment == 0 || *alignment == 1 || *alignment == 2 || *alignment == 4 || *alignment == 8
             || *alignment == 16)) {
        return warning(location, std::format("'#pragma pack' alignment must be 1, 2, 4, 8 or 16, not '{}'", value));
    }
    packAlignment_ = *alignment;
}

void Parser::handleWeakPragma(std::string_view arguments, SourceLocation location) {
    PragmaText text(arguments);
    const std::string_view name = text.word();
    if (name.empty()) {
        return warning(location, "malformed '#pragma weak', ignored");
    }
    if (text.consume('=')) {
        return warning(location, "'#pragma weak' with an alias is not supported, ignored");
    }
    weakNames_.push_back(intern(name));
}

// `#pragma weak f` applies to f wherever f is declared, before or after the pragma.
void Parser::applyWeakPragmas() {
    for (const Symbol name : weakNames_) {
        const auto found = externals_.find(name);
        if (found == externals_.end()) {
            continue;
        }
        for (auto* function = ast::dyn_cast<ast::FunctionDecl>(found->second); function != nullptr;
             function = function->previous()) {
            function->setWeak();
        }
    }
}

// ---- Translation unit ----

ast::TranslationUnit* Parser::parseTranslationUnit() {
    skipPragmas();
    std::vector<ast::Decl*> decls;
    std::vector<ast::Decl*> declaration;
    while (!at(TokenKind::EndOfFile)) {
        declaration.clear();
        parseExternalDeclaration(declaration);
        // Implicit function declarations precede the declaration that caused them.
        decls.insert(decls.end(), implicitDecls_.begin(), implicitDecls_.end());
        implicitDecls_.clear();
        decls.insert(decls.end(), declaration.begin(), declaration.end());
    }
    applyWeakPragmas();
    return context_.create<ast::TranslationUnit>(ast::TranslationUnit{.decls = context_.copy<ast::Decl*>(decls)});
}

void Parser::parseExternalDeclaration(std::vector<ast::Decl*>& out) {
    if (tryConsume(TokenKind::Semicolon)) {
        return; // GNU: an empty declaration
    }
    if (at(TokenKind::KwAsm)) {
        return skipFileScopeAsm();
    }
    if (at(TokenKind::KwStaticAssert)) {
        out.push_back(parseStaticAssert());
        return;
    }
    while (tryConsume(TokenKind::KwExtension)) {
    }
    if (at(TokenKind::Identifier) && peek(1).is(TokenKind::Identifier) && !isTypedefName(token())) {
        failUnknownTypeName(token());
    }
    const std::size_t mark = tagDefinitions_.size();
    const DeclSpec spec = parseDeclarationSpecifiers(SpecifierContext::File);
    flushTagDefinitions(mark, out);
    if (tryConsume(TokenKind::Semicolon)) {
        return finishEmptyDeclaration(spec);
    }
    Declarator first = parseDeclarator(DeclaratorMode::Named);
    parseDeclaratorTail(first);
    if (first.isFunction()
        && (at(TokenKind::LeftBrace) || (!first.chunks.front().identifiers.empty() && startsDeclaration(token())))) {
        out.push_back(parseFunctionDefinition(spec, first));
        return;
    }
    parseInitDeclarators(spec, std::move(first), out);
    expect(TokenKind::Semicolon);
}

// A file-scope `asm("...");` has no effect on the program's semantics as far as the interpreter is concerned.
void Parser::skipFileScopeAsm() {
    consume();
    expect(TokenKind::LeftParen);
    if (!at(TokenKind::StringLiteral)) {
        failExpected("string literal");
    }
    (void)parseStringLiteral();
    expect(TokenKind::RightParen);
    expect(TokenKind::Semicolon);
}

} // namespace detail

ast::TranslationUnit* parseTranslationUnit(std::span<const lex::Token> tokens, ast::AstContext& context,
                                           const SourceManager& sources, DiagnosticEngine& diagnostics) {
    try {
        detail::Parser parser(tokens, context, sources, diagnostics);
        return parser.parseTranslationUnit();
    } catch (const detail::ParseError&) {
        return nullptr;
    }
}

} // namespace nitwit::parse
