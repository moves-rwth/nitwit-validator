#include "parse/parser.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <format>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

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

bool isDigit(char c) {
    return c >= '0' && c <= '9';
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

std::size_t Parser::following(std::size_t index) const {
    if (tokens_[index].is(TokenKind::EndOfFile)) {
        return index;
    }
    do {
        ++index;
    } while (tokens_[index].is(TokenKind::Pragma));
    return index;
}

const lex::Token& Parser::peek(std::size_t ahead) const {
    std::size_t index = pos_;
    for (; ahead > 0; --ahead) {
        index = following(index);
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

// GCC's `#pragma pack`: `pack()` and `pack(N)` set the maximum field alignment (N = 0 or no N: none),
// `pack(push[, id][, N])` saves it on a stack and then sets N, `pack(pop[, id])` restores the saved value (with an id,
// the one saved by the matching push). Malformed pragmas are ignored with a warning, as in GCC.
void Parser::handlePackPragma(std::string_view arguments, SourceLocation location) {
    enum class Action : std::uint8_t { Set, Push, Pop };
    Action action = Action::Set;
    std::optional<std::uint64_t> alignment;
    std::string_view id;
    const auto malformed = [&] {
        warning(location, "malformed '#pragma pack' - ignored");
    };

    PragmaText text(arguments);
    if (!text.consume('(')) {
        return malformed();
    }
    if (!text.consume(')')) {
        const std::string_view first = text.word();
        if (first == "push" || first == "pop") {
            action = first == "push" ? Action::Push : Action::Pop;
            while (text.consume(',')) {
                const std::string_view item = text.word();
                const std::optional<std::uint64_t> number = parseNumber(item);
                if (number && action == Action::Push && !alignment) {
                    alignment = number;
                } else if (!item.empty() && !number && !isDigit(item.front()) && id.empty()) {
                    id = item;
                } else {
                    return malformed();
                }
            }
        } else {
            alignment = parseNumber(first);
            if (!alignment) {
                return malformed();
            }
        }
        if (!text.consume(')')) {
            return malformed();
        }
    }

    if (alignment
        && !(*alignment == 0 || *alignment == 1 || *alignment == 2 || *alignment == 4 || *alignment == 8
             || *alignment == 16)) {
        return warning(location, std::format("'#pragma pack' alignment must be 1, 2, 4, 8 or 16, not {}", *alignment));
    }
    switch (action) {
    case Action::Set:
        packAlignment_ = alignment.value_or(0);
        return;
    case Action::Push:
        packStack_.push_back({packAlignment_, std::string(id)});
        packAlignment_ = alignment.value_or(packAlignment_);
        return;
    case Action::Pop:
        return popPackAlignment(id, location);
    }
}

void Parser::popPackAlignment(std::string_view id, SourceLocation location) {
    if (packStack_.empty()) {
        return warning(location, "'#pragma pack(pop)' without a matching '#pragma pack(push)'");
    }
    if (!id.empty()) {
        // GCC drops the entries above the one pushed with this id; without one, it pops the last entry.
        const auto named = std::ranges::find(packStack_ | std::views::reverse, id, &PackEntry::id);
        if (named == packStack_.rend()) {
            warning(location, std::format("'#pragma pack(pop, {0})' without a matching '#pragma pack(push, {0})'", id));
        } else {
            packStack_.erase(named.base(), packStack_.end());
        }
    }
    packAlignment_ = packStack_.back().alignment;
    packStack_.pop_back();
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
