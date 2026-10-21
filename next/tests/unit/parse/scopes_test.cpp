// Name binding: scopes, the typedef/identifier ambiguities, redeclarations, and tags.

#include <gtest/gtest.h>

#include "ast/casting.hpp"
#include "parse_test_support.hpp"

namespace nitwit::parse {
namespace {

class ScopeTest : public ParserTest {
protected:
    // The statements of `f` in `text`.
    std::span<ast::Stmt* const> bodyOfF(std::string text) {
        const ast::TranslationUnit* unit = parse(std::move(text));
        EXPECT_NE(unit, nullptr) << firstError();
        return unit != nullptr ? bodyOf(unit, "f") : std::span<ast::Stmt* const>();
    }

    static const ast::Expr* expressionOf(const ast::Stmt* stmt) { return ast::cast<ast::ExpressionStmt>(stmt)->expr(); }

    static const ast::Decl* declOf(const ast::Stmt* stmt, std::size_t index = 0) {
        return ast::cast<ast::DeclarationStmt>(stmt)->decls()[index];
    }
};

TEST_F(ScopeTest, StarAfterATypedefNameDeclaresAPointer) {
    const auto body = bodyOfF("typedef int T; void f(void) { T * x; }");
    ASSERT_EQ(body.size(), 1u);
    EXPECT_EQ(ast::typeToString(ast::cast<ast::VarDecl>(declOf(body[0]))->type()), "T *");
}

TEST_F(ScopeTest, StarAfterAVariableIsAMultiplication) {
    const auto body = bodyOfF("int T, x; void f(void) { T * x; }");
    ASSERT_EQ(body.size(), 1u);
    EXPECT_TRUE(ast::isa<ast::BinaryExpr>(expressionOf(body[0])));
}

TEST_F(ScopeTest, ParenthesizedTypedefNameIsACast) {
    const auto body = bodyOfF("typedef int T; int x; void f(void) { (T)(x); }");
    ASSERT_EQ(body.size(), 1u);
    EXPECT_TRUE(ast::isa<ast::CastExpr>(expressionOf(body[0])));
}

TEST_F(ScopeTest, ParenthesizedFunctionNameIsACall) {
    const auto body = bodyOfF("int T(int); int x; void f(void) { (T)(x); }");
    ASSERT_EQ(body.size(), 1u);
    EXPECT_TRUE(ast::isa<ast::CallExpr>(expressionOf(body[0])));
}

TEST_F(ScopeTest, SizeofOfATypedefNameOrAVariable) {
    const auto body = bodyOfF("typedef int T; int v; void f(void) { sizeof(T); sizeof(v); }");
    ASSERT_EQ(body.size(), 2u);
    EXPECT_FALSE(ast::cast<ast::SizeOfAlignOfExpr>(expressionOf(body[0]))->typeOperand().isNull());
    EXPECT_NE(ast::cast<ast::SizeOfAlignOfExpr>(expressionOf(body[1]))->operand(), nullptr);
}

TEST_F(ScopeTest, VariableInAnInnerBlockShadowsATypedef) {
    const auto body = bodyOfF("typedef int T; int x; void f(void) { { int T = 2; T * x; } T * y; }");
    ASSERT_EQ(body.size(), 2u);
    const auto* inner = ast::cast<ast::CompoundStmt>(body[0]);
    EXPECT_TRUE(ast::isa<ast::BinaryExpr>(expressionOf(inner->body()[1])));
    EXPECT_TRUE(ast::isa<ast::DeclarationStmt>(body[1]));
}

TEST_F(ScopeTest, ParameterShadowsATypedef) {
    const auto body = bodyOfF("typedef int T; int x; void f(int T) { T * x; }");
    ASSERT_EQ(body.size(), 1u);
    EXPECT_TRUE(ast::isa<ast::BinaryExpr>(expressionOf(body[0])));
}

TEST_F(ScopeTest, TypedefInAnInnerBlockShadowsAVariable) {
    const auto body = bodyOfF("int T; void f(void) { typedef char T; T c; }");
    ASSERT_EQ(body.size(), 2u);
    EXPECT_EQ(ast::typeToString(ast::cast<ast::VarDecl>(declOf(body[1]))->type()), "T");
}

TEST_F(ScopeTest, NameIsInScopeInItsOwnInitializer) {
    // C17 6.2.1p7: the scope of x begins after its declarator, so the inner x refers to the new variable.
    const auto body = bodyOfF("int x; void f(void) { int x = sizeof x; }");
    ASSERT_EQ(body.size(), 1u);
    const auto* var = ast::cast<ast::VarDecl>(declOf(body[0]));
    const auto* sizeOf = ast::cast<ast::SizeOfAlignOfExpr>(var->initializer());
    EXPECT_EQ(ast::cast<ast::DeclRefExpr>(sizeOf->operand())->decl(), var);
}

TEST_F(ScopeTest, EnumConstantIsInScopeAfterItsEnumerator) {
    EXPECT_NE(parse("enum { A, B = A + 1 };"), nullptr) << firstError();
}

TEST_F(ScopeTest, ParametersAreInTheScopeOfTheBody) {
    EXPECT_EQ(errorOf("void f(int a) { int a; }"), "test.c:1:21: error: redefinition of 'a'");
}

TEST_F(ScopeTest, ParameterNamesMustBeDistinct) {
    EXPECT_EQ(errorOf("void f(int a, int a);"), "test.c:1:19: error: redefinition of 'a'");
}

TEST_F(ScopeTest, PrototypeParameterNamesDoNotLeak) {
    EXPECT_EQ(errorOf("int f(int a); int g(void) { return a; }"),
              "test.c:1:36: error: use of undeclared identifier 'a'");
}

TEST_F(ScopeTest, FileScopeRedeclarationsAreLinked) {
    const ast::TranslationUnit* unit = parse("extern int x; int x = 1; int f(int); int f(int a) { return a; }");
    ASSERT_NE(unit, nullptr);
    ASSERT_EQ(unit->decls.size(), 4u);
    EXPECT_EQ(ast::cast<ast::VarDecl>(unit->decls[1])->previous(), unit->decls[0]);
    EXPECT_EQ(ast::cast<ast::FunctionDecl>(unit->decls[3])->previous(), unit->decls[2]);
}

TEST_F(ScopeTest, UsesBindToTheLatestDeclaration) {
    const ast::TranslationUnit* unit = parse("int f(int); int f(int a) { return f(a); }");
    ASSERT_NE(unit, nullptr);
    const auto* definition = ast::cast<ast::FunctionDecl>(unit->decls[1]);
    const auto* returnStmt = ast::cast<ast::ReturnStmt>(definition->body()->body()[0]);
    const auto* call = ast::cast<ast::CallExpr>(returnStmt->value());
    EXPECT_EQ(call->directCallee(), definition);
}

TEST_F(ScopeTest, BlockScopeExternIsLinkedToTheFileScopeEntity) {
    const ast::TranslationUnit* unit = parse("int g; void f(void) { int g; { extern int g; } }");
    ASSERT_NE(unit, nullptr) << firstError();
    const auto body = bodyOf(unit, "f");
    const auto* inner = ast::cast<ast::CompoundStmt>(body[1]);
    const auto* externDecl = ast::cast<ast::VarDecl>(declOf(inner->body()[0]));
    EXPECT_EQ(externDecl->previous(), unit->decls[0]);
    EXPECT_EQ(ast::cast<ast::VarDecl>(declOf(body[0]))->previous(), nullptr);
}

TEST_F(ScopeTest, LaterFileScopeDefinitionIsLinkedToABlockScopeExtern) {
    const ast::TranslationUnit* unit = parse("void f(void) { extern int g; g = 1; } int g = 2;");
    ASSERT_NE(unit, nullptr) << firstError();
    const auto* externDecl = ast::cast<ast::VarDecl>(declOf(bodyOf(unit, "f")[0]));
    EXPECT_EQ(find<ast::VarDecl>(unit, "g")->previous(), externDecl);
}

TEST_F(ScopeTest, BlockScopeFunctionDeclarationIsLinked) {
    const ast::TranslationUnit* unit = parse("int h(void); void f(void) { int h(void); }");
    ASSERT_NE(unit, nullptr) << firstError();
    const auto* local = ast::cast<ast::FunctionDecl>(declOf(bodyOf(unit, "f")[0]));
    EXPECT_EQ(local->previous(), unit->decls[0]);
}

TEST_F(ScopeTest, RedefinitionInABlockIsAnError) {
    EXPECT_EQ(errorOf("void f(void) { int a; int a; }"), "test.c:1:27: error: redefinition of 'a'");
}

TEST_F(ScopeTest, RedeclarationAsADifferentKindIsAnError) {
    EXPECT_EQ(errorOf("int a; typedef int a;"),
              "test.c:1:20: error: redefinition of 'a' as a different kind of symbol");
}

TEST_F(ScopeTest, TypedefMayBeRedeclared) {
    EXPECT_NE(parse("typedef int T; typedef int T; T x;"), nullptr) << firstError();
}

TEST_F(ScopeTest, FunctionRedefinitionIsAnError) {
    EXPECT_EQ(errorOf("int f(void) { return 0; } int f(void) { return 1; }"),
              "test.c:1:31: error: redefinition of 'f'");
}

TEST_F(ScopeTest, ForwardDeclaredStructIsCompletedByItsDefinition) {
    const ast::TranslationUnit* unit = parse("struct S; struct S *p; struct S { int a; };");
    ASSERT_NE(unit, nullptr);
    const auto* record = find<ast::RecordDecl>(unit, "S");
    const auto* pointer = ast::cast<ast::PointerType>(find<ast::VarDecl>(unit, "p")->type().type());
    EXPECT_EQ(ast::cast<ast::RecordType>(pointer->pointee().type())->decl(), record);
    EXPECT_TRUE(record->isComplete());
}

TEST_F(ScopeTest, UseOfAnUndeclaredTagDeclaresIt) {
    const ast::TranslationUnit* unit = parse("struct S *p; struct S { int a; } s;");
    ASSERT_NE(unit, nullptr);
    const auto* first = ast::cast<ast::PointerType>(find<ast::VarDecl>(unit, "p")->type().type());
    EXPECT_EQ(first->pointee().type(), find<ast::VarDecl>(unit, "s")->type().type());
}

TEST_F(ScopeTest, ForwardDeclarationInAnInnerScopeDeclaresANewTag) {
    // C17 6.7.2.3p7: `struct S;` hides the outer S.
    const ast::TranslationUnit* unit =
        parse("struct S { int a; }; void f(void) { struct S; struct S { char c; } inner; struct S *p; }");
    ASSERT_NE(unit, nullptr) << firstError();
    const auto body = bodyOf(unit, "f");
    const auto* inner = ast::cast<ast::VarDecl>(declOf(body[1], 1));
    EXPECT_NE(ast::cast<ast::RecordType>(inner->type().type())->decl(), find<ast::RecordDecl>(unit, "S"));
    const auto* pointer = ast::cast<ast::PointerType>(ast::cast<ast::VarDecl>(declOf(body[2]))->type().type());
    EXPECT_EQ(pointer->pointee().type(), inner->type().type());
}

TEST_F(ScopeTest, UseInAnInnerScopeRefersToTheOuterTag) {
    const ast::TranslationUnit* unit = parse("struct S; void f(void) { struct S *p; } struct S { int a; };");
    ASSERT_NE(unit, nullptr) << firstError();
    const auto* pointer =
        ast::cast<ast::PointerType>(ast::cast<ast::VarDecl>(declOf(bodyOf(unit, "f")[0]))->type().type());
    EXPECT_EQ(ast::cast<ast::RecordType>(pointer->pointee().type())->decl(), find<ast::RecordDecl>(unit, "S"));
}

TEST_F(ScopeTest, TagDeclaredInsideAStructIsVisibleOutside) {
    EXPECT_NE(parse("struct S { struct T { int a; } t; }; struct T x;"), nullptr) << firstError();
}

TEST_F(ScopeTest, TagsAndOrdinaryIdentifiersAreSeparate) {
    EXPECT_NE(parse("struct S { int S; } S; typedef struct S S_t;"), nullptr) << firstError();
}

TEST_F(ScopeTest, StructRedefinitionIsAnError) {
    EXPECT_EQ(errorOf("struct S { int a; }; struct S { int b; };"), "test.c:1:29: error: redefinition of 'struct S'");
}

TEST_F(ScopeTest, TagKindMismatchIsAnError) {
    EXPECT_EQ(errorOf("struct S { int a; }; union S *p;"),
              "test.c:1:28: error: use of 'S' with tag type that does not match previous declaration");
}

TEST_F(ScopeTest, EnumUsedBeforeItsDefinition) {
    const ast::TranslationUnit* unit = parse("enum E; enum E *p; enum E { A };");
    ASSERT_NE(unit, nullptr) << firstError();
    EXPECT_TRUE(find<ast::EnumDecl>(unit, "E")->isComplete());
}

} // namespace
} // namespace nitwit::parse
