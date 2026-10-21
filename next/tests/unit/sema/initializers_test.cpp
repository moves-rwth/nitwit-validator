// Initializers (C17 6.7.9): the semantic form of initializer lists, designators, brace elision and strings.

#include <gtest/gtest.h>

#include "sema_test_support.hpp"

namespace nitwit::sema {
namespace {

class InitializersTest : public SemaTest {
protected:
    // The dump of the initializer of the variable `x` that `text` declares, without positions.
    std::string initializerOf(std::string text, DataModel model = DataModel::LP64) {
        const ast::TranslationUnit* unit = analyze(std::move(text), model);
        EXPECT_NE(unit, nullptr) << firstError();
        if (unit == nullptr) {
            return "";
        }
        return withoutPositions(ast::dump(find<ast::VarDecl>(unit, "x")->initializer(), sources_));
    }
};

TEST_F(InitializersTest, StructsGetOneElementPerField) {
    EXPECT_EQ(initializerOf("struct s { int a; char b; double c; } x = { 1 };"), "InitListExpr semantic 'struct s'\n"
                                                                                 "  IntegerLiteral 1 'int'\n"
                                                                                 "  ImplicitZeroExpr 'char'\n"
                                                                                 "  ImplicitZeroExpr 'double'\n");
}

TEST_F(InitializersTest, ElementsAreConvertedToTheMemberTypes) {
    EXPECT_EQ(initializerOf("struct s { char a; double b; } x = { 1, 2 };"),
              "InitListExpr semantic 'struct s'\n"
              "  ImplicitCastExpr IntegralCast 'char'\n"
              "    IntegerLiteral 1 'int'\n"
              "  ImplicitCastExpr IntegralToFloating 'double'\n"
              "    IntegerLiteral 2 'int'\n");
}

TEST_F(InitializersTest, DesignatorsSelectMembersInAnyOrder) {
    EXPECT_EQ(initializerOf("struct s { int a, b, c; } x = { .c = 3, .a = 1 };"), "InitListExpr semantic 'struct s'\n"
                                                                                  "  IntegerLiteral 1 'int'\n"
                                                                                  "  ImplicitZeroExpr 'int'\n"
                                                                                  "  IntegerLiteral 3 'int'\n");
}

TEST_F(InitializersTest, ElementsContinueAfterADesignatedMember) {
    EXPECT_EQ(initializerOf("struct s { int a, b, c; } x = { .b = 2, 3 };"), "InitListExpr semantic 'struct s'\n"
                                                                             "  ImplicitZeroExpr 'int'\n"
                                                                             "  IntegerLiteral 2 'int'\n"
                                                                             "  IntegerLiteral 3 'int'\n");
}

TEST_F(InitializersTest, NestedDesignatorsContinueInTheInnermostAggregate) {
    EXPECT_EQ(initializerOf("struct p { int x, y; }; struct l { struct p from, to; } x = { .from.y = 2, 3, 4 };"),
              "InitListExpr semantic 'struct l'\n"
              "  InitListExpr semantic 'struct p'\n"
              "    ImplicitZeroExpr 'int'\n"
              "    IntegerLiteral 2 'int'\n"
              "  InitListExpr semantic 'struct p'\n"
              "    IntegerLiteral 3 'int'\n"
              "    IntegerLiteral 4 'int'\n");
}

TEST_F(InitializersTest, BracesMayBeElided) {
    EXPECT_EQ(initializerOf("struct p { int x, y; }; struct l { struct p from, to; } x = { 1, 2, 3 };"),
              "InitListExpr semantic 'struct l'\n"
              "  InitListExpr semantic 'struct p'\n"
              "    IntegerLiteral 1 'int'\n"
              "    IntegerLiteral 2 'int'\n"
              "  InitListExpr semantic 'struct p'\n"
              "    IntegerLiteral 3 'int'\n"
              "    ImplicitZeroExpr 'int'\n");
    EXPECT_EQ(initializerOf("int x[2][2] = { 1, 2, 3 };"), "InitListExpr semantic 'int [2][2]'\n"
                                                           "  InitListExpr semantic 'int [2]'\n"
                                                           "    IntegerLiteral 1 'int'\n"
                                                           "    IntegerLiteral 2 'int'\n"
                                                           "  InitListExpr semantic 'int [2]'\n"
                                                           "    IntegerLiteral 3 'int'\n");
}

TEST_F(InitializersTest, ArrayDesignatorsAndGaps) {
    EXPECT_EQ(initializerOf("int x[5] = { [3] = 3, 4, [1] = 1 };"), "InitListExpr semantic 'int [5]'\n"
                                                                    "  ImplicitZeroExpr 'int'\n"
                                                                    "  IntegerLiteral 1 'int'\n"
                                                                    "  ImplicitZeroExpr 'int'\n"
                                                                    "  IntegerLiteral 3 'int'\n"
                                                                    "  IntegerLiteral 4 'int'\n");
    EXPECT_EQ(errorOf("int x[2] = { [2] = 1 };"), "1:15: array index in initializer exceeds array bounds");
}

TEST_F(InitializersTest, RangeDesignatorsShareTheirValue) {
    const ast::TranslationUnit* unit = analyze("long x[4] = { [1 ... 2] = 7 };");
    ASSERT_NE(unit, nullptr) << firstError();
    const auto* list = ast::cast<ast::InitListExpr>(find<ast::VarDecl>(unit, "x")->initializer());
    ASSERT_EQ(list->elements().size(), 3u);
    EXPECT_EQ(list->elements()[1], list->elements()[2]);
    EXPECT_EQ(ast::typeToString(list->elements()[1]->type()), "long");
}

TEST_F(InitializersTest, UnionsInitializeOneMember) {
    EXPECT_EQ(initializerOf("union u { char c; int i; } x = { 65 };"), "InitListExpr semantic union_field c 'union u'\n"
                                                                       "  ImplicitCastExpr IntegralCast 'char'\n"
                                                                       "    IntegerLiteral 65 'int'\n");
    EXPECT_EQ(initializerOf("union u { char c; int i; } x = { .i = 2 };"),
              "InitListExpr semantic union_field i 'union u'\n"
              "  IntegerLiteral 2 'int'\n");
    EXPECT_EQ(initializerOf("union u { char c; int i; } x = {};"), "InitListExpr semantic 'union u'\n");
}

TEST_F(InitializersTest, UnnamedBitFieldsAreSkippedButGetZero) {
    EXPECT_EQ(initializerOf("struct s { int a : 3; int : 5; int b : 4; } x = { 1, 2 };"),
              "InitListExpr semantic 'struct s'\n"
              "  IntegerLiteral 1 'int'\n"
              "  ImplicitZeroExpr 'int'\n"
              "  IntegerLiteral 2 'int'\n");
}

TEST_F(InitializersTest, AnonymousMembersTakePartInInitialization) {
    EXPECT_EQ(initializerOf("struct s { int a; union { int b; float c; }; int d; } x = { 1, 2, 3 };"),
              "InitListExpr semantic 'struct s'\n"
              "  IntegerLiteral 1 'int'\n"
              "  InitListExpr semantic union_field b 'union <anonymous>'\n"
              "    IntegerLiteral 2 'int'\n"
              "  IntegerLiteral 3 'int'\n");
    EXPECT_EQ(initializerOf("struct s { int a; union { int b; float c; }; } x = { .c = 1.5f };"),
              "InitListExpr semantic 'struct s'\n"
              "  ImplicitZeroExpr 'int'\n"
              "  InitListExpr semantic union_field c 'union <anonymous>'\n"
              "    FloatingLiteral 1.5f 'float'\n");
}

TEST_F(InitializersTest, StringsInitializeCharacterArrays) {
    EXPECT_EQ(initializerOf("char x[] = \"ab\";"), "StringLiteral \"ab\" 'char [3]' lvalue\n");
    EXPECT_EQ(initializerOf("char x[] = { \"ab\" };"), "StringLiteral \"ab\" 'char [3]' lvalue\n");
    EXPECT_EQ(initializerOf("struct s { char name[4]; int n; } x = { \"abc\", 1 };"),
              "InitListExpr semantic 'struct s'\n"
              "  StringLiteral \"abc\" 'char [4]' lvalue\n"
              "  IntegerLiteral 1 'int'\n");
    EXPECT_NE(analyze("char x[2] = \"ab\";"), nullptr) << firstError();
    EXPECT_TRUE(warnings().empty());
    EXPECT_NE(analyze("char x[2] = \"abc\";"), nullptr) << firstError();
    EXPECT_TRUE(hasWarning("initializer-string for array is too long"));
    EXPECT_NE(analyze("int x[] = L\"ab\"; unsigned short y[] = u\"ab\";"), nullptr) << firstError();
    EXPECT_EQ(errorOf("int x[] = \"ab\";"), "1:11: array of inappropriate type initialized from string constant");
}

TEST_F(InitializersTest, BracedScalarsAreReplacedByTheirElement) {
    EXPECT_EQ(initializerOf("int x = { 5 };"), "IntegerLiteral 5 'int'\n");
    EXPECT_EQ(initializerOf("int x = {};"), "ImplicitZeroExpr 'int'\n");
    EXPECT_EQ(initializerOf("double x = { 1 };"), "ImplicitCastExpr IntegralToFloating 'double'\n"
                                                  "  IntegerLiteral 1 'int'\n");
}

TEST_F(InitializersTest, StructuresFromExpressions) {
    EXPECT_EQ(initializerOf("struct s { int a; } v; struct t { struct s in; int b; } x = { v, 2 };"),
              "InitListExpr semantic 'struct t'\n"
              "  ImplicitCastExpr LValueToRValue 'struct s'\n"
              "    DeclRefExpr v (VarDecl at 1:21) 'struct s' lvalue\n"
              "  IntegerLiteral 2 'int'\n");
}

TEST_F(InitializersTest, ExcessElementsWarn) {
    EXPECT_NE(analyze("int x[1] = { 1, 2 };"), nullptr) << firstError();
    EXPECT_TRUE(hasWarning("excess elements in initializer"));
    EXPECT_NE(analyze("int x = { 1, 2 };"), nullptr) << firstError();
    EXPECT_TRUE(hasWarning("excess elements in scalar initializer"));
}

TEST_F(InitializersTest, DesignatorErrors) {
    EXPECT_EQ(errorOf("struct s { int a; } x = { .b = 1 };"), "1:27: unknown field 'b' specified in initializer");
    EXPECT_EQ(errorOf("int x[2] = { .a = 1 };"), "1:14: field name not in record or union initializer");
    EXPECT_EQ(errorOf("struct s { int a; } x = { [0] = 1 };"), "1:27: array index in non-array initializer");
    EXPECT_EQ(errorOf("int x[2] = 5;"), "1:12: array initialized from non-constant array expression");
}

TEST_F(InitializersTest, CompoundLiterals) {
    EXPECT_EQ(typeOf("", "(int[]){1, 2, 3}"), "int [3] lvalue");
    EXPECT_EQ(dumpExpr("", "(long){1}"), "CompoundLiteralExpr (long) 'long' lvalue\n"
                                         "  InitListExpr semantic 'long'\n"
                                         "    ImplicitCastExpr IntegralCast 'long'\n"
                                         "      IntegerLiteral 1 'int'\n");
    EXPECT_EQ(typeOf("", "(char[]){\"ab\"}"), "char [3] lvalue");
}

TEST_F(InitializersTest, FlexibleArrayMembersGetTheirInitializedSize) {
    EXPECT_EQ(initializerOf("struct s { int n; int d[]; } x = { 2, { 3, 4 } };"), "InitListExpr semantic 'struct s'\n"
                                                                                  "  IntegerLiteral 2 'int'\n"
                                                                                  "  InitListExpr semantic 'int [2]'\n"
                                                                                  "    IntegerLiteral 3 'int'\n"
                                                                                  "    IntegerLiteral 4 'int'\n");
}

TEST_F(InitializersTest, OnlyStaticObjectsInitializeFlexibleArrayMembers) {
    EXPECT_EQ(errorOf("struct F { int n; int a[]; }; void f(void) { struct F x = { 1, { 2 } }; }"),
              "1:64: non-static initialization of a flexible array member");
    EXPECT_EQ(errorOf("struct F { int n; int a[]; }; void f(void) { struct F x = { 1, {} }; }"),
              "1:64: non-static initialization of a flexible array member");
    EXPECT_EQ(errorOf("struct F { int n; int a[]; }; void f(void) { (struct F){ .a = { 1 } }; }"),
              "1:63: non-static initialization of a flexible array member");
    EXPECT_NE(analyze("struct F { int n; int a[]; }; struct F g = { 1, { 2 } };\n"
                      "void f(void) { static struct F s = { 1, { 2, 3 } }; struct F z = { 1 }; }"),
              nullptr)
        << firstError();
}

TEST_F(InitializersTest, HugeDesignatorsAreRejectedBeforeTheirElementsAreMade) {
    EXPECT_EQ(errorOf("int a[60000000] = { [59999999] = 1 };"),
              "1:34: an array initializer with this many elements is not supported");
    EXPECT_EQ(errorOf("int a[60000000] = { [0 ... 59999999] = 1 };"),
              "1:40: an array initializer with this many elements is not supported");
}

TEST_F(InitializersTest, GapsShareOneZero) {
    const ast::TranslationUnit* unit = analyze("int x[5] = { [4] = 1 };");
    ASSERT_NE(unit, nullptr) << firstError();
    const auto* list = ast::cast<ast::InitListExpr>(find<ast::VarDecl>(unit, "x")->initializer());
    ASSERT_EQ(list->elements().size(), 5u);
    EXPECT_TRUE(ast::isa<ast::ImplicitZeroExpr>(list->elements()[0]));
    EXPECT_EQ(list->elements()[0], list->elements()[3]);
}

} // namespace
} // namespace nitwit::sema
