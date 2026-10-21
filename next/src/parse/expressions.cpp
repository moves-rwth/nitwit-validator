// Expressions (C17 6.5) and initializers (C17 6.7.9), with the GNU extensions.

#include <cstdint>
#include <format>
#include <optional>
#include <string_view>
#include <vector>

#include "ast/casting.hpp"
#include "lex/lexer.hpp"
#include "lex/literals.hpp"
#include "parse/parser_impl.hpp"

namespace nitwit::parse::detail {

namespace {

using ast::BinaryOperator;

struct BinaryOperatorInfo {
    BinaryOperator op;
    int precedence; // higher binds tighter
};

std::optional<BinaryOperatorInfo> binaryOperator(TokenKind kind) {
    switch (kind) {
    case TokenKind::Star:
        return BinaryOperatorInfo{BinaryOperator::Mul, 10};
    case TokenKind::Slash:
        return BinaryOperatorInfo{BinaryOperator::Div, 10};
    case TokenKind::Percent:
        return BinaryOperatorInfo{BinaryOperator::Rem, 10};
    case TokenKind::Plus:
        return BinaryOperatorInfo{BinaryOperator::Add, 9};
    case TokenKind::Minus:
        return BinaryOperatorInfo{BinaryOperator::Sub, 9};
    case TokenKind::LessLess:
        return BinaryOperatorInfo{BinaryOperator::Shl, 8};
    case TokenKind::GreaterGreater:
        return BinaryOperatorInfo{BinaryOperator::Shr, 8};
    case TokenKind::Less:
        return BinaryOperatorInfo{BinaryOperator::Less, 7};
    case TokenKind::Greater:
        return BinaryOperatorInfo{BinaryOperator::Greater, 7};
    case TokenKind::LessEqual:
        return BinaryOperatorInfo{BinaryOperator::LessEqual, 7};
    case TokenKind::GreaterEqual:
        return BinaryOperatorInfo{BinaryOperator::GreaterEqual, 7};
    case TokenKind::EqualEqual:
        return BinaryOperatorInfo{BinaryOperator::Equal, 6};
    case TokenKind::ExclaimEqual:
        return BinaryOperatorInfo{BinaryOperator::NotEqual, 6};
    case TokenKind::Amp:
        return BinaryOperatorInfo{BinaryOperator::BitwiseAnd, 5};
    case TokenKind::Caret:
        return BinaryOperatorInfo{BinaryOperator::BitwiseXor, 4};
    case TokenKind::Pipe:
        return BinaryOperatorInfo{BinaryOperator::BitwiseOr, 3};
    case TokenKind::AmpAmp:
        return BinaryOperatorInfo{BinaryOperator::LogicalAnd, 2};
    case TokenKind::PipePipe:
        return BinaryOperatorInfo{BinaryOperator::LogicalOr, 1};
    default:
        return std::nullopt;
    }
}

constexpr int lowestBinaryPrecedence = 1;

std::optional<BinaryOperator> assignmentOperator(TokenKind kind) {
    switch (kind) {
    case TokenKind::Equal:
        return BinaryOperator::Assign;
    case TokenKind::StarEqual:
        return BinaryOperator::MulAssign;
    case TokenKind::SlashEqual:
        return BinaryOperator::DivAssign;
    case TokenKind::PercentEqual:
        return BinaryOperator::RemAssign;
    case TokenKind::PlusEqual:
        return BinaryOperator::AddAssign;
    case TokenKind::MinusEqual:
        return BinaryOperator::SubAssign;
    case TokenKind::LessLessEqual:
        return BinaryOperator::ShlAssign;
    case TokenKind::GreaterGreaterEqual:
        return BinaryOperator::ShrAssign;
    case TokenKind::AmpEqual:
        return BinaryOperator::AndAssign;
    case TokenKind::CaretEqual:
        return BinaryOperator::XorAssign;
    case TokenKind::PipeEqual:
        return BinaryOperator::OrAssign;
    default:
        return std::nullopt;
    }
}

std::optional<ast::UnaryOperator> prefixOperator(TokenKind kind) {
    switch (kind) {
    case TokenKind::Amp:
        return ast::UnaryOperator::AddressOf;
    case TokenKind::Star:
        return ast::UnaryOperator::Dereference;
    case TokenKind::Plus:
        return ast::UnaryOperator::Plus;
    case TokenKind::Minus:
        return ast::UnaryOperator::Minus;
    case TokenKind::Tilde:
        return ast::UnaryOperator::BitwiseNot;
    case TokenKind::Exclaim:
        return ast::UnaryOperator::LogicalNot;
    case TokenKind::KwReal:
        return ast::UnaryOperator::Real;
    case TokenKind::KwImag:
        return ast::UnaryOperator::Imag;
    case TokenKind::KwExtension:
        return ast::UnaryOperator::Extension;
    default:
        return std::nullopt;
    }
}

SourceRange spanOf(const ast::Expr* first, const ast::Expr* last) {
    return {first->range().begin, last->range().end};
}

} // namespace

ast::Expr* Parser::parseExpression() {
    ast::Expr* expr = parseAssignmentExpression();
    while (at(TokenKind::Comma)) {
        const SourceLocation comma = consume().location;
        ast::Expr* rhs = parseAssignmentExpression();
        expr = context_.create<ast::BinaryExpr>(spanOf(expr, rhs), BinaryOperator::Comma, expr, rhs, comma);
    }
    return expr;
}

ast::Expr* Parser::parseAssignmentExpression() {
    const DepthGuard guard(*this);
    ast::Expr* lhs = parseConditionalExpression();
    const std::optional<BinaryOperator> op = assignmentOperator(token().kind);
    if (!op) {
        return lhs;
    }
    const SourceLocation location = consume().location;
    ast::Expr* rhs = parseAssignmentExpression();
    return context_.create<ast::BinaryExpr>(spanOf(lhs, rhs), *op, lhs, rhs, location);
}

ast::Expr* Parser::parseConditionalExpression() {
    ast::Expr* condition = parseBinaryExpression(lowestBinaryPrecedence);
    if (!at(TokenKind::Question)) {
        return condition;
    }
    const SourceLocation question = consume().location;
    // GNU: `c ?: b` has no middle operand.
    ast::Expr* trueExpr = at(TokenKind::Colon) ? nullptr : parseExpression();
    expect(TokenKind::Colon);
    const DepthGuard guard(*this);
    ast::Expr* falseExpr = parseConditionalExpression();
    return context_.create<ast::ConditionalExpr>(spanOf(condition, falseExpr), condition, trueExpr, falseExpr,
                                                 question);
}

// Precedence climbing: operators of equal precedence are parsed in the loop (left associative), so the recursion
// depth is bounded by the number of precedence levels.
ast::Expr* Parser::parseBinaryExpression(int minPrecedence) {
    ast::Expr* lhs = parseCastExpression();
    while (true) {
        const std::optional<BinaryOperatorInfo> info = binaryOperator(token().kind);
        if (!info || info->precedence < minPrecedence) {
            return lhs;
        }
        const SourceLocation location = consume().location;
        ast::Expr* rhs = parseBinaryExpression(info->precedence + 1);
        lhs = context_.create<ast::BinaryExpr>(spanOf(lhs, rhs), info->op, lhs, rhs, location);
    }
}

ast::Expr* Parser::parseCastExpression() {
    if (!at(TokenKind::LeftParen) || !startsTypeName(peek(1))) {
        return parseUnaryExpression();
    }
    const SourceLocation begin = consume().location;
    const ast::QualType type = parseTypeName();
    expect(TokenKind::RightParen);
    if (at(TokenKind::LeftBrace)) {
        return parsePostfixSuffixes(parseCompoundLiteral(begin, type));
    }
    const DepthGuard guard(*this);
    ast::Expr* operand = parseCastExpression();
    return context_.create<ast::CastExpr>(SourceRange{begin, operand->range().end}, type, operand);
}

ast::Expr* Parser::parseUnaryExpression() {
    const lex::Token& op = token();
    switch (op.kind) {
    case TokenKind::PlusPlus:
    case TokenKind::MinusMinus: {
        consume();
        const DepthGuard guard(*this);
        ast::Expr* operand = parseUnaryExpression();
        return context_.create<ast::UnaryExpr>(
            SourceRange{op.location, operand->range().end},
            op.is(TokenKind::PlusPlus) ? ast::UnaryOperator::PreIncrement : ast::UnaryOperator::PreDecrement, operand);
    }
    case TokenKind::KwSizeof:
    case TokenKind::KwAlignof:
        return parseSizeOfAlignOf();
    case TokenKind::AmpAmp:
        fail(op.location, "taking the address of a label is not supported");
    default:
        break;
    }
    if (const std::optional<ast::UnaryOperator> unary = prefixOperator(op.kind)) {
        consume();
        const DepthGuard guard(*this);
        ast::Expr* operand = parseCastExpression();
        return context_.create<ast::UnaryExpr>(SourceRange{op.location, operand->range().end}, *unary, operand);
    }
    return parsePostfixSuffixes(parsePrimaryExpression());
}

// `sizeof`, `_Alignof` and GNU `__alignof__`, of an expression or a parenthesized type name; `sizeof (T){...}` is the
// size of a compound literal.
ast::Expr* Parser::parseSizeOfAlignOf() {
    const lex::Token& keyword = consume();
    using Operator = ast::SizeOfAlignOfExpr::Operator;
    Operator op = Operator::SizeOf;
    if (keyword.is(TokenKind::KwAlignof)) {
        op = lex::spellingOf(sources_, keyword) == "_Alignof" ? Operator::AlignOf : Operator::PreferredAlignOf;
    }
    if (at(TokenKind::LeftParen) && startsTypeName(peek(1))) {
        const SourceLocation begin = consume().location;
        const ast::QualType type = parseTypeName();
        expect(TokenKind::RightParen);
        if (!at(TokenKind::LeftBrace)) {
            return context_.create<ast::SizeOfAlignOfExpr>(rangeFrom(keyword.location), op, type);
        }
        ast::Expr* literal = parsePostfixSuffixes(parseCompoundLiteral(begin, type));
        return context_.create<ast::SizeOfAlignOfExpr>(rangeFrom(keyword.location), op, literal);
    }
    const DepthGuard guard(*this);
    ast::Expr* operand = parseUnaryExpression();
    return context_.create<ast::SizeOfAlignOfExpr>(rangeFrom(keyword.location), op, operand);
}

ast::Expr* Parser::parsePostfixSuffixes(ast::Expr* expr) {
    while (true) {
        const SourceLocation begin = expr->range().begin;
        switch (token().kind) {
        case TokenKind::LeftSquare: {
            consume();
            ast::Expr* index = parseExpression();
            expect(TokenKind::RightSquare);
            expr = context_.create<ast::ArraySubscriptExpr>(rangeFrom(begin), expr, index);
            break;
        }
        case TokenKind::LeftParen: {
            consume();
            std::vector<ast::Expr*> arguments;
            while (!at(TokenKind::RightParen)) {
                arguments.push_back(parseAssignmentExpression());
                if (!tryConsume(TokenKind::Comma)) {
                    break;
                }
            }
            const SourceLocation rightParen = expect(TokenKind::RightParen).location;
            expr = context_.create<ast::CallExpr>(rangeFrom(begin), expr, context_.copy<ast::Expr*>(arguments),
                                                  rightParen);
            break;
        }
        case TokenKind::Period:
        case TokenKind::Arrow: {
            const bool isArrow = consume().is(TokenKind::Arrow);
            if (!at(TokenKind::Identifier)) {
                failExpected("member name");
            }
            const lex::Token& member = consume();
            expr =
                context_.create<ast::MemberExpr>(rangeFrom(begin), expr, member.identifier, isArrow, member.location);
            break;
        }
        case TokenKind::PlusPlus:
        case TokenKind::MinusMinus: {
            const bool isIncrement = consume().is(TokenKind::PlusPlus);
            expr = context_.create<ast::UnaryExpr>(
                rangeFrom(begin), isIncrement ? ast::UnaryOperator::PostIncrement : ast::UnaryOperator::PostDecrement,
                expr);
            break;
        }
        default:
            return expr;
        }
    }
}

ast::Expr* Parser::parsePrimaryExpression() {
    const lex::Token& current = token();
    switch (current.kind) {
    case TokenKind::Identifier:
        return parseIdentifierExpression();
    case TokenKind::IntegerLiteral: {
        consume();
        const auto value = lex::decodeIntegerLiteral(lex::spellingOf(sources_, current));
        if (!value) {
            fail(current.location, value.error().message);
        }
        return context_.create<ast::IntegerLiteralExpr>(current.range(), *value);
    }
    case TokenKind::FloatingLiteral: {
        consume();
        const auto value = lex::decodeFloatingLiteral(lex::spellingOf(sources_, current));
        if (!value) {
            fail(current.location, value.error().message);
        }
        return context_.create<ast::FloatingLiteralExpr>(current.range(), *value);
    }
    case TokenKind::CharacterLiteral: {
        consume();
        const auto value = lex::decodeCharacterLiteral(lex::spellingOf(sources_, current));
        if (!value) {
            fail(current.location, value.error().message);
        }
        return context_.create<ast::CharacterLiteralExpr>(current.range(), *value);
    }
    case TokenKind::StringLiteral:
        return parseStringLiteral();
    case TokenKind::LeftParen: {
        if (peek(1).is(TokenKind::LeftBrace)) {
            return parseStatementExpression();
        }
        consume();
        ast::Expr* inner = parseExpression();
        expect(TokenKind::RightParen);
        return context_.create<ast::ParenExpr>(rangeFrom(current.location), inner);
    }
    case TokenKind::KwGeneric:
        return parseGenericSelection();
    case TokenKind::KwBuiltinVaArg:
        return parseBuiltinVaArg();
    case TokenKind::KwBuiltinOffsetof:
        return parseBuiltinOffsetof();
    case TokenKind::KwBuiltinTypesCompatibleP:
        return parseBuiltinTypesCompatible();
    case TokenKind::KwBuiltinChooseExpr:
        return parseBuiltinChooseExpr();
    default:
        failExpected("expression");
    }
}

ast::Expr* Parser::parseIdentifierExpression() {
    const lex::Token& name = consume();
    const std::string_view text = name.identifier.text();
    if (text == "__func__" || text == "__FUNCTION__" || text == "__PRETTY_FUNCTION__") {
        // C17 6.4.2.2: the name of the enclosing function, as if declared as a static char array.
        if (currentFunction_ == nullptr) {
            warning(name.location, std::format("'{}' is not defined outside of a function", text));
        }
        const std::string_view function = currentFunction_ != nullptr ? currentFunction_->name().text() : "";
        const std::vector<std::uint32_t> codeUnits(function.begin(), function.end());
        return context_.create<ast::StringLiteralExpr>(name.range(), lex::CharEncoding::Plain,
                                                       context_.copy<std::uint32_t>(codeUnits));
    }
    ast::Decl* decl = lookupOrdinary(name.identifier);
    if (decl == nullptr) {
        if (!at(TokenKind::LeftParen)) {
            fail(name.location, std::format("use of undeclared identifier '{}'", text));
        }
        decl = declareImplicitFunction(name);
    }
    if (ast::isa<ast::TypedefDecl>(decl)) {
        fail(name.location, std::format("unexpected type name '{}', expected an expression", text));
    }
    return context_.create<ast::DeclRefExpr>(name.range(), name.identifier, ast::cast<ast::ValueDecl>(decl));
}

// Adjacent string literals form one string (C17 6.4.5p5).
ast::StringLiteralExpr* Parser::parseStringLiteral() {
    const SourceLocation begin = token().location;
    std::vector<std::string_view> spellings;
    while (at(TokenKind::StringLiteral)) {
        spellings.push_back(lex::spellingOf(sources_, consume()));
    }
    const auto decoded = lex::decodeStringLiteral(spellings);
    if (!decoded) {
        fail(begin, decoded.error().message);
    }
    return context_.create<ast::StringLiteralExpr>(rangeFrom(begin), decoded->encoding,
                                                   context_.copy<std::uint32_t>(decoded->codeUnits));
}

ast::Expr* Parser::parseCompoundLiteral(SourceLocation begin, ast::QualType type) {
    ast::InitListExpr* initializer = parseInitializerList();
    return context_.create<ast::CompoundLiteralExpr>(rangeFrom(begin), type, initializer, currentFunction_ == nullptr);
}

// GNU `({ ... })`.
ast::Expr* Parser::parseStatementExpression() {
    const SourceLocation begin = consume().location;
    if (currentFunction_ == nullptr) {
        fail(begin, "a statement expression is only allowed inside a function");
    }
    ast::CompoundStmt* body = parseCompoundStatement(true);
    expect(TokenKind::RightParen);
    return context_.create<ast::StatementExpr>(rangeFrom(begin), body);
}

ast::Expr* Parser::parseGenericSelection() {
    const SourceLocation begin = consume().location;
    expect(TokenKind::LeftParen);
    ast::Expr* controlling = parseAssignmentExpression();
    std::vector<ast::GenericSelectionExpr::Association> associations;
    while (tryConsume(TokenKind::Comma)) {
        ast::QualType type;
        if (!tryConsume(TokenKind::KwDefault)) {
            type = parseTypeName();
        }
        expect(TokenKind::Colon);
        associations.push_back({.type = type, .result = parseAssignmentExpression()});
    }
    expect(TokenKind::RightParen);
    return context_.create<ast::GenericSelectionExpr>(
        rangeFrom(begin), controlling, context_.copy<ast::GenericSelectionExpr::Association>(associations));
}

ast::Expr* Parser::parseBuiltinVaArg() {
    const SourceLocation begin = consume().location;
    expect(TokenKind::LeftParen);
    ast::Expr* list = parseAssignmentExpression();
    expect(TokenKind::Comma);
    const ast::QualType type = parseTypeName();
    expect(TokenKind::RightParen);
    return context_.create<ast::VaArgExpr>(rangeFrom(begin), list, type);
}

// `__builtin_offsetof(type, member-designator)`, where the designator is `a`, `.b` and `[i]` in any sequence.
ast::Expr* Parser::parseBuiltinOffsetof() {
    const SourceLocation begin = consume().location;
    expect(TokenKind::LeftParen);
    const ast::QualType type = parseTypeName();
    expect(TokenKind::Comma);
    std::vector<ast::OffsetOfExpr::Component> components;
    bool expectField = true;
    while (true) {
        if (expectField) {
            if (!at(TokenKind::Identifier)) {
                failExpected("member name");
            }
            components.push_back(
                {.isField = true, .fieldName = consume().identifier, .index = nullptr, .field = nullptr});
            expectField = false;
        } else if (tryConsume(TokenKind::Period)) {
            expectField = true;
        } else if (tryConsume(TokenKind::LeftSquare)) {
            components.push_back(
                {.isField = false, .fieldName = Symbol(), .index = parseExpression(), .field = nullptr});
            expect(TokenKind::RightSquare);
        } else {
            break;
        }
    }
    expect(TokenKind::RightParen);
    return context_.create<ast::OffsetOfExpr>(rangeFrom(begin), type,
                                              context_.copy<ast::OffsetOfExpr::Component>(components));
}

ast::Expr* Parser::parseBuiltinTypesCompatible() {
    const SourceLocation begin = consume().location;
    expect(TokenKind::LeftParen);
    const ast::QualType first = parseTypeName();
    expect(TokenKind::Comma);
    const ast::QualType second = parseTypeName();
    expect(TokenKind::RightParen);
    return context_.create<ast::TypesCompatibleExpr>(rangeFrom(begin), first, second);
}

ast::Expr* Parser::parseBuiltinChooseExpr() {
    const SourceLocation begin = consume().location;
    expect(TokenKind::LeftParen);
    ast::Expr* condition = parseAssignmentExpression();
    expect(TokenKind::Comma);
    ast::Expr* first = parseAssignmentExpression();
    expect(TokenKind::Comma);
    ast::Expr* second = parseAssignmentExpression();
    expect(TokenKind::RightParen);
    return context_.create<ast::ChooseExpr>(rangeFrom(begin), condition, first, second);
}

// ---- Initializers ----

ast::Expr* Parser::parseInitializer() {
    if (at(TokenKind::LeftBrace)) {
        return parseInitializerList();
    }
    return parseAssignmentExpression();
}

ast::InitListExpr* Parser::parseInitializerList() {
    const DepthGuard guard(*this);
    const SourceLocation begin = expect(TokenKind::LeftBrace).location;
    std::vector<ast::Expr*> elements;
    while (!at(TokenKind::RightBrace)) {
        const SourceLocation elementBegin = token().location;
        const std::vector<ast::Designator> designators = parseDesignation();
        ast::Expr* value = parseInitializer();
        if (!designators.empty()) {
            value = context_.create<ast::DesignatedInitExpr>(rangeFrom(elementBegin),
                                                             context_.copy<ast::Designator>(designators), value);
        }
        elements.push_back(value);
        if (!tryConsume(TokenKind::Comma)) {
            break;
        }
    }
    expect(TokenKind::RightBrace);
    return context_.create<ast::InitListExpr>(rangeFrom(begin), context_.copy<ast::Expr*>(elements));
}

// `.a[2] =`, GNU's range `[1 ... 3] =`, and GNU's obsolete forms `a:` and `[2]` without `=`.
std::vector<ast::Designator> Parser::parseDesignation() {
    using Kind = ast::Designator::Kind;
    std::vector<ast::Designator> designators;
    if (at(TokenKind::Identifier) && peek(1).is(TokenKind::Colon)) {
        const lex::Token& name = consume();
        consume();
        designators.push_back({.kind = Kind::Field,
                               .location = name.location,
                               .fieldName = name.identifier,
                               .first = nullptr,
                               .last = nullptr});
        return designators;
    }
    while (true) {
        if (at(TokenKind::Period)) {
            const SourceLocation location = consume().location;
            if (!at(TokenKind::Identifier)) {
                failExpected("member name");
            }
            designators.push_back({.kind = Kind::Field,
                                   .location = location,
                                   .fieldName = consume().identifier,
                                   .first = nullptr,
                                   .last = nullptr});
        } else if (at(TokenKind::LeftSquare)) {
            const SourceLocation location = consume().location;
            ast::Expr* first = parseConditionalExpression();
            ast::Expr* last = nullptr;
            if (tryConsume(TokenKind::Ellipsis)) {
                last = parseConditionalExpression();
            }
            expect(TokenKind::RightSquare);
            designators.push_back({.kind = last != nullptr ? Kind::Range : Kind::Index,
                                   .location = location,
                                   .fieldName = Symbol(),
                                   .first = first,
                                   .last = last});
        } else {
            break;
        }
    }
    const bool isObsoleteIndex = designators.size() == 1 && designators.front().kind != Kind::Field;
    if (!designators.empty() && !tryConsume(TokenKind::Equal) && !isObsoleteIndex) {
        failExpected("'='");
    }
    return designators;
}

} // namespace nitwit::parse::detail
