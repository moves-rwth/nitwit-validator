// Expressions: literals, operators and their precedence, postfix forms, casts, and the GNU builtins.

#include <gtest/gtest.h>

#include "ast/casting.hpp"
#include "parse_test_support.hpp"

namespace nitwit::parse {
namespace {

using ExpressionTest = ParserTest;

TEST_F(ExpressionTest, IntegerLiterals) {
    EXPECT_EQ(dumpExpr("", "0x10"), "IntegerLiteral 16\n");
    EXPECT_EQ(dumpExpr("", "10ul"), "IntegerLiteral 10ul\n");
    EXPECT_EQ(dumpExpr("", "0b101LL"), "IntegerLiteral 5ll\n");
}

TEST_F(ExpressionTest, FloatingLiterals) {
    EXPECT_EQ(dumpExpr("", "1.5"), "FloatingLiteral 1.5\n");
    EXPECT_EQ(dumpExpr("", "0.1f"), "FloatingLiteral 0.1f\n");
    EXPECT_EQ(dumpExpr("", "2e3L"), "FloatingLiteral 2000L\n");
}

TEST_F(ExpressionTest, CharacterLiterals) {
    EXPECT_EQ(dumpExpr("", "'a'"), "CharacterLiteral 97\n");
    EXPECT_EQ(dumpExpr("", "L'b'"), "CharacterLiteral L 98\n");
    EXPECT_EQ(dumpExpr("", "'\\377'"), "CharacterLiteral -1\n");
}

TEST_F(ExpressionTest, AdjacentStringLiteralsAreConcatenated) {
    EXPECT_EQ(dumpExpr("", "\"ab\" \"c\\n\""), "StringLiteral \"abc\\n\"\n");
    EXPECT_EQ(dumpExpr("", "L\"a\" \"b\""), "StringLiteral L\"ab\"\n");
}

TEST_F(ExpressionTest, StringLiteralRangeCoversAllTokens) {
    const ast::TranslationUnit* unit = parse("char *s = \"a\"\n  \"b\";");
    ASSERT_NE(unit, nullptr);
    const ast::Expr* literal = find<ast::VarDecl>(unit, "s")->initializer();
    EXPECT_EQ(position(literal->range().begin), "1:11");
    EXPECT_EQ(position(literal->range().end), "2:6");
}

TEST_F(ExpressionTest, MultiplicationBindsTighterThanAddition) {
    EXPECT_EQ(dumpExpr("int a, b, c;", "a + b * c"), "BinaryExpr +\n"
                                                     "  DeclRefExpr a (VarDecl at 1:5)\n"
                                                     "  BinaryExpr *\n"
                                                     "    DeclRefExpr b (VarDecl at 1:8)\n"
                                                     "    DeclRefExpr c (VarDecl at 1:11)\n");
}

TEST_F(ExpressionTest, BinaryOperatorsAreLeftAssociative) {
    EXPECT_EQ(dumpExpr("int a, b, c;", "a - b - c"), "BinaryExpr -\n"
                                                     "  BinaryExpr -\n"
                                                     "    DeclRefExpr a (VarDecl at 1:5)\n"
                                                     "    DeclRefExpr b (VarDecl at 1:8)\n"
                                                     "  DeclRefExpr c (VarDecl at 1:11)\n");
}

TEST_F(ExpressionTest, PrecedenceOfAllBinaryOperators) {
    // From the lowest to the highest precedence: || && | ^ & == < << + *
    EXPECT_EQ(dumpExpr("int a;", "a || a && a | a ^ a & a == a < a << a + a * a"),
              "BinaryExpr ||\n"
              "  DeclRefExpr a (VarDecl at 1:5)\n"
              "  BinaryExpr &&\n"
              "    DeclRefExpr a (VarDecl at 1:5)\n"
              "    BinaryExpr |\n"
              "      DeclRefExpr a (VarDecl at 1:5)\n"
              "      BinaryExpr ^\n"
              "        DeclRefExpr a (VarDecl at 1:5)\n"
              "        BinaryExpr &\n"
              "          DeclRefExpr a (VarDecl at 1:5)\n"
              "          BinaryExpr ==\n"
              "            DeclRefExpr a (VarDecl at 1:5)\n"
              "            BinaryExpr <\n"
              "              DeclRefExpr a (VarDecl at 1:5)\n"
              "              BinaryExpr <<\n"
              "                DeclRefExpr a (VarDecl at 1:5)\n"
              "                BinaryExpr +\n"
              "                  DeclRefExpr a (VarDecl at 1:5)\n"
              "                  BinaryExpr *\n"
              "                    DeclRefExpr a (VarDecl at 1:5)\n"
              "                    DeclRefExpr a (VarDecl at 1:5)\n");
}

TEST_F(ExpressionTest, AssignmentIsRightAssociative) {
    EXPECT_EQ(dumpExpr("int a, b;", "a = b += 2"), "BinaryExpr =\n"
                                                   "  DeclRefExpr a (VarDecl at 1:5)\n"
                                                   "  BinaryExpr +=\n"
                                                   "    DeclRefExpr b (VarDecl at 1:8)\n"
                                                   "    IntegerLiteral 2\n");
}

TEST_F(ExpressionTest, CommaOperator) {
    EXPECT_EQ(dumpExpr("int a;", "a, 1, 2"), "BinaryExpr ,\n"
                                             "  BinaryExpr ,\n"
                                             "    DeclRefExpr a (VarDecl at 1:5)\n"
                                             "    IntegerLiteral 1\n"
                                             "  IntegerLiteral 2\n");
}

TEST_F(ExpressionTest, BinaryExpressionKeepsTheOperatorLocation) {
    const ast::TranslationUnit* unit = parse("int a = 1 <<  2;");
    ASSERT_NE(unit, nullptr);
    const auto* shift = ast::cast<ast::BinaryExpr>(find<ast::VarDecl>(unit, "a")->initializer());
    EXPECT_EQ(position(shift->operatorLocation()), "1:11");
    EXPECT_EQ(position(shift->range().begin), "1:9");
    EXPECT_EQ(position(shift->range().end), "1:16");
}

TEST_F(ExpressionTest, ConditionalIsRightAssociative) {
    EXPECT_EQ(dumpExpr("int a;", "a ? 1 : a ? 2 : 3"), "ConditionalExpr\n"
                                                       "  DeclRefExpr a (VarDecl at 1:5)\n"
                                                       "  IntegerLiteral 1\n"
                                                       "  ConditionalExpr\n"
                                                       "    DeclRefExpr a (VarDecl at 1:5)\n"
                                                       "    IntegerLiteral 2\n"
                                                       "    IntegerLiteral 3\n");
}

TEST_F(ExpressionTest, ConditionalKeepsTheQuestionMarkLocation) {
    const ast::TranslationUnit* unit = parse("int a, b = a ? 1 : 2;");
    ASSERT_NE(unit, nullptr);
    const auto* conditional = ast::cast<ast::ConditionalExpr>(find<ast::VarDecl>(unit, "b")->initializer());
    EXPECT_EQ(position(conditional->questionLocation()), "1:14");
}

TEST_F(ExpressionTest, ConditionalWithoutMiddleOperand) {
    EXPECT_EQ(dumpExpr("int a;", "a ?: 2"), "ConditionalExpr ?:\n"
                                            "  DeclRefExpr a (VarDecl at 1:5)\n"
                                            "  IntegerLiteral 2\n");
}

TEST_F(ExpressionTest, MiddleOperandOfConditionalMayBeACommaExpression) {
    EXPECT_EQ(dumpExpr("int a;", "a ? 1, 2 : 3"), "ConditionalExpr\n"
                                                  "  DeclRefExpr a (VarDecl at 1:5)\n"
                                                  "  BinaryExpr ,\n"
                                                  "    IntegerLiteral 1\n"
                                                  "    IntegerLiteral 2\n"
                                                  "  IntegerLiteral 3\n");
}

TEST_F(ExpressionTest, PrefixOperators) {
    EXPECT_EQ(dumpExpr("int a, *p;", "-~!*&a"), "UnaryExpr -\n"
                                                "  UnaryExpr ~\n"
                                                "    UnaryExpr !\n"
                                                "      UnaryExpr *\n"
                                                "        UnaryExpr &\n"
                                                "          DeclRefExpr a (VarDecl at 1:5)\n");
    EXPECT_EQ(dumpExpr("int a;", "++a"), "UnaryExpr ++\n  DeclRefExpr a (VarDecl at 1:5)\n");
    EXPECT_EQ(dumpExpr("int a;", "+--a"), "UnaryExpr +\n"
                                          "  UnaryExpr --\n"
                                          "    DeclRefExpr a (VarDecl at 1:5)\n");
}

TEST_F(ExpressionTest, PostfixOperatorsBindTighterThanPrefixOperators) {
    EXPECT_EQ(dumpExpr("int *p;", "*p++"), "UnaryExpr *\n"
                                           "  UnaryExpr postfix ++\n"
                                           "    DeclRefExpr p (VarDecl at 1:6)\n");
}

TEST_F(ExpressionTest, MemberAccessAndSubscripts) {
    EXPECT_EQ(dumpExpr("struct S { int a[2]; struct S *next; } s;", "s.next->a[1]"),
              "ArraySubscriptExpr\n"
              "  MemberExpr ->a\n"
              "    MemberExpr .next\n"
              "      DeclRefExpr s (VarDecl at 1:40)\n"
              "  IntegerLiteral 1\n");
}

TEST_F(ExpressionTest, MemberExpressionKeepsTheMemberLocation) {
    const ast::TranslationUnit* unit = parse("struct S { int a; } s; int b = s . a;");
    ASSERT_NE(unit, nullptr);
    const auto* member = ast::cast<ast::MemberExpr>(find<ast::VarDecl>(unit, "b")->initializer());
    EXPECT_EQ(position(member->memberLocation()), "1:36");
    EXPECT_FALSE(member->isArrow());
}

TEST_F(ExpressionTest, CallWithArguments) {
    EXPECT_EQ(dumpExpr("int f(int, int); int a;", "f(a, a + 1)"), "CallExpr\n"
                                                                  "  DeclRefExpr f (FunctionDecl at 1:5)\n"
                                                                  "  DeclRefExpr a (VarDecl at 1:22)\n"
                                                                  "  BinaryExpr +\n"
                                                                  "    DeclRefExpr a (VarDecl at 1:22)\n"
                                                                  "    IntegerLiteral 1\n");
}

TEST_F(ExpressionTest, CallKeepsTheRightParenthesisLocation) {
    const ast::TranslationUnit* unit = parse("int f(void);\nint x = f(\n  );");
    ASSERT_NE(unit, nullptr);
    const auto* call = ast::cast<ast::CallExpr>(find<ast::VarDecl>(unit, "x")->initializer());
    EXPECT_EQ(position(call->rightParenLocation()), "3:3");
    EXPECT_EQ(call->directCallee(), find<ast::FunctionDecl>(unit, "f"));
}

TEST_F(ExpressionTest, CastOfATypeName) {
    EXPECT_EQ(dumpExpr("long a;", "(int)(char *)a"), "CastExpr (int)\n"
                                                     "  CastExpr (char *)\n"
                                                     "    DeclRefExpr a (VarDecl at 1:6)\n");
}

TEST_F(ExpressionTest, ParenthesizedExpressionIsKept) {
    EXPECT_EQ(dumpExpr("int a;", "(a)"), "ParenExpr\n  DeclRefExpr a (VarDecl at 1:5)\n");
}

TEST_F(ExpressionTest, CompoundLiteral) {
    EXPECT_EQ(dumpExpr("struct P { int x, y; };", "(struct P){ 1, .y = 2 }.y"), "MemberExpr .y\n"
                                                                                "  CompoundLiteralExpr (struct P)\n"
                                                                                "    InitListExpr\n"
                                                                                "      IntegerLiteral 1\n"
                                                                                "      DesignatedInitExpr .y\n"
                                                                                "        IntegerLiteral 2\n");
}

TEST_F(ExpressionTest, CompoundLiteralAtFileScope) {
    EXPECT_EQ(dumpOf("int *p = (int[]){ 1 };"), "VarDecl p 'int *'\n"
                                                "  CompoundLiteralExpr (int []) file_scope\n"
                                                "    InitListExpr\n"
                                                "      IntegerLiteral 1\n");
}

TEST_F(ExpressionTest, SizeofOfExpressionsAndTypes) {
    EXPECT_EQ(dumpExpr("int a;", "sizeof a + sizeof(a) + sizeof(int)"), "BinaryExpr +\n"
                                                                        "  BinaryExpr +\n"
                                                                        "    SizeOfAlignOfExpr sizeof\n"
                                                                        "      DeclRefExpr a (VarDecl at 1:5)\n"
                                                                        "    SizeOfAlignOfExpr sizeof\n"
                                                                        "      ParenExpr\n"
                                                                        "        DeclRefExpr a (VarDecl at 1:5)\n"
                                                                        "  SizeOfAlignOfExpr sizeof (int)\n");
}

TEST_F(ExpressionTest, SizeofOfACompoundLiteral) {
    EXPECT_EQ(dumpExpr("", "sizeof (int[]){ 1, 2 }"), "SizeOfAlignOfExpr sizeof\n"
                                                      "  CompoundLiteralExpr (int [])\n"
                                                      "    InitListExpr\n"
                                                      "      IntegerLiteral 1\n"
                                                      "      IntegerLiteral 2\n");
}

TEST_F(ExpressionTest, SizeofTypeThenMultiplication) {
    EXPECT_EQ(dumpExpr("", "sizeof(int) * 2"), "BinaryExpr *\n"
                                               "  SizeOfAlignOfExpr sizeof (int)\n"
                                               "  IntegerLiteral 2\n");
}

TEST_F(ExpressionTest, AlignofForms) {
    EXPECT_EQ(dumpExpr("double d;", "_Alignof(double) + __alignof__(double) + __alignof__ d"),
              "BinaryExpr +\n"
              "  BinaryExpr +\n"
              "    SizeOfAlignOfExpr _Alignof (double)\n"
              "    SizeOfAlignOfExpr __alignof__ (double)\n"
              "  SizeOfAlignOfExpr __alignof__\n"
              "    DeclRefExpr d (VarDecl at 1:8)\n");
}

TEST_F(ExpressionTest, RealImagAndExtension) {
    EXPECT_EQ(dumpExpr("_Complex double z;", "__real__ z + __imag__ z + __extension__ 1"),
              "BinaryExpr +\n"
              "  BinaryExpr +\n"
              "    UnaryExpr __real__\n"
              "      DeclRefExpr z (VarDecl at 1:17)\n"
              "    UnaryExpr __imag__\n"
              "      DeclRefExpr z (VarDecl at 1:17)\n"
              "  UnaryExpr __extension__\n"
              "    IntegerLiteral 1\n");
}

TEST_F(ExpressionTest, StatementExpression) {
    EXPECT_EQ(dumpExpr("", "({ int t = 1; t; })"), "StatementExpr\n"
                                                   "  CompoundStmt\n"
                                                   "    DeclarationStmt\n"
                                                   "      VarDecl t 'int'\n"
                                                   "        IntegerLiteral 1\n"
                                                   "    ExpressionStmt\n"
                                                   "      DeclRefExpr t (VarDecl at 3:8)\n");
}

TEST_F(ExpressionTest, GenericSelection) {
    EXPECT_EQ(dumpExpr("int a;", "_Generic(a, int: 1, char *: 2, default: 3)"), "GenericSelectionExpr\n"
                                                                                "  DeclRefExpr a (VarDecl at 1:5)\n"
                                                                                "  GenericAssociation (int)\n"
                                                                                "    IntegerLiteral 1\n"
                                                                                "  GenericAssociation (char *)\n"
                                                                                "    IntegerLiteral 2\n"
                                                                                "  GenericAssociation default\n"
                                                                                "    IntegerLiteral 3\n");
}

TEST_F(ExpressionTest, BuiltinVaArg) {
    EXPECT_EQ(dumpExpr("__builtin_va_list ap;", "__builtin_va_arg(ap, long)"), "VaArgExpr (long)\n"
                                                                               "  DeclRefExpr ap (VarDecl at 1:19)\n");
}

TEST_F(ExpressionTest, BuiltinOffsetof) {
    EXPECT_EQ(dumpExpr("struct S { struct { int b[3]; } a; }; int i;", "__builtin_offsetof(struct S, a.b[i + 1])"),
              "OffsetOfExpr (struct S) a.b[i + 1]\n");
}

TEST_F(ExpressionTest, BuiltinOffsetofComponents) {
    const ast::TranslationUnit* unit =
        parse("struct S { int b[3]; }; unsigned long o = __builtin_offsetof(struct S, b[2]);");
    ASSERT_NE(unit, nullptr);
    const auto* offsetOf = ast::cast<ast::OffsetOfExpr>(find<ast::VarDecl>(unit, "o")->initializer());
    ASSERT_EQ(offsetOf->components().size(), 2u);
    EXPECT_TRUE(offsetOf->components()[0].isField);
    EXPECT_EQ(offsetOf->components()[0].fieldName.text(), "b");
    EXPECT_FALSE(offsetOf->components()[1].isField);
    EXPECT_TRUE(ast::isa<ast::IntegerLiteralExpr>(offsetOf->components()[1].index));
}

TEST_F(ExpressionTest, BuiltinTypesCompatibleAndChooseExpr) {
    EXPECT_EQ(dumpExpr("int a;", "__builtin_choose_expr(__builtin_types_compatible_p(int, long), a, 2)"),
              "ChooseExpr\n"
              "  TypesCompatibleExpr (int, long)\n"
              "  DeclRefExpr a (VarDecl at 1:5)\n"
              "  IntegerLiteral 2\n");
}

TEST_F(ExpressionTest, FunctionNameIdentifiersAreStringLiterals) {
    const ast::TranslationUnit* unit = parse("void work(void) { __func__; __FUNCTION__; __PRETTY_FUNCTION__; }");
    ASSERT_NE(unit, nullptr) << firstError();
    for (const ast::Stmt* stmt : bodyOf(unit, "work")) {
        const auto* literal = ast::cast<ast::StringLiteralExpr>(ast::cast<ast::ExpressionStmt>(stmt)->expr());
        EXPECT_EQ(literal->codeUnits().size(), 4u);
        EXPECT_EQ(literal->codeUnits()[0], static_cast<std::uint32_t>('w'));
    }
}

TEST_F(ExpressionTest, CallOfAnUndeclaredFunctionDeclaresItImplicitly) {
    const ast::TranslationUnit* unit = parse("int main(void) { return foo(1) + foo(2); }");
    ASSERT_NE(unit, nullptr) << firstError();
    ASSERT_EQ(unit->decls.size(), 2u);
    const auto* implicit = ast::cast<ast::FunctionDecl>(unit->decls[0]);
    EXPECT_EQ(implicit->name().text(), "foo");
    EXPECT_TRUE(implicit->isImplicit());
    EXPECT_EQ(ast::typeToString(implicit->type()), "int ()");
    EXPECT_EQ(messages(), std::vector<std::string>{"test.c:1:25: warning: implicit declaration of function 'foo'"});
}

TEST_F(ExpressionTest, ImplicitDeclarationIsLinkedToTheLaterDefinition) {
    const ast::TranslationUnit* unit = parse("int main(void) { return f(); }\nint f(void) { return 1; }");
    ASSERT_NE(unit, nullptr);
    const auto* definition = find<ast::FunctionDecl>(unit, "f");
    ASSERT_NE(definition->previous(), nullptr);
    EXPECT_TRUE(definition->previous()->isImplicit());
}

TEST_F(ExpressionTest, BuiltinFunctionsAreDeclaredImplicitlyWithoutWarning) {
    const ast::TranslationUnit* unit = parse("int f(int x) { return __builtin_expect(x, 0); }");
    ASSERT_NE(unit, nullptr);
    EXPECT_TRUE(find<ast::FunctionDecl>(unit, "__builtin_expect")->isImplicit());
    EXPECT_TRUE(messages().empty());
}

TEST_F(ExpressionTest, UndeclaredAttributeArgumentsAreUnbound) {
    EXPECT_EQ(dumpOf("typedef int QI __attribute__((__mode__(__QI__)));"), "TypedefDecl QI 'int'\n"
                                                                           "  Attribute mode\n"
                                                                           "    DeclRefExpr __QI__ (unbound)\n");
}

} // namespace
} // namespace nitwit::parse
