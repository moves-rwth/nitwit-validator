// Statements: every statement form, switch labels, label resolution, and the source locations witnesses use.

#include <gtest/gtest.h>

#include "ast/casting.hpp"
#include "parse_test_support.hpp"

namespace nitwit::parse {
namespace {

using StatementTest = ParserTest;

TEST_F(StatementTest, CompoundExpressionAndNullStatements) {
    EXPECT_EQ(dumpOf("void f(void) { { ; } 1; }"), "FunctionDecl f 'void (void)'\n"
                                                   "  CompoundStmt\n"
                                                   "    CompoundStmt\n"
                                                   "      NullStmt\n"
                                                   "    ExpressionStmt\n"
                                                   "      IntegerLiteral 1\n");
}

TEST_F(StatementTest, IfWithAndWithoutElse) {
    EXPECT_EQ(dumpOf("void f(int a) { if (a) a = 1; if (a) ; else a = 2; }"),
              "FunctionDecl f 'void (int)'\n"
              "  VarDecl a 'int' parameter\n"
              "  CompoundStmt\n"
              "    IfStmt\n"
              "      DeclRefExpr a (VarDecl at 1:12)\n"
              "      ExpressionStmt\n"
              "        BinaryExpr =\n"
              "          DeclRefExpr a (VarDecl at 1:12)\n"
              "          IntegerLiteral 1\n"
              "    IfStmt\n"
              "      DeclRefExpr a (VarDecl at 1:12)\n"
              "      NullStmt\n"
              "      ExpressionStmt\n"
              "        BinaryExpr =\n"
              "          DeclRefExpr a (VarDecl at 1:12)\n"
              "          IntegerLiteral 2\n");
}

TEST_F(StatementTest, ElseBindsToTheInnermostIf) {
    const ast::TranslationUnit* unit = parse("void f(int a) { if (a) if (a) ; else ; }");
    ASSERT_NE(unit, nullptr);
    const auto* outer = ast::cast<ast::IfStmt>(bodyOf(unit, "f")[0]);
    EXPECT_EQ(outer->elseStmt(), nullptr);
    EXPECT_NE(ast::cast<ast::IfStmt>(outer->thenStmt())->elseStmt(), nullptr);
}

TEST_F(StatementTest, ElseIfChainNestsInTheElseBranches) {
    const ast::TranslationUnit* unit = parse("void f(int a) {\n  if (a == 1) ;\n  else if (a == 2) ;\n  else ;\n}");
    ASSERT_NE(unit, nullptr);
    const auto* first = ast::cast<ast::IfStmt>(bodyOf(unit, "f")[0]);
    const auto* second = ast::cast<ast::IfStmt>(first->elseStmt());
    EXPECT_EQ(position(first->location()), "2:3");
    EXPECT_EQ(position(second->location()), "3:8");
    EXPECT_EQ(position(first->range().end), position(second->range().end));
    EXPECT_TRUE(ast::isa<ast::NullStmt>(second->elseStmt()));
}

TEST_F(StatementTest, LongElseIfChainsDoNotCountAsNesting) {
    std::string text = "void f(int a) { if (a == 0) ;";
    for (int i = 1; i < 20000; ++i) {
        text += std::format(" else if (a == {}) ;", i);
    }
    text += " }";
    EXPECT_NE(parse(text), nullptr) << firstError();
}

TEST_F(StatementTest, Loops) {
    EXPECT_EQ(dumpOf("void f(int n) { while (n) n--; do n++; while (n < 3); for (;;) break; }"),
              "FunctionDecl f 'void (int)'\n"
              "  VarDecl n 'int' parameter\n"
              "  CompoundStmt\n"
              "    WhileStmt\n"
              "      DeclRefExpr n (VarDecl at 1:12)\n"
              "      ExpressionStmt\n"
              "        UnaryExpr postfix --\n"
              "          DeclRefExpr n (VarDecl at 1:12)\n"
              "    DoWhileStmt\n"
              "      ExpressionStmt\n"
              "        UnaryExpr postfix ++\n"
              "          DeclRefExpr n (VarDecl at 1:12)\n"
              "      BinaryExpr <\n"
              "        DeclRefExpr n (VarDecl at 1:12)\n"
              "        IntegerLiteral 3\n"
              "    ForStmt\n"
              "      (null)\n"
              "      (null)\n"
              "      (null)\n"
              "      BreakStmt\n");
}

TEST_F(StatementTest, DoWhileKeepsTheWhileLocation) {
    const ast::TranslationUnit* unit = parse("void f(void) {\n  do ;\n  while (0);\n}");
    ASSERT_NE(unit, nullptr);
    const auto* loop = ast::cast<ast::DoWhileStmt>(bodyOf(unit, "f")[0]);
    EXPECT_EQ(position(loop->location()), "2:3");
    EXPECT_EQ(position(loop->whileLocation()), "3:3");
}

TEST_F(StatementTest, ForWithDeclarationAndExpressionClauses) {
    EXPECT_EQ(dumpOf("void f(void) { int i; for (int j = 0; j < 2; j++) continue; for (i = 0; ; ) ; }"),
              "FunctionDecl f 'void (void)'\n"
              "  CompoundStmt\n"
              "    DeclarationStmt\n"
              "      VarDecl i 'int'\n"
              "    ForStmt\n"
              "      DeclarationStmt\n"
              "        VarDecl j 'int'\n"
              "          IntegerLiteral 0\n"
              "      BinaryExpr <\n"
              "        DeclRefExpr j (VarDecl at 1:32)\n"
              "        IntegerLiteral 2\n"
              "      UnaryExpr postfix ++\n"
              "        DeclRefExpr j (VarDecl at 1:32)\n"
              "      ContinueStmt\n"
              "    ForStmt\n"
              "      ExpressionStmt\n"
              "        BinaryExpr =\n"
              "          DeclRefExpr i (VarDecl at 1:20)\n"
              "          IntegerLiteral 0\n"
              "      (null)\n"
              "      (null)\n"
              "      NullStmt\n");
}

TEST_F(StatementTest, ForDeclarationIsScopedToTheLoop) {
    EXPECT_EQ(errorOf("void f(void) { for (int i = 0; i < 2; i++) ; i = 1; }"),
              "test.c:1:46: error: use of undeclared identifier 'i'");
}

TEST_F(StatementTest, SwitchCollectsItsCaseLabelsInSourceOrder) {
    const ast::TranslationUnit* unit = parse("void f(int a) {\n"
                                             "  switch (a) {\n"
                                             "  case 1: { case 2: ; }\n"
                                             "  default: switch (a) { case 9: ; }\n"
                                             "  case 3 ... 5: break;\n"
                                             "  }\n"
                                             "}");
    ASSERT_NE(unit, nullptr) << firstError();
    const auto* outer = ast::cast<ast::SwitchStmt>(bodyOf(unit, "f")[0]);
    ASSERT_EQ(outer->cases().size(), 3u);
    EXPECT_EQ(position(outer->cases()[0]->location()), "3:3");
    EXPECT_EQ(position(outer->cases()[1]->location()), "3:13");
    EXPECT_EQ(position(outer->cases()[2]->location()), "5:3");
    EXPECT_NE(outer->cases()[2]->rangeEnd(), nullptr);
    ASSERT_NE(outer->defaultStmt(), nullptr);
    EXPECT_EQ(position(outer->defaultStmt()->location()), "4:3");
    const auto* inner = ast::cast<ast::SwitchStmt>(outer->defaultStmt()->subStmt());
    ASSERT_EQ(inner->cases().size(), 1u);
    EXPECT_EQ(inner->defaultStmt(), nullptr);
}

TEST_F(StatementTest, ConsecutiveLabelsNest) {
    EXPECT_EQ(dumpOf("void f(int a) { switch (a) { case 1: case 2: L: a = 0; } }"),
              "FunctionDecl f 'void (int)'\n"
              "  VarDecl a 'int' parameter\n"
              "  CompoundStmt\n"
              "    SwitchStmt (cases: 2)\n"
              "      DeclRefExpr a (VarDecl at 1:12)\n"
              "      CompoundStmt\n"
              "        CaseStmt\n"
              "          IntegerLiteral 1\n"
              "          CaseStmt\n"
              "            IntegerLiteral 2\n"
              "            LabelStmt L\n"
              "              ExpressionStmt\n"
              "                BinaryExpr =\n"
              "                  DeclRefExpr a (VarDecl at 1:12)\n"
              "                  IntegerLiteral 0\n");
}

TEST_F(StatementTest, ManyConsecutiveCaseLabelsDoNotCountAsNesting) {
    std::string text = "void f(int a) { switch (a) {";
    for (int i = 0; i < 20000; ++i) {
        text += std::format(" case {}:", i);
    }
    text += " break; } }";
    const ast::TranslationUnit* unit = parse(text);
    ASSERT_NE(unit, nullptr) << firstError();
    EXPECT_EQ(ast::cast<ast::SwitchStmt>(bodyOf(unit, "f")[0])->cases().size(), 20000u);
}

TEST_F(StatementTest, LabelAtTheEndOfABlockGetsANullStatement) {
    const ast::TranslationUnit* unit = parse("void f(int a) { switch (a) { default: } end: }");
    ASSERT_NE(unit, nullptr) << firstError();
    const auto* label = ast::cast<ast::LabelStmt>(bodyOf(unit, "f")[1]);
    EXPECT_TRUE(ast::isa<ast::NullStmt>(label->subStmt()));
}

TEST_F(StatementTest, LabelBeforeADeclaration) {
    const ast::TranslationUnit* unit = parse("void f(void) { L: int x = 1; goto L; }");
    ASSERT_NE(unit, nullptr) << firstError();
    EXPECT_TRUE(ast::isa<ast::DeclarationStmt>(ast::cast<ast::LabelStmt>(bodyOf(unit, "f")[0])->subStmt()));
}

TEST_F(StatementTest, GotoBindsToLabelsBeforeAndAfterIt) {
    const ast::TranslationUnit* unit = parse("void f(void) { a: goto b; { b: goto a; } }");
    ASSERT_NE(unit, nullptr) << firstError();
    const auto body = bodyOf(unit, "f");
    const auto* a = ast::cast<ast::LabelStmt>(body[0]);
    const auto* b = ast::cast<ast::LabelStmt>(ast::cast<ast::CompoundStmt>(body[1])->body()[0]);
    EXPECT_EQ(ast::cast<ast::GotoStmt>(a->subStmt())->target(), b);
    EXPECT_EQ(ast::cast<ast::GotoStmt>(b->subStmt())->target(), a);
}

TEST_F(StatementTest, LabelsAreNotSharedBetweenFunctions) {
    EXPECT_EQ(errorOf("void f(void) { L: ; }\nvoid g(void) { goto L; }"),
              "test.c:2:16: error: use of undeclared label 'L'");
}

TEST_F(StatementTest, LabelsHaveTheirOwnNamespace) {
    EXPECT_NE(parse("typedef int T; int x; void f(void) { T: x: goto T; }"), nullptr) << firstError();
}

TEST_F(StatementTest, LocalLabelsAreScopedToTheirBlock) {
    const ast::TranslationUnit* unit =
        parse("void f(void) { ({ __label__ L; goto L; L: ; }); ({ __label__ L; goto L; L: ; }); L: goto L; }");
    ASSERT_NE(unit, nullptr) << firstError();
    const auto body = bodyOf(unit, "f");
    const auto* outer = ast::cast<ast::LabelStmt>(body[2]);
    EXPECT_EQ(ast::cast<ast::GotoStmt>(outer->subStmt())->target(), outer);
    const auto* first = ast::cast<ast::StatementExpr>(ast::cast<ast::ExpressionStmt>(body[0])->expr());
    const auto* gotoStmt = ast::cast<ast::GotoStmt>(first->body()->body()[0]);
    EXPECT_EQ(gotoStmt->target(), first->body()->body()[1]);
}

TEST_F(StatementTest, ReturnWithAndWithoutValue) {
    EXPECT_EQ(dumpOf("void f(void) { return; } int g(void) { return 1; }"), "FunctionDecl f 'void (void)'\n"
                                                                            "  CompoundStmt\n"
                                                                            "    ReturnStmt\n"
                                                                            "FunctionDecl g 'int (void)'\n"
                                                                            "  CompoundStmt\n"
                                                                            "    ReturnStmt\n"
                                                                            "      IntegerLiteral 1\n");
}

TEST_F(StatementTest, AsmStatementKeepsOnlyTheTemplate) {
    EXPECT_EQ(dumpOf("void f(int x) { __asm__ __volatile__(\"\" ::: \"memory\");\n"
                     "  asm goto (\"jmp %l0\" : [o] \"=r\"(x) : \"r\"(x + 1) : \"cc\", \"memory\" : done); done: ; }"),
              "FunctionDecl f 'void (int)'\n"
              "  VarDecl x 'int' parameter\n"
              "  CompoundStmt\n"
              "    AsmStmt\n"
              "      StringLiteral \"\"\n"
              "    AsmStmt\n"
              "      StringLiteral \"jmp %l0\"\n"
              "    LabelStmt done\n"
              "      NullStmt\n");
}

TEST_F(StatementTest, StatementAttributeIsANullStatement) {
    EXPECT_EQ(dumpOf("void f(int a) { switch (a) { case 1: __attribute__((fallthrough)); default: ; } }"),
              "FunctionDecl f 'void (int)'\n"
              "  VarDecl a 'int' parameter\n"
              "  CompoundStmt\n"
              "    SwitchStmt (cases: 1, default)\n"
              "      DeclRefExpr a (VarDecl at 1:12)\n"
              "      CompoundStmt\n"
              "        CaseStmt\n"
              "          IntegerLiteral 1\n"
              "          NullStmt\n"
              "        DefaultStmt\n"
              "          NullStmt\n");
}

TEST_F(StatementTest, DeclarationsInBlocks) {
    EXPECT_EQ(dumpOf("void f(void) { __attribute__((unused)) int a; struct S { int x; } s; struct S; }"),
              "FunctionDecl f 'void (void)'\n"
              "  CompoundStmt\n"
              "    DeclarationStmt\n"
              "      VarDecl a 'int'\n"
              "        Attribute unused\n"
              "    DeclarationStmt\n"
              "      RecordDecl struct S\n"
              "        FieldDecl x 'int'\n"
              "      VarDecl s 'struct S'\n"
              "    DeclarationStmt\n");
}

TEST_F(StatementTest, StatementRangesStartAtTheirFirstToken) {
    const ast::TranslationUnit* unit = parse("void f(int a) {\n  a = 1;\n  while (a) a--;\n  return;\n}");
    ASSERT_NE(unit, nullptr);
    const auto body = bodyOf(unit, "f");
    EXPECT_EQ(position(body[0]->range().begin), "2:3");
    EXPECT_EQ(position(body[0]->range().end), "2:9");
    EXPECT_EQ(position(body[1]->range().begin), "3:3");
    EXPECT_EQ(position(body[1]->range().end), "3:17");
    EXPECT_EQ(position(body[2]->range().begin), "4:3");
}

} // namespace
} // namespace nitwit::parse
