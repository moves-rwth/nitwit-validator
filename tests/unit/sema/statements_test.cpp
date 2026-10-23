// Statements (C17 6.8): conditions, switch and case values, return values.

#include <gtest/gtest.h>

#include "sema_test_support.hpp"

namespace nitwit::sema {
namespace {

using StatementsTest = SemaTest;

TEST_F(StatementsTest, ConditionsAreScalarRvalues) {
    const std::string dump = dumpOf("void f(int *p) { if (p) { } while (*p) { } }");
    EXPECT_NE(dump.find("IfStmt\n      ImplicitCastExpr LValueToRValue 'int *'\n"), std::string::npos) << dump;
    EXPECT_EQ(errorOf("struct s { int a; } v; void f(void) { if (v) {} }"),
              "1:43: used struct or union type value where scalar is required in 'if' condition");
    EXPECT_EQ(errorOf("void g(void); void f(void) { while (g()) {} }"),
              "1:37: used non-scalar type value where scalar is required in 'while' condition");
}

TEST_F(StatementsTest, ForLoopsWithAndWithoutCondition) {
    EXPECT_NE(analyze("void f(void) { for (int i = 0; i < 3; i++) { } for (;;) { break; } }"), nullptr) << firstError();
}

TEST_F(StatementsTest, SwitchConditionsArePromotedAndCaseValuesConverted) {
    const ast::TranslationUnit* unit = analyze("void f(unsigned char c, unsigned u) {\n"
                                               "  switch (c) { case 'a': case 300: break; }\n"
                                               "  switch (u) { case -1: break; case 1 ... 3: break; }\n"
                                               "}\n");
    ASSERT_NE(unit, nullptr) << firstError();
    const auto* f = find<ast::FunctionDecl>(unit, "f");
    const auto* first = ast::cast<ast::SwitchStmt>(f->body()->body()[0]);
    EXPECT_EQ(ast::typeToString(first->condition()->type()), "int");
    EXPECT_EQ(first->cases()[1]->low(), 300);
    const auto* second = ast::cast<ast::SwitchStmt>(f->body()->body()[1]);
    EXPECT_EQ(ast::typeToString(second->condition()->type()), "unsigned int");
    EXPECT_EQ(second->cases()[0]->low(), 4294967295);
    EXPECT_EQ(second->cases()[1]->low(), 1);
    EXPECT_EQ(second->cases()[1]->high(), 3);
}

TEST_F(StatementsTest, SwitchErrors) {
    EXPECT_EQ(errorOf("void f(int x) { switch (x) { case 1: case 1: break; } }"), "1:38: duplicate case value");
    EXPECT_EQ(errorOf("void f(int x) { switch (x) { case 1 ... 5: case 3: break; } }"), "1:44: duplicate case value");
    EXPECT_EQ(errorOf("void f(char x) { switch (x) { case 1: case 257 - 256: break; } }"),
              "1:39: duplicate case value");
    EXPECT_EQ(errorOf("void f(double x) { switch (x) { } }"), "1:28: switch quantity not an integer");
    EXPECT_EQ(errorOf("void f(int x, int y) { switch (x) { case y: break; } }"),
              "1:42: case label is not an integer constant");
    EXPECT_NE(analyze("void f(int x) { switch (x) { case 5 ... 1: break; } }"), nullptr) << firstError();
    EXPECT_TRUE(hasWarning("empty range specified"));
}

TEST_F(StatementsTest, ReturnValuesAreConverted) {
    EXPECT_EQ(dumpOf("long f(void) { return 1; }"), "FunctionDecl f 'long (void)'\n"
                                                    "  CompoundStmt\n"
                                                    "    ReturnStmt\n"
                                                    "      ImplicitCastExpr IntegralCast 'long'\n"
                                                    "        IntegerLiteral 1 'int'\n");
}

TEST_F(StatementsTest, ReturnMismatchesWarn) {
    EXPECT_NE(analyze("int f(void) { return; }"), nullptr) << firstError();
    EXPECT_TRUE(hasWarning("'return' with no value, in function returning non-void"));
    const ast::TranslationUnit* unit = analyze("void f(void) { return 1; }");
    ASSERT_NE(unit, nullptr) << firstError();
    EXPECT_TRUE(hasWarning("'return' with a value, in function returning void"));
    const auto* returnStmt = ast::cast<ast::ReturnStmt>(find<ast::FunctionDecl>(unit, "f")->body()->body()[0]);
    EXPECT_EQ(ast::cast<ast::ImplicitCastExpr>(returnStmt->value())->castKind(), ast::CastKind::ToVoid);
    EXPECT_NE(analyze("void g(void); void f(void) { return g(); }"), nullptr) << firstError();
    EXPECT_TRUE(warnings().empty());
}

TEST_F(StatementsTest, ExpressionStatementsKeepLvalues) {
    EXPECT_EQ(dumpExpr("int x;", "x"), "DeclRefExpr x (VarDecl at 1:5) 'int' lvalue\n");
}

TEST_F(StatementsTest, LongElseIfChainsAndLabelChains) {
    std::string text = "int f(int x) {\n  if (x == 0) return 0;\n";
    for (int i = 1; i < 5000; ++i) {
        text += std::format("  else if (x == {}) return {};\n", i, i);
    }
    text += "  switch (x) {\n";
    for (int i = 0; i < 5000; ++i) {
        text += std::format("  case {}:\n", i);
    }
    text += "  return 1; }\n  return 2;\n}\n";
    EXPECT_NE(analyze(text), nullptr) << firstError();
}

TEST_F(StatementsTest, LongOperatorChains) {
    // Sema and constant evaluation walk left-nested chains in loops, so they need no deep recursion here.
    std::string expression = "x";
    std::string sum = "1";
    for (int i = 0; i < 20000; ++i) {
        expression += " + x";
        sum += " + 1";
    }
    EXPECT_EQ(typeOf("char x;", expression), "int");
    EXPECT_EQ(constantOf("", sum), 20001);
}

} // namespace
} // namespace nitwit::sema
