// Statements (C17 6.8) and block declarations, with GNU local labels, case ranges and asm statements.

#include <cstddef>
#include <format>
#include <utility>
#include <vector>

#include "ast/casting.hpp"
#include "parse/parser_impl.hpp"

namespace nitwit::parse::detail {

ast::Stmt* Parser::parseStatement() {
    const DepthGuard guard(*this);
    if (startsLabel()) {
        return parseLabeledStatement();
    }
    const lex::Token& current = token();
    switch (current.kind) {
    case TokenKind::LeftBrace:
        return parseCompoundStatement(true);
    case TokenKind::KwIf:
        return parseIfStatement();
    case TokenKind::KwWhile:
        return parseWhileStatement();
    case TokenKind::KwDo:
        return parseDoStatement();
    case TokenKind::KwFor:
        return parseForStatement();
    case TokenKind::KwSwitch:
        return parseSwitchStatement();
    case TokenKind::KwGoto:
        return parseGotoStatement();
    case TokenKind::KwReturn:
        return parseReturnStatement();
    case TokenKind::KwAsm:
        return parseAsmStatement();
    case TokenKind::KwBreak:
    case TokenKind::KwContinue: {
        const bool isBreak = current.is(TokenKind::KwBreak);
        if (loopDepth_ == 0 && (!isBreak || switches_.empty())) {
            fail(current.location, isBreak ? "'break' statement not in loop or switch statement"
                                           : "'continue' statement not in loop statement");
        }
        consume();
        expect(TokenKind::Semicolon);
        if (isBreak) {
            return context_.create<ast::BreakStmt>(rangeFrom(current.location));
        }
        return context_.create<ast::ContinueStmt>(rangeFrom(current.location));
    }
    case TokenKind::Semicolon:
        consume();
        return context_.create<ast::NullStmt>(rangeFrom(current.location));
    case TokenKind::KwStaticAssert: {
        ast::Decl* const decls[] = {parseStaticAssert()};
        return context_.create<ast::DeclarationStmt>(rangeFrom(current.location), context_.copy<ast::Decl*>(decls));
    }
    case TokenKind::KwAttribute: {
        // A statement attribute such as `__attribute__((fallthrough));`, or a declaration with attributes.
        std::vector<ast::Attribute> attributes;
        parseAttributes(attributes);
        if (tryConsume(TokenKind::Semicolon)) {
            return context_.create<ast::NullStmt>(rangeFrom(current.location));
        }
        if (!startsDeclarationStatement()) {
            failExpected("declaration or ';' after attributes");
        }
        return parseDeclarationStatement(current.location, std::move(attributes));
    }
    default:
        break;
    }
    if (startsDeclarationStatement()) {
        return parseDeclarationStatement(current.location, {});
    }
    if (current.is(TokenKind::Identifier) && peek(1).is(TokenKind::Identifier)
        && lookupOrdinary(current.identifier) == nullptr) {
        failUnknownTypeName(current);
    }
    return parseExpressionStatement();
}

// Each substatement of a selection or iteration statement is a block (C17 6.8.4p3, 6.8.5p5).
ast::Stmt* Parser::parseSubstatement() {
    pushScope(ScopeKind::Block);
    ast::Stmt* stmt = parseStatement();
    popScope();
    return stmt;
}

ast::CompoundStmt* Parser::parseCompoundStatement(bool opensScope) {
    const SourceLocation begin = expect(TokenKind::LeftBrace).location;
    if (opensScope) {
        pushScope(ScopeKind::Block);
    }
    std::vector<ast::Stmt*> body;
    while (!at(TokenKind::RightBrace)) {
        if (at(TokenKind::EndOfFile)) {
            failExpected("'}'");
        }
        if (at(TokenKind::KwLabel)) {
            parseLocalLabels();
            continue;
        }
        body.push_back(parseStatement());
    }
    consume();
    if (opensScope) {
        popScope();
    }
    return context_.create<ast::CompoundStmt>(rangeFrom(begin), context_.copy<ast::Stmt*>(body));
}

bool Parser::startsLabel() const {
    return at(TokenKind::KwCase) || at(TokenKind::KwDefault)
           || (at(TokenKind::Identifier) && peek(1).is(TokenKind::Colon));
}

bool Parser::startsDeclarationStatement() const {
    std::size_t ahead = 0;
    while (peek(ahead).is(TokenKind::KwExtension)) {
        ++ahead;
    }
    const lex::Token& first = peek(ahead);
    if (first.is(TokenKind::KwAttribute)) {
        return ahead > 0; // `__extension__ __attribute__((...)) int x;`
    }
    return startsDeclaration(first) && !(first.is(TokenKind::Identifier) && peek(ahead + 1).is(TokenKind::Colon));
}

// A chain of labels, `case 1: case 2: L: statement`. It is parsed in a loop, since switches of generated programs
// can have thousands of consecutive labels. A label may end a block or precede a declaration, as in C23 and GCC.
ast::Stmt* Parser::parseLabeledStatement() {
    struct PendingLabel {
        TokenKind kind = TokenKind::Identifier;
        SourceLocation begin;
        ast::Expr* value = nullptr;
        ast::Expr* rangeEnd = nullptr;
        Symbol name;
        std::size_t switchIndex = 0;
        std::size_t caseSlot = 0;
    };
    std::vector<PendingLabel> labels;
    while (startsLabel()) {
        const lex::Token& first = consume();
        PendingLabel label;
        label.kind = first.kind;
        label.begin = first.location;
        if (first.is(TokenKind::Identifier)) {
            label.name = first.identifier;
            consume(); // :
            std::vector<ast::Attribute> ignored;
            parseAttributes(ignored);
        } else {
            if (switches_.empty()) {
                fail(first.location, std::format("'{}' label not within a switch statement",
                                                 first.is(TokenKind::KwCase) ? "case" : "default"));
            }
            label.switchIndex = switches_.size() - 1;
            if (first.is(TokenKind::KwCase)) {
                label.value = parseConditionalExpression();
                if (tryConsume(TokenKind::Ellipsis)) {
                    label.rangeEnd = parseConditionalExpression();
                }
                // Slots keep the cases in source order, also with cases in nested blocks of the same switch.
                std::vector<ast::CaseStmt*>& cases = switches_[label.switchIndex].cases;
                label.caseSlot = cases.size();
                cases.push_back(nullptr);
            } else {
                SwitchContext& context = switches_[label.switchIndex];
                if (context.hasDefault) {
                    fail(first.location, "multiple default labels in one switch");
                }
                context.hasDefault = true;
            }
            expect(TokenKind::Colon);
        }
        labels.push_back(label);
    }

    ast::Stmt* stmt = nullptr;
    if (at(TokenKind::RightBrace)) {
        stmt = context_.create<ast::NullStmt>(SourceRange{previousEnd_, previousEnd_});
    } else {
        stmt = parseStatement();
    }
    for (auto label = labels.rbegin(); label != labels.rend(); ++label) {
        const SourceRange range = rangeFrom(label->begin);
        switch (label->kind) {
        case TokenKind::KwCase: {
            auto* caseStmt = context_.create<ast::CaseStmt>(range, label->value, label->rangeEnd, stmt);
            switches_[label->switchIndex].cases[label->caseSlot] = caseStmt;
            stmt = caseStmt;
            break;
        }
        case TokenKind::KwDefault: {
            auto* defaultStmt = context_.create<ast::DefaultStmt>(range, stmt);
            switches_[label->switchIndex].defaultStmt = defaultStmt;
            stmt = defaultStmt;
            break;
        }
        default: {
            auto* labelStmt = context_.create<ast::LabelStmt>(range, label->name, stmt);
            defineLabel(labelStmt);
            stmt = labelStmt;
            break;
        }
        }
    }
    return stmt;
}

ast::Stmt* Parser::parseDeclarationStatement(SourceLocation begin, std::vector<ast::Attribute> attributes) {
    while (tryConsume(TokenKind::KwExtension)) {
    }
    const std::size_t mark = tagDefinitions_.size();
    DeclSpec spec = parseDeclarationSpecifiers(SpecifierContext::Block);
    spec.attributes.insert(spec.attributes.begin(), attributes.begin(), attributes.end());
    std::vector<ast::Decl*> decls;
    flushTagDefinitions(mark, decls);
    if (at(TokenKind::Semicolon)) {
        finishEmptyDeclaration(spec);
    } else {
        Declarator first = parseDeclarator(DeclaratorMode::Named);
        parseDeclaratorTail(first);
        if (first.isFunction() && at(TokenKind::LeftBrace)) {
            fail(token().location, "nested functions are not supported");
        }
        parseInitDeclarators(spec, std::move(first), decls);
    }
    expect(TokenKind::Semicolon);
    return context_.create<ast::DeclarationStmt>(rangeFrom(begin), context_.copy<ast::Decl*>(decls));
}

ast::Stmt* Parser::parseExpressionStatement() {
    ast::Expr* expr = parseExpression();
    expect(TokenKind::Semicolon);
    return context_.create<ast::ExpressionStmt>(rangeFrom(expr->range().begin), expr);
}

// An `else if` chain is parsed in a loop, since generated programs have long ones.
ast::Stmt* Parser::parseIfStatement() {
    struct Branch {
        SourceLocation begin;
        ast::Expr* condition;
        ast::Stmt* thenStmt;
    };
    std::vector<Branch> branches;
    ast::Stmt* elseStmt = nullptr;
    while (true) {
        const SourceLocation begin = consume().location;
        expect(TokenKind::LeftParen);
        ast::Expr* condition = parseExpression();
        expect(TokenKind::RightParen);
        branches.push_back({begin, condition, parseSubstatement()});
        if (!tryConsume(TokenKind::KwElse)) {
            break;
        }
        if (!at(TokenKind::KwIf)) {
            elseStmt = parseSubstatement();
            break;
        }
    }
    for (auto branch = branches.rbegin(); branch != branches.rend(); ++branch) {
        elseStmt =
            context_.create<ast::IfStmt>(rangeFrom(branch->begin), branch->condition, branch->thenStmt, elseStmt);
    }
    return elseStmt;
}

ast::Stmt* Parser::parseWhileStatement() {
    const SourceLocation begin = consume().location;
    expect(TokenKind::LeftParen);
    ast::Expr* condition = parseExpression();
    expect(TokenKind::RightParen);
    ++loopDepth_;
    ast::Stmt* body = parseSubstatement();
    --loopDepth_;
    return context_.create<ast::WhileStmt>(rangeFrom(begin), condition, body);
}

ast::Stmt* Parser::parseDoStatement() {
    const SourceLocation begin = consume().location;
    ++loopDepth_;
    ast::Stmt* body = parseSubstatement();
    --loopDepth_;
    const SourceLocation whileLocation = expect(TokenKind::KwWhile).location;
    expect(TokenKind::LeftParen);
    ast::Expr* condition = parseExpression();
    expect(TokenKind::RightParen);
    expect(TokenKind::Semicolon);
    return context_.create<ast::DoWhileStmt>(rangeFrom(begin), body, condition, whileLocation);
}

ast::Stmt* Parser::parseForStatement() {
    const SourceLocation begin = consume().location;
    expect(TokenKind::LeftParen);
    pushScope(ScopeKind::Block); // a declaration in the first clause is scoped to the loop (C17 6.8.5p5)
    ast::Stmt* init = nullptr;
    if (startsDeclarationStatement()) {
        init = parseDeclarationStatement(token().location, {});
    } else if (!tryConsume(TokenKind::Semicolon)) {
        init = parseExpressionStatement();
    }
    ast::Expr* condition = at(TokenKind::Semicolon) ? nullptr : parseExpression();
    expect(TokenKind::Semicolon);
    ast::Expr* increment = at(TokenKind::RightParen) ? nullptr : parseExpression();
    expect(TokenKind::RightParen);
    ++loopDepth_;
    ast::Stmt* body = parseSubstatement();
    --loopDepth_;
    popScope();
    return context_.create<ast::ForStmt>(rangeFrom(begin), init, condition, increment, body);
}

ast::Stmt* Parser::parseSwitchStatement() {
    const SourceLocation begin = consume().location;
    expect(TokenKind::LeftParen);
    ast::Expr* condition = parseExpression();
    expect(TokenKind::RightParen);
    switches_.emplace_back();
    ast::Stmt* body = parseSubstatement();
    const SwitchContext labels = std::move(switches_.back());
    switches_.pop_back();
    auto* switchStmt = context_.create<ast::SwitchStmt>(rangeFrom(begin), condition, body);
    switchStmt->setLabels(context_.copy<ast::CaseStmt*>(labels.cases), labels.defaultStmt);
    return switchStmt;
}

ast::Stmt* Parser::parseGotoStatement() {
    const SourceLocation begin = consume().location;
    if (at(TokenKind::Star)) {
        fail(token().location, "computed goto is not supported");
    }
    if (!at(TokenKind::Identifier)) {
        failExpected("label name");
    }
    const Symbol label = consume().identifier;
    expect(TokenKind::Semicolon);
    auto* gotoStmt = context_.create<ast::GotoStmt>(rangeFrom(begin), label);
    labelScopeFor(label).gotos.push_back(gotoStmt);
    return gotoStmt;
}

ast::Stmt* Parser::parseReturnStatement() {
    const SourceLocation begin = consume().location;
    ast::Expr* value = at(TokenKind::Semicolon) ? nullptr : parseExpression();
    expect(TokenKind::Semicolon);
    return context_.create<ast::ReturnStmt>(rangeFrom(begin), value);
}

// GNU `asm [volatile] [inline] [goto] ("template" : outputs : inputs : clobbers : labels);`. Only the template is
// kept, but the operands are parsed like any expression.
ast::Stmt* Parser::parseAsmStatement() {
    const SourceLocation begin = consume().location;
    while (at(TokenKind::KwVolatile) || at(TokenKind::KwInline) || at(TokenKind::KwGoto)) {
        consume();
    }
    expect(TokenKind::LeftParen);
    if (!at(TokenKind::StringLiteral)) {
        failExpected("string literal");
    }
    ast::StringLiteralExpr* asmTemplate = parseStringLiteral();
    if (tryConsume(TokenKind::Colon)) {
        parseAsmOperands(); // outputs
        if (tryConsume(TokenKind::Colon)) {
            parseAsmOperands(); // inputs
            if (tryConsume(TokenKind::Colon)) {
                while (at(TokenKind::StringLiteral)) { // clobbers
                    (void)parseStringLiteral();
                    if (!tryConsume(TokenKind::Comma)) {
                        break;
                    }
                }
                if (tryConsume(TokenKind::Colon)) {
                    while (tryConsume(TokenKind::Identifier)) { // labels of asm goto
                        if (!tryConsume(TokenKind::Comma)) {
                            break;
                        }
                    }
                }
            }
        }
    }
    expect(TokenKind::RightParen);
    expect(TokenKind::Semicolon);
    return context_.create<ast::AsmStmt>(rangeFrom(begin), asmTemplate);
}

// `[name] "constraint" (expression)`, separated by commas.
void Parser::parseAsmOperands() {
    while (at(TokenKind::StringLiteral) || at(TokenKind::LeftSquare)) {
        if (tryConsume(TokenKind::LeftSquare)) {
            expect(TokenKind::Identifier);
            expect(TokenKind::RightSquare);
        }
        if (!at(TokenKind::StringLiteral)) {
            failExpected("string literal");
        }
        (void)parseStringLiteral();
        expect(TokenKind::LeftParen);
        (void)parseExpression();
        expect(TokenKind::RightParen);
        if (!tryConsume(TokenKind::Comma)) {
            break;
        }
    }
}

// GNU `__label__ a, b;` declares labels local to the enclosing block.
void Parser::parseLocalLabels() {
    consume();
    Scope& scope = scopes_.back();
    if (!scope.localLabels) {
        scope.localLabels.emplace();
    }
    while (true) {
        if (!at(TokenKind::Identifier)) {
            failExpected("label name");
        }
        scope.localLabels->labels.try_emplace(consume().identifier, nullptr);
        if (!tryConsume(TokenKind::Comma)) {
            break;
        }
    }
    expect(TokenKind::Semicolon);
}

void Parser::failUnknownTypeName(const lex::Token& name) {
    fail(name.location, std::format("unknown type name '{}'", name.identifier.text()));
}

} // namespace nitwit::parse::detail
