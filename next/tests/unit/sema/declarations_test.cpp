// Declarations: array sizes and typeof, redeclarations, tentative definitions, attributes, enumerations, bit-field
// widths, _Static_assert, and the locals of functions.

#include <gtest/gtest.h>

#include "sema_test_support.hpp"

namespace nitwit::sema {
namespace {

class DeclarationsTest : public SemaTest {
protected:
    // The type of the last top-level variable or function `name` after analyzing `text`.
    std::string typeOfDecl(std::string text, std::string_view name, DataModel model = DataModel::LP64) {
        const ast::TranslationUnit* unit = analyze(std::move(text), model);
        EXPECT_NE(unit, nullptr) << firstError();
        return unit != nullptr ? ast::typeToString(find<ast::ValueDecl>(unit, name)->type()) : "";
    }
};

TEST_F(DeclarationsTest, ArraySizesAreConstantExpressions) {
    EXPECT_EQ(typeOfDecl("int a[2 + 3];", "a"), "int [5]");
    EXPECT_EQ(typeOfDecl("enum { N = 4 }; int a[N][N * 2];", "a"), "int [4][8]");
    EXPECT_EQ(typeOfDecl("int a[0];", "a"), "int [0]");
    EXPECT_EQ(errorOf("int a[-1];"), "1:7: size of array is negative");
    EXPECT_EQ(errorOf("int a[1.5];"), "1:7: size of array has non-integer type");
    EXPECT_EQ(errorOf("char a[0x7fffffff][2];", DataModel::ILP32), "1:8: size of array is too large");
}

TEST_F(DeclarationsTest, VariableLengthArraysOnlyInBlocksAndPrototypes) {
    const ast::TranslationUnit* unit = analyze("void f(int n) { int a[n]; }");
    ASSERT_NE(unit, nullptr) << firstError();
    const auto* function = find<ast::FunctionDecl>(unit, "f");
    const auto* a = function->locals()[1];
    const auto* array = ast::cast<ast::ArrayType>(a->type().type());
    EXPECT_EQ(array->sizeKind(), ast::ArrayType::SizeKind::Variable);
    // The size expression is converted to an rvalue for the interpreter.
    EXPECT_EQ(withoutPositions(ast::dump(array->sizeExpression(), sources_)),
              "ImplicitCastExpr LValueToRValue 'int'\n  DeclRefExpr n (VarDecl at 1:12) 'int' lvalue\n");
    EXPECT_NE(analyze("void g(int n, int (*p)[n]);"), nullptr) << firstError();
    EXPECT_EQ(errorOf("int n; int a[n];"), "1:14: variably modified type at file scope");
    EXPECT_EQ(errorOf("void f(int n) { static int a[n]; }"), "1:28: storage size of 'a' isn't constant");
}

TEST_F(DeclarationsTest, ArraysAreCompletedByTheirInitializer) {
    EXPECT_EQ(typeOfDecl("int a[] = {1, 2, 3};", "a"), "int [3]");
    EXPECT_EQ(typeOfDecl("char s[] = \"abc\";", "s"), "char [4]");
    EXPECT_EQ(typeOfDecl("int a[] = {[7] = 1};", "a"), "int [8]");
    EXPECT_EQ(typeOfDecl("typedef int T[]; T a = {1, 2};", "a"), "int [2]");
}

TEST_F(DeclarationsTest, TypeofIsResolved) {
    const ast::TranslationUnit* unit = analyze("long x; typeof(x + 1) y; __typeof__(int *) z;");
    ASSERT_NE(unit, nullptr) << firstError();
    EXPECT_EQ(context().canonical(find<ast::VarDecl>(unit, "y")->type()), context().builtin(ast::BuiltinKind::Long));
    EXPECT_TRUE(isPointer(context(), find<ast::VarDecl>(unit, "z")->type()));
    EXPECT_EQ(errorOf("struct s { int b : 3; } v; typeof(v.b) w;"), "1:35: 'typeof' applied to a bit-field");
}

TEST_F(DeclarationsTest, AutoTypeHasTheTypeOfTheInitializerAfterDecay) {
    const ast::TranslationUnit* unit =
        analyze("void f(void) { int a[3]; const __auto_type p = a; __auto_type c = 'x'; }");
    ASSERT_NE(unit, nullptr) << firstError();
    const auto* function = find<ast::FunctionDecl>(unit, "f");
    EXPECT_EQ(ast::typeToString(context().canonical(function->locals()[1]->type())), "int *const");
    EXPECT_EQ(ast::typeToString(context().canonical(function->locals()[2]->type())), "int");
}

TEST_F(DeclarationsTest, TypeofParametersAreAdjusted) {
    const ast::TranslationUnit* unit = analyze("int arr[4]; int fn(int); void f(typeof(arr) p, typeof(fn) q) {}");
    ASSERT_NE(unit, nullptr) << firstError();
    const auto* f = find<ast::FunctionDecl>(unit, "f");
    EXPECT_EQ(ast::typeToString(f->type()), "void (int *, typeof(fn) *)");
    EXPECT_TRUE(isPointer(context(), f->parameters()[0]->type()));
    EXPECT_TRUE(isPointer(context(), f->parameters()[1]->type()));
    EXPECT_EQ(errorOf("int (*fp)(int); typeof(*fp) g;"), "1:29: a function declared through 'typeof' ('g') is not "
                                                         "supported");
}

TEST_F(DeclarationsTest, RedeclarationsGetTheCompositeType) {
    EXPECT_EQ(typeOfDecl("extern int a[]; int a[10];", "a"), "int [10]");
    EXPECT_EQ(typeOfDecl("int a[10]; extern int a[];", "a"), "int [10]");
    EXPECT_EQ(typeOfDecl("int f(); int f(int x) { return x; }", "f"), "int (int)");
    EXPECT_EQ(typeOfDecl("int f(int); int f();", "f"), "int (int)");
    EXPECT_EQ(typeOfDecl("typedef unsigned long size_t; size_t n; unsigned long n;", "n"), "unsigned long");
}

TEST_F(DeclarationsTest, ConflictingRedeclarationsAreErrors) {
    EXPECT_EQ(errorOf("int x; long x;"), "1:13: conflicting types for 'x'; have 'long', previously 'int'");
    EXPECT_EQ(errorOf("int f(void); int f(int);"), "1:18: conflicting types for 'f'; have 'int (int)', previously "
                                                   "'int (void)'");
    EXPECT_EQ(errorOf("int f(); int f(float x);"), "1:14: conflicting types for 'f'; have 'int (float)', previously "
                                                   "'int ()'");
    EXPECT_EQ(errorOf("int printf(); int printf(const char *, ...);").substr(0, 33),
              "1:19: conflicting types for 'prin");
    EXPECT_EQ(errorOf("int x = 1; int x = 2;"), "1:16: redefinition of 'x'");
}

TEST_F(DeclarationsTest, ConflictsWithImplicitDeclarationsAreWarnings) {
    const ast::TranslationUnit* unit = analyze("int main(void) { return foo(1); }\nvoid foo(int x) {}");
    ASSERT_NE(unit, nullptr) << firstError();
    EXPECT_TRUE(hasWarning("conflicting types for 'foo'"));
    EXPECT_EQ(ast::typeToString(find<ast::FunctionDecl>(unit, "foo")->type()), "void (int)");
}

TEST_F(DeclarationsTest, ImplicitDeclarationsOfFunctionsDeclaredInEndedBlocksKeepTheirType) {
    const ast::TranslationUnit* unit = analyze("void f(void) { extern char *g(int); }\n"
                                               "char *h(void) { return g(1); }");
    ASSERT_NE(unit, nullptr) << firstError();
    EXPECT_EQ(ast::typeToString(find<ast::FunctionDecl>(unit, "g")->type()), "char *(int)");
    EXPECT_FALSE(hasWarning("makes pointer from integer"));
}

TEST_F(DeclarationsTest, TentativeArrayDefinitionsGetOneElement) {
    const ast::TranslationUnit* unit = analyze("int a[];");
    ASSERT_NE(unit, nullptr) << firstError();
    EXPECT_EQ(ast::typeToString(find<ast::VarDecl>(unit, "a")->type()), "int [1]");
    EXPECT_TRUE(hasWarning("array 'a' assumed to have one element"));
    EXPECT_NE(analyze("extern int b[];"), nullptr) << firstError();
    EXPECT_FALSE(hasWarning("assumed to have one element"));
}

TEST_F(DeclarationsTest, IncompleteObjects) {
    EXPECT_NE(analyze("struct s x; struct s { int a; };"), nullptr) << firstError();
    EXPECT_EQ(errorOf("struct s x;"), "1:10: storage size of 'x' isn't known");
    EXPECT_EQ(errorOf("void f(void) { struct s x; }"), "1:25: storage size of 'x' isn't known");
    EXPECT_EQ(errorOf("void x;"), "1:6: variable 'x' declared void");
    EXPECT_NE(analyze("extern void x; void *p = &x;"), nullptr) << firstError();
}

TEST_F(DeclarationsTest, EnumerationValues) {
    const ast::TranslationUnit* unit = analyze("enum e { A, B = 5, C, D = A - 1, E };");
    ASSERT_NE(unit, nullptr) << firstError();
    EXPECT_EQ(find<ast::EnumConstantDecl>(unit, "A")->value(), 0);
    EXPECT_EQ(find<ast::EnumConstantDecl>(unit, "C")->value(), 6);
    EXPECT_EQ(find<ast::EnumConstantDecl>(unit, "D")->value(), -1);
    EXPECT_EQ(find<ast::EnumConstantDecl>(unit, "E")->value(), 0);
    EXPECT_EQ(errorOf("enum e { A = 2147483647, B };"), "1:26: overflow in enumeration values");
    EXPECT_EQ(errorOf("enum e { A = 1.0 };"), "1:14: enumerator value for 'A' is not an integer constant");
}

TEST_F(DeclarationsTest, EnumerationIntegerTypesAsInGcc) {
    const auto integerType = [&](std::string text, DataModel model = DataModel::LP64) {
        const ast::TranslationUnit* unit = analyze(std::move(text), model);
        EXPECT_NE(unit, nullptr) << firstError();
        return unit != nullptr ? ast::typeToString(find<ast::EnumDecl>(unit, "e")->integerType()) : "";
    };
    EXPECT_EQ(integerType("enum e { A, B };"), "unsigned int");
    EXPECT_EQ(integerType("enum e { A = -1, B };"), "int");
    EXPECT_EQ(integerType("enum e { A = 0x80000000 };"), "unsigned int");
    EXPECT_EQ(integerType("enum e { A = -1, B = 0x80000000 };"), "long");
    EXPECT_EQ(integerType("enum e { A = -1, B = 0x80000000 };", DataModel::ILP32), "long long");
    EXPECT_EQ(integerType("enum e { A = 0xffffffffffffffffULL };"), "unsigned long");
    EXPECT_EQ(integerType("enum __attribute__((packed)) e { A, B = 200 };"), "unsigned char");
    EXPECT_EQ(integerType("enum __attribute__((packed)) e { A = -1, B = 100 };"), "signed char");
    EXPECT_EQ(integerType("enum __attribute__((packed)) e { A = 300 };"), "unsigned short");
}

TEST_F(DeclarationsTest, EnumerationConstantsWiderThanIntHaveTheEnumeratedType) {
    const ast::TranslationUnit* unit = analyze("enum e { A = 1, B = 0x100000000 }; enum f { C = 1 };");
    ASSERT_NE(unit, nullptr) << firstError();
    EXPECT_EQ(ast::typeToString(find<ast::EnumConstantDecl>(unit, "A")->type()), "enum e");
    EXPECT_EQ(ast::typeToString(find<ast::EnumConstantDecl>(unit, "C")->type()), "int");
}

TEST_F(DeclarationsTest, EnumerationsInTypeNamesAreCompleted) {
    EXPECT_EQ(constantOf("", "sizeof(enum { X, Y }) + 0"), 4);
    const ast::TranslationUnit* unit = analyze("int f(void) { return (enum { P = 7, Q })0 + Q; }");
    ASSERT_NE(unit, nullptr) << firstError();
}

TEST_F(DeclarationsTest, BitFieldWidths) {
    const ast::TranslationUnit* unit = analyze("struct s { unsigned a : 3; int : 0; long long b : 40; };");
    ASSERT_NE(unit, nullptr) << firstError();
    const ast::RecordDecl* record = findRecord(unit, "s");
    EXPECT_EQ(record->fields()[0]->bitWidth(), 3u);
    EXPECT_EQ(record->fields()[1]->bitWidth(), 0u);
    EXPECT_EQ(record->fields()[2]->bitWidth(), 40u);
}

TEST_F(DeclarationsTest, StaticAssertions) {
    EXPECT_NE(analyze("_Static_assert(sizeof(int) == 4, \"int\");"), nullptr) << firstError();
    EXPECT_EQ(errorOf("_Static_assert(sizeof(long) == 4, \"long\");"), "1:1: static assertion failed: \"long\"");
    EXPECT_EQ(errorOf("void f(void) { _Static_assert(0); }"), "1:16: static assertion failed");
    EXPECT_EQ(errorOf("int n; _Static_assert(n, \"\");"), "1:23: static assertion expression is not an integer "
                                                          "constant");
}

TEST_F(DeclarationsTest, AlignedAttributesOnVariablesAndTypedefs) {
    const ast::TranslationUnit* unit = analyze("int x __attribute__((aligned(16))); _Alignas(8) char y;\n"
                                               "typedef int T __attribute__((aligned(32)));");
    ASSERT_NE(unit, nullptr) << firstError();
    EXPECT_EQ(find<ast::VarDecl>(unit, "x")->requestedAlignment(), 16u);
    EXPECT_EQ(find<ast::VarDecl>(unit, "y")->requestedAlignment(), 8u);
    EXPECT_EQ(find<ast::TypedefDecl>(unit, "T")->requestedAlignment(), 32u);
}

TEST_F(DeclarationsTest, AttributesAfterTheTagOfAReferenceApplyToTheDeclaration) {
    // GCC applies attributes in a struct specifier to the struct only in its definition.
    EXPECT_EQ(layoutText("struct s { char c; int i; }; typedef struct s __attribute__((packed)) t;", "s"),
              "size 8 align 4 c@0 i@4");
    EXPECT_EQ(layoutText("struct s { char c; int i; }; typedef struct __attribute__((packed)) s t;", "s"),
              "size 8 align 4 c@0 i@4");
    EXPECT_EQ(
        layoutText("struct in { int a; }; struct out { char c; struct in __attribute__((aligned(64))) m; };", "out"),
        "size 128 align 64 c@0 m@64");
    const ast::TranslationUnit* unit =
        analyze("struct s { int a; }; typedef struct s __attribute__((aligned(16))) t; t x;");
    ASSERT_NE(unit, nullptr) << firstError();
    EXPECT_EQ(alignOf(context(), find<ast::VarDecl>(unit, "x")->type()), 16u);
}

TEST_F(DeclarationsTest, ModeAttribute) {
    EXPECT_EQ(typeOfDecl("typedef int T __attribute__((mode(QI))); T x;", "x"), "T");
    const ast::TranslationUnit* unit = analyze("typedef unsigned int u8 __attribute__((__mode__(__QI__)));\n"
                                               "typedef int word __attribute__((mode(word)));\n"
                                               "int d __attribute__((mode(DI)));");
    ASSERT_NE(unit, nullptr) << firstError();
    EXPECT_EQ(ast::typeToString(find<ast::TypedefDecl>(unit, "u8")->underlying()), "unsigned char");
    EXPECT_EQ(ast::typeToString(find<ast::TypedefDecl>(unit, "word")->underlying()), "long");
    EXPECT_EQ(ast::typeToString(find<ast::VarDecl>(unit, "d")->type()), "long");
    EXPECT_EQ(errorOf("typedef float F __attribute__((mode(SF)));"), "1:32: the machine mode 'SF' is not supported");
}

TEST_F(DeclarationsTest, FunctionAttributes) {
    const ast::TranslationUnit* unit = analyze("void fail(void) __attribute__((noreturn)); void w(void) "
                                               "__attribute__((weak)); void w(void);");
    ASSERT_NE(unit, nullptr) << firstError();
    EXPECT_TRUE(find<ast::FunctionDecl>(unit, "fail")->isNoreturn());
    EXPECT_TRUE(find<ast::FunctionDecl>(unit, "w")->isWeak()
                || find<ast::FunctionDecl>(unit, "w")->previous()->isWeak());
}

TEST_F(DeclarationsTest, UnsupportedAttributesAreErrors) {
    EXPECT_EQ(errorOf("void f(int *p); void g(void) { int x __attribute__((cleanup(f))); }"),
              "1:53: the attribute 'cleanup' is not supported");
    EXPECT_EQ(errorOf("typedef int v4 __attribute__((vector_size(16)));"),
              "1:31: the attribute 'vector_size' is not supported");
    EXPECT_NE(analyze("int f(void) { return 0; } int g(void) __attribute__((alias(\"f\")));"), nullptr) << firstError();
}

TEST_F(DeclarationsTest, LocalsAreTheParametersAndAutomaticVariablesInOrder) {
    const ast::TranslationUnit* unit = analyze("int g;\n"
                                               "void f(int a, int b) {\n"
                                               "  int x; static int s; extern int g;\n"
                                               "  for (int i = 0; i < 2; ++i) { int y; }\n"
                                               "  { register int z; }\n"
                                               "  int m = ({ int t = 1; t; });\n"
                                               "}\n");
    ASSERT_NE(unit, nullptr) << firstError();
    const auto* f = find<ast::FunctionDecl>(unit, "f");
    std::string names;
    for (const ast::VarDecl* local : f->locals()) {
        names += std::format("{}={} ", local->name().text(), local->localIndex());
    }
    EXPECT_EQ(names, "a=0 b=1 x=2 i=3 y=4 z=5 m=6 t=7 ");
}

TEST_F(DeclarationsTest, KnrDefinitions) {
    const ast::TranslationUnit* unit = analyze("int f(a, b) char a; double b; { return a + b; }");
    ASSERT_NE(unit, nullptr) << firstError();
    const auto* f = find<ast::FunctionDecl>(unit, "f");
    EXPECT_EQ(ast::typeToString(f->type()), "int ()");
    EXPECT_EQ(f->locals().size(), 2u);
}

TEST_F(DeclarationsTest, BuiltinSignaturesForImplicitDeclarations) {
    const ast::TranslationUnit* unit = analyze("void f(void) { char *p = malloc(10); memset(p, 0, 10); "
                                               "__builtin_unreachable(); }");
    ASSERT_NE(unit, nullptr) << firstError();
    EXPECT_EQ(ast::typeToString(find<ast::FunctionDecl>(unit, "malloc")->type()), "void *(unsigned long)");
    EXPECT_EQ(ast::typeToString(find<ast::FunctionDecl>(unit, "memset")->type()), "void *(void *, int, unsigned long)");
    EXPECT_TRUE(find<ast::FunctionDecl>(unit, "__builtin_unreachable")->isNoreturn());
    EXPECT_TRUE(hasWarning("incompatible implicit declaration of built-in function 'malloc'"));
}

TEST_F(DeclarationsTest, LibrarySignaturesFollowTheDataModel) {
    const ast::TranslationUnit* unit = analyze("void f(void) { strlen(\"x\"); }", DataModel::ILP32);
    ASSERT_NE(unit, nullptr) << firstError();
    EXPECT_EQ(ast::typeToString(find<ast::FunctionDecl>(unit, "strlen")->type()), "unsigned int (const char *)");
}

TEST_F(DeclarationsTest, ExplicitDeclarationsKeepTheirType) {
    const ast::TranslationUnit* unit = analyze("int abs(); void f(void) { abs(1); }");
    ASSERT_NE(unit, nullptr) << firstError();
    EXPECT_EQ(ast::typeToString(find<ast::FunctionDecl>(unit, "abs")->type()), "int ()");
}

} // namespace
} // namespace nitwit::sema
