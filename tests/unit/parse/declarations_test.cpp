// Declarations: specifiers, declarators, types, records, enums, initializers, function definitions.

#include <string>

#include <gtest/gtest.h>

#include "ast/casting.hpp"
#include "ast/dump.hpp"
#include "parse_test_support.hpp"

namespace nitwit::parse {
namespace {

class DeclarationTest : public ParserTest {
protected:
    // The written type of the variable `x` declared by `declaration`.
    std::string typeOfX(const std::string& declaration) {
        const ast::TranslationUnit* unit = parse(declaration);
        EXPECT_NE(unit, nullptr) << firstError();
        if (unit == nullptr) {
            return "";
        }
        const auto* var = find<ast::ValueDecl>(unit, "x");
        return var != nullptr ? ast::typeToString(var->type()) : "";
    }
};

TEST_F(DeclarationTest, BuiltinTypeSpecifiersCombineInAnyOrder) {
    EXPECT_EQ(typeOfX("unsigned x;"), "unsigned int");
    EXPECT_EQ(typeOfX("long unsigned int long x;"), "unsigned long long");
    EXPECT_EQ(typeOfX("short x;"), "short");
    EXPECT_EQ(typeOfX("signed char x;"), "signed char");
    EXPECT_EQ(typeOfX("char unsigned x;"), "unsigned char");
    EXPECT_EQ(typeOfX("long double x;"), "long double");
    EXPECT_EQ(typeOfX("signed x;"), "int");
    EXPECT_EQ(typeOfX("_Bool x;"), "_Bool");
    EXPECT_EQ(typeOfX("unsigned __int128 x;"), "unsigned __int128");
    EXPECT_EQ(typeOfX("__int128_t x;"), "__int128_t");
    EXPECT_EQ(typeOfX("__builtin_va_list x;"), "__builtin_va_list");
}

TEST_F(DeclarationTest, ComplexTypes) {
    EXPECT_EQ(typeOfX("_Complex double x;"), "_Complex double");
    EXPECT_EQ(typeOfX("float _Complex x;"), "_Complex float");
    EXPECT_EQ(typeOfX("__complex__ x;"), "_Complex double");
}

TEST_F(DeclarationTest, FloatNTypesMapToStandardTypes) {
    EXPECT_EQ(typeOfX("_Float32 x;"), "float");
    EXPECT_EQ(typeOfX("_Float64 x;"), "double");
    EXPECT_EQ(typeOfX("_Float32x x;"), "double");
    EXPECT_EQ(typeOfX("_Float64x x;"), "long double");
}

TEST_F(DeclarationTest, Float128IsLongDoubleWithAWarning) {
    EXPECT_EQ(typeOfX("__float128 x;"), "long double");
    EXPECT_TRUE(hasWarning("'__float128' is approximated by 'long double'"));
}

TEST_F(DeclarationTest, QualifiersApplyToTheBaseType) {
    EXPECT_EQ(typeOfX("const volatile int x;"), "const volatile int");
    EXPECT_EQ(typeOfX("int const x;"), "const int");
    EXPECT_EQ(typeOfX("_Atomic int x;"), "_Atomic int");
    EXPECT_EQ(typeOfX("_Atomic(long) x;"), "_Atomic long");
}

TEST_F(DeclarationTest, PointerDeclarators) {
    EXPECT_EQ(typeOfX("char *x;"), "char *");
    EXPECT_EQ(typeOfX("const char *x;"), "const char *");
    EXPECT_EQ(typeOfX("char *const x;"), "char *const");
    EXPECT_EQ(typeOfX("int *const *volatile x;"), "int *const *volatile");
    EXPECT_EQ(typeOfX("int *restrict x;"), "int *restrict");
}

TEST_F(DeclarationTest, ArrayDeclarators) {
    EXPECT_EQ(typeOfX("int x[3];"), "int [3]");
    EXPECT_EQ(typeOfX("int x[2][3];"), "int [2][3]");
    EXPECT_EQ(typeOfX("extern int x[];"), "int []");
    EXPECT_EQ(typeOfX("int *x[3];"), "int *[3]");
    EXPECT_EQ(typeOfX("int (*x)[3];"), "int (*)[3]");
    EXPECT_EQ(typeOfX("enum { N = 4 }; int x[N + 1];"), "int [N + 1]");
}

TEST_F(DeclarationTest, ArrayTypesAsWrittenKeepTheirSizeExpression) {
    const ast::TranslationUnit* unit = parse("int x[2 * 3];");
    ASSERT_NE(unit, nullptr);
    const auto* array = ast::cast<ast::ArrayType>(find<ast::VarDecl>(unit, "x")->type().type());
    EXPECT_EQ(array->sizeKind(), ast::ArrayType::SizeKind::Unresolved);
    ASSERT_NE(array->sizeExpression(), nullptr);
    EXPECT_TRUE(ast::isa<ast::BinaryExpr>(array->sizeExpression()));
}

TEST_F(DeclarationTest, FunctionDeclarators) {
    EXPECT_EQ(typeOfX("int x(void);"), "int (void)");
    EXPECT_EQ(typeOfX("int x();"), "int ()");
    EXPECT_EQ(typeOfX("int x(int, char *, ...);"), "int (int, char *, ...)");
    EXPECT_EQ(typeOfX("int (*x)(int);"), "int (*)(int)");
    EXPECT_EQ(typeOfX("int *x(int);"), "int *(int)");
    EXPECT_EQ(typeOfX("void (*x[4])(void);"), "void (*[4])(void)");
    EXPECT_EQ(typeOfX("int (*(*x)(int))(char);"), "int (*(*)(int))(char)");
}

TEST_F(DeclarationTest, FunctionReturningAFunctionPointer) {
    const ast::TranslationUnit* unit = parse("int (*x(long a))(char);");
    ASSERT_NE(unit, nullptr);
    const auto* function = find<ast::FunctionDecl>(unit, "x");
    EXPECT_EQ(ast::typeToString(function->type()), "int (*(long))(char)");
    ASSERT_EQ(function->parameters().size(), 1u);
    EXPECT_EQ(function->parameters()[0]->name().text(), "a");
}

TEST_F(DeclarationTest, ParenthesizedDeclaratorNamesTheEntity) {
    EXPECT_EQ(typeOfX("int ((x));"), "int");
    EXPECT_EQ(typeOfX("int (x)[2];"), "int [2]");
}

TEST_F(DeclarationTest, ArrayAndFunctionParametersAreAdjustedToPointers) {
    const ast::TranslationUnit* unit = parse("void x(int a[3], int b[const static 4], int c[*], int d(void));");
    ASSERT_NE(unit, nullptr);
    const auto* function = find<ast::FunctionDecl>(unit, "x");
    EXPECT_EQ(ast::typeToString(function->type()), "void (int *, int *const, int *, int (*)(void))");
    ASSERT_EQ(function->parameters().size(), 4u);
    EXPECT_EQ(ast::typeToString(function->parameters()[1]->type()), "int *const");
    EXPECT_TRUE(function->parameters()[1]->isParameter());
}

TEST_F(DeclarationTest, TypedefArrayParameterIsAdjustedWithItsQualifiers) {
    const ast::TranslationUnit* unit = parse("typedef int A[3]; void x(const A a);");
    ASSERT_NE(unit, nullptr);
    EXPECT_EQ(ast::typeToString(find<ast::FunctionDecl>(unit, "x")->type()), "void (const int *)");
}

TEST_F(DeclarationTest, VoidParameterListMeansNoParameters) {
    const ast::TranslationUnit* unit = parse("typedef void V; int f(void); int g(V);");
    ASSERT_NE(unit, nullptr);
    EXPECT_TRUE(find<ast::FunctionDecl>(unit, "f")->parameters().empty());
    EXPECT_EQ(ast::typeToString(find<ast::FunctionDecl>(unit, "g")->type()), "int (void)");
}

TEST_F(DeclarationTest, UnnamedParametersAreLocatedAtTheirSpecifiers) {
    const ast::TranslationUnit* unit = parse("int f(int, char *);");
    ASSERT_NE(unit, nullptr);
    const auto* function = find<ast::FunctionDecl>(unit, "f");
    ASSERT_EQ(function->parameters().size(), 2u);
    EXPECT_TRUE(function->parameters()[1]->name().empty());
    EXPECT_EQ(position(function->parameters()[1]->location()), "1:12");
}

TEST_F(DeclarationTest, AbstractDeclaratorsInTypeNames) {
    EXPECT_EQ(dumpExpr("", "sizeof(int (*)[3])"), "SizeOfAlignOfExpr sizeof (int (*)[3])\n");
    EXPECT_EQ(dumpExpr("", "sizeof(int *(*)(void))"), "SizeOfAlignOfExpr sizeof (int *(*)(void))\n");
    EXPECT_EQ(dumpExpr("", "sizeof(char (*[2])(int))"), "SizeOfAlignOfExpr sizeof (char (*[2])(int))\n");
}

TEST_F(DeclarationTest, StorageClassesAndFunctionSpecifiers) {
    EXPECT_EQ(dumpOf("static int a; extern int b; _Thread_local int c; int f(register int r);\n"
                     "static inline _Noreturn void g(void);"),
              "VarDecl a 'int' static\n"
              "VarDecl b 'int' extern\n"
              "VarDecl c 'int' thread_local\n"
              "FunctionDecl f 'int (int)'\n"
              "  VarDecl r 'int' register parameter\n"
              "FunctionDecl g 'void (void)' static inline noreturn\n");
}

TEST_F(DeclarationTest, SeveralDeclaratorsShareTheSpecifiers) {
    EXPECT_EQ(dumpOf("const int a = 1, *b, c[2];"), "VarDecl a 'const int'\n"
                                                    "  IntegerLiteral 1\n"
                                                    "VarDecl b 'const int *'\n"
                                                    "VarDecl c 'const int [2]'\n");
}

TEST_F(DeclarationTest, TypedefDeclaresATypeName) {
    EXPECT_EQ(dumpOf("typedef unsigned long size_t; typedef size_t *P; P p;"), "TypedefDecl size_t 'unsigned long'\n"
                                                                               "TypedefDecl P 'size_t *'\n"
                                                                               "VarDecl p 'P'\n");
}

TEST_F(DeclarationTest, TypedefNameIsNotATypeSpecifierAfterAnother) {
    // `unsigned T` declares a variable T, since a typedef name is a type specifier only if there is no other one.
    const ast::TranslationUnit* unit = parse("typedef int T; void f(void) { unsigned T; T = 1; }");
    ASSERT_NE(unit, nullptr) << firstError();
    const auto* declaration = ast::cast<ast::DeclarationStmt>(bodyOf(unit, "f")[0]);
    EXPECT_EQ(ast::typeToString(ast::cast<ast::VarDecl>(declaration->decls()[0])->type()), "unsigned int");
}

TEST_F(DeclarationTest, StructDefinitionWithMembers) {
    EXPECT_EQ(dumpOf("struct S { int a, *b; char c[4]; unsigned d : 3, : 0; };"), "RecordDecl struct S\n"
                                                                                  "  FieldDecl a 'int'\n"
                                                                                  "  FieldDecl b 'int *'\n"
                                                                                  "  FieldDecl c 'char [4]'\n"
                                                                                  "  FieldDecl d 'unsigned int'\n"
                                                                                  "    IntegerLiteral 3\n"
                                                                                  "  FieldDecl 'unsigned int'\n"
                                                                                  "    IntegerLiteral 0\n");
}

TEST_F(DeclarationTest, FieldsKnowTheirRecordAndIndex) {
    const ast::TranslationUnit* unit = parse("union U { int a; float b; };");
    ASSERT_NE(unit, nullptr);
    const auto* record = find<ast::RecordDecl>(unit, "U");
    EXPECT_TRUE(record->isUnion());
    EXPECT_TRUE(record->isComplete());
    ASSERT_EQ(record->fields().size(), 2u);
    EXPECT_EQ(record->fields()[1]->parent(), record);
    EXPECT_EQ(record->fields()[1]->index(), 1u);
}

TEST_F(DeclarationTest, NestedRecordDefinitionsPrecedeTheEnclosingDeclaration) {
    EXPECT_EQ(dumpOf("struct S { struct T { int x; } t; enum E { A } e; } s;"), "RecordDecl struct T\n"
                                                                                "  FieldDecl x 'int'\n"
                                                                                "EnumDecl enum E\n"
                                                                                "  EnumConstantDecl A\n"
                                                                                "RecordDecl struct S\n"
                                                                                "  FieldDecl t 'struct T'\n"
                                                                                "  FieldDecl e 'enum E'\n"
                                                                                "VarDecl s 'struct S'\n");
}

TEST_F(DeclarationTest, AnonymousMembers) {
    const ast::TranslationUnit* unit = parse("struct S { union { int a; float b; }; struct { int c; } named; };");
    ASSERT_NE(unit, nullptr);
    const auto* record = find<ast::RecordDecl>(unit, "S");
    ASSERT_EQ(record->fields().size(), 2u);
    EXPECT_TRUE(record->fields()[0]->isAnonymousRecord());
    EXPECT_EQ(ast::typeToString(record->fields()[0]->type()), "union <anonymous>");
    EXPECT_FALSE(record->fields()[1]->isAnonymousRecord());
}

TEST_F(DeclarationTest, TaggedStructWithoutDeclaratorDeclaresNoMember) {
    const ast::TranslationUnit* unit = parse("struct A { int x; }; struct B { struct A; int y; };");
    ASSERT_NE(unit, nullptr);
    EXPECT_EQ(find<ast::RecordDecl>(unit, "B")->fields().size(), 1u);
    EXPECT_TRUE(hasWarning("declaration does not declare anything"));
}

TEST_F(DeclarationTest, EmptyStructsAndZeroLengthArrays) {
    EXPECT_EQ(dumpOf("struct E {}; struct F { int n; int data[0]; int rest[]; };"), "RecordDecl struct E\n"
                                                                                    "RecordDecl struct F\n"
                                                                                    "  FieldDecl n 'int'\n"
                                                                                    "  FieldDecl data 'int [0]'\n"
                                                                                    "  FieldDecl rest 'int []'\n");
}

TEST_F(DeclarationTest, MissingLastSemicolonInStructIsAccepted) {
    EXPECT_NE(parse("struct S { int a; int b };"), nullptr) << firstError();
}

TEST_F(DeclarationTest, EnumConstantsWithAndWithoutValues) {
    EXPECT_EQ(dumpOf("enum E { A, B = 5, C = B + 1, };"), "EnumDecl enum E\n"
                                                          "  EnumConstantDecl A\n"
                                                          "  EnumConstantDecl B\n"
                                                          "    IntegerLiteral 5\n"
                                                          "  EnumConstantDecl C\n"
                                                          "    BinaryExpr +\n"
                                                          "      DeclRefExpr B (EnumConstantDecl at 1:13)\n"
                                                          "      IntegerLiteral 1\n");
}

TEST_F(DeclarationTest, InitializersWithDesignators) {
    EXPECT_EQ(dumpOf("struct P { int x, y; } p = { .y = 2, 1 }; int a[4] = { [1] = 1, [2 ... 3] = 2 };"),
              "RecordDecl struct P\n"
              "  FieldDecl x 'int'\n"
              "  FieldDecl y 'int'\n"
              "VarDecl p 'struct P'\n"
              "  InitListExpr\n"
              "    DesignatedInitExpr .y\n"
              "      IntegerLiteral 2\n"
              "    IntegerLiteral 1\n"
              "VarDecl a 'int [4]'\n"
              "  InitListExpr\n"
              "    DesignatedInitExpr [1]\n"
              "      IntegerLiteral 1\n"
              "    DesignatedInitExpr [2 ... 3]\n"
              "      IntegerLiteral 2\n");
}

TEST_F(DeclarationTest, NestedDesignatorsAndBraces) {
    EXPECT_EQ(dumpOf("struct Q { int a[2]; } q[2] = { [1].a[0] = 5, { { 1, 2 } } };"),
              "RecordDecl struct Q\n"
              "  FieldDecl a 'int [2]'\n"
              "VarDecl q 'struct Q [2]'\n"
              "  InitListExpr\n"
              "    DesignatedInitExpr [1].a[0]\n"
              "      IntegerLiteral 5\n"
              "    InitListExpr\n"
              "      InitListExpr\n"
              "        IntegerLiteral 1\n"
              "        IntegerLiteral 2\n");
}

TEST_F(DeclarationTest, EmptyInitializerList) {
    EXPECT_EQ(dumpOf("int a[2] = {};"), "VarDecl a 'int [2]'\n  InitListExpr\n");
}

TEST_F(DeclarationTest, DesignatorLocations) {
    const ast::TranslationUnit* unit = parse("struct P { int x; } p = { .x = 1 };");
    ASSERT_NE(unit, nullptr);
    const auto* list = ast::cast<ast::InitListExpr>(find<ast::VarDecl>(unit, "p")->initializer());
    const auto* designated = ast::cast<ast::DesignatedInitExpr>(list->elements()[0]);
    EXPECT_EQ(position(designated->designators()[0].location), "1:27");
    EXPECT_EQ(designated->designators()[0].fieldName.text(), "x");
}

TEST_F(DeclarationTest, StaticAssert) {
    EXPECT_EQ(dumpOf("_Static_assert(sizeof(int) == 4, \"int\"); void f(void) { _Static_assert(1, \"\"); }"),
              "StaticAssertDecl\n"
              "  BinaryExpr ==\n"
              "    SizeOfAlignOfExpr sizeof (int)\n"
              "    IntegerLiteral 4\n"
              "  StringLiteral \"int\"\n"
              "FunctionDecl f 'void (void)'\n"
              "  CompoundStmt\n"
              "    DeclarationStmt\n"
              "      StaticAssertDecl\n"
              "        IntegerLiteral 1\n"
              "        StringLiteral \"\"\n");
}

TEST_F(DeclarationTest, AlignasBecomesAnAlignedAttribute) {
    EXPECT_EQ(dumpOf("_Alignas(8) char a; _Alignas(long) char b;"), "VarDecl a 'char'\n"
                                                                    "  Attribute aligned\n"
                                                                    "    IntegerLiteral 8\n"
                                                                    "VarDecl b 'char'\n"
                                                                    "  Attribute aligned\n"
                                                                    "    SizeOfAlignOfExpr _Alignof (long)\n");
}

TEST_F(DeclarationTest, FunctionDefinitionHasParametersAndBody) {
    EXPECT_EQ(dumpOf("int add(int a, int b) { return a + b; }"), "FunctionDecl add 'int (int, int)'\n"
                                                                 "  VarDecl a 'int' parameter\n"
                                                                 "  VarDecl b 'int' parameter\n"
                                                                 "  CompoundStmt\n"
                                                                 "    ReturnStmt\n"
                                                                 "      BinaryExpr +\n"
                                                                 "        DeclRefExpr a (VarDecl at 1:13)\n"
                                                                 "        DeclRefExpr b (VarDecl at 1:20)\n");
}

TEST_F(DeclarationTest, KnrDefinition) {
    EXPECT_EQ(dumpOf("int f(a, b, c) char *b; int a; { return a; }"), "FunctionDecl f 'int ()'\n"
                                                                      "  VarDecl a 'int' parameter\n"
                                                                      "  VarDecl b 'char *' parameter\n"
                                                                      "  VarDecl c 'int' parameter\n"
                                                                      "  CompoundStmt\n"
                                                                      "    ReturnStmt\n"
                                                                      "      DeclRefExpr a (VarDecl at 1:29)\n");
    EXPECT_TRUE(hasWarning("type of 'c' defaults to 'int'"));
}

TEST_F(DeclarationTest, ImplicitIntDeclarations) {
    EXPECT_EQ(dumpOf("static x; const y = 1; main() { return 0; }"), "VarDecl x 'int' static\n"
                                                                     "VarDecl y 'const int'\n"
                                                                     "  IntegerLiteral 1\n"
                                                                     "FunctionDecl main 'int ()'\n"
                                                                     "  CompoundStmt\n"
                                                                     "    ReturnStmt\n"
                                                                     "      IntegerLiteral 0\n");
    EXPECT_TRUE(hasWarning("type defaults to 'int'"));
}

TEST_F(DeclarationTest, WarningsForPreC99Code) {
    ASSERT_NE(parse("static x;\nmain() { return f(x); }\nint g(a) { return a; }"), nullptr) << firstError();
    EXPECT_EQ(messages(), (std::vector<std::string>{"test.c:1:1: warning: type defaults to 'int'",
                                                    "test.c:2:1: warning: type defaults to 'int'",
                                                    "test.c:2:17: warning: implicit declaration of function 'f'",
                                                    "test.c:3:7: warning: type of 'a' defaults to 'int'"}));
}

TEST_F(DeclarationTest, FunctionDeclaredThroughATypedef) {
    const ast::TranslationUnit* unit = parse("typedef int F(int); F f;");
    ASSERT_NE(unit, nullptr);
    EXPECT_EQ(ast::typeToString(find<ast::FunctionDecl>(unit, "f")->type()), "F");
}

TEST_F(DeclarationTest, FunctionDeclaredThroughTypeofOfAFunction) {
    const ast::TranslationUnit* unit = parse("int f(int); extern typeof(f) f;");
    ASSERT_NE(unit, nullptr) << firstError();
    const auto* function = find<ast::FunctionDecl>(unit, "f");
    EXPECT_EQ(ast::typeToString(function->type()), "typeof(f)");
    EXPECT_NE(function->previous(), nullptr);
}

TEST_F(DeclarationTest, AutoTypeTakesTheTypeOfItsInitializer) {
    const ast::TranslationUnit* unit = parse("void f(void) { const __auto_type x = 1.5; }");
    ASSERT_NE(unit, nullptr) << firstError();
    const auto* declaration = ast::cast<ast::DeclarationStmt>(bodyOf(unit, "f")[0]);
    const auto* var = ast::cast<ast::VarDecl>(declaration->decls()[0]);
    EXPECT_EQ(ast::typeToString(var->type()), "const __auto_type");
    const auto* typeOf = ast::cast<ast::TypeofType>(var->type().type());
    EXPECT_TRUE(typeOf->isAutoType());
    EXPECT_EQ(typeOf->expression(), var->initializer());
}

TEST_F(DeclarationTest, TypeofOfAnExpressionAndOfAType) {
    EXPECT_EQ(dumpOf("int a; typeof(a + 1) b; __typeof__(char *) c;"), "VarDecl a 'int'\n"
                                                                       "VarDecl b 'typeof(a + 1)'\n"
                                                                       "VarDecl c 'char *'\n");
}

TEST_F(DeclarationTest, AsmLabelsAreIgnored) {
    EXPECT_EQ(dumpOf("int f(void) __asm__(\"g\"); extern int v asm(\"w\") = 0;"), "FunctionDecl f 'int (void)'\n"
                                                                                  "VarDecl v 'int' extern\n"
                                                                                  "  IntegerLiteral 0\n");
}

TEST_F(DeclarationTest, FileScopeAsmAndEmptyDeclarationsAreSkipped) {
    EXPECT_EQ(dumpOf("__asm__(\".symver a,b\"); ; int x;"), "VarDecl x 'int'\n");
}

TEST_F(DeclarationTest, ExtensionKeywordBeforeDeclarations) {
    EXPECT_EQ(dumpOf("__extension__ typedef long long ll; struct S { __extension__ long long a; };"),
              "TypedefDecl ll 'long long'\n"
              "RecordDecl struct S\n"
              "  FieldDecl a 'long long'\n");
}

} // namespace
} // namespace nitwit::parse
