// Expressions (C17 6.5): types and value categories, operand conversions, member access, calls and the GNU forms.

#include <gtest/gtest.h>

#include "sema_test_support.hpp"

namespace nitwit::sema {
namespace {

using ExpressionsTest = SemaTest;

TEST_F(ExpressionsTest, IntegerLiteralTypes) {
    EXPECT_EQ(typeOf("", "2147483647"), "int");
    EXPECT_EQ(typeOf("", "2147483648"), "long");
    EXPECT_EQ(typeOf("", "2147483648", DataModel::ILP32), "long long");
    EXPECT_EQ(typeOf("", "0x80000000"), "unsigned int");
    EXPECT_EQ(typeOf("", "0x100000000"), "long");
    EXPECT_EQ(typeOf("", "0x100000000", DataModel::ILP32), "long long");
    EXPECT_EQ(typeOf("", "4294967295u"), "unsigned int");
    EXPECT_EQ(typeOf("", "4294967296u", DataModel::ILP32), "unsigned long long");
    EXPECT_EQ(typeOf("", "1l"), "long");
    EXPECT_EQ(typeOf("", "1ull"), "unsigned long long");
    EXPECT_EQ(typeOf("", "0xffffffffffffffff"), "unsigned long");
    EXPECT_EQ(typeOf("", "9223372036854775808"), "unsigned long");
    EXPECT_TRUE(hasWarning("integer constant is so large that it is unsigned"));
    EXPECT_EQ(typeOf("", "9223372036854775808", DataModel::ILP32), "unsigned long long");
}

TEST_F(ExpressionsTest, OtherLiteralTypes) {
    EXPECT_EQ(typeOf("", "'a'"), "int");
    EXPECT_EQ(typeOf("", "L'a'"), "int");
    EXPECT_EQ(typeOf("", "u'a'"), "unsigned short");
    EXPECT_EQ(typeOf("", "U'a'"), "unsigned int");
    EXPECT_EQ(typeOf("", "1.0f"), "float");
    EXPECT_EQ(typeOf("", "1.0"), "double");
    EXPECT_EQ(typeOf("", "1.0L"), "long double");
    EXPECT_EQ(typeOf("", "\"abc\""), "char [4] lvalue");
    EXPECT_EQ(typeOf("", "L\"ab\""), "int [3] lvalue");
    EXPECT_EQ(typeOf("", "u\"ab\""), "unsigned short [3] lvalue");
}

TEST_F(ExpressionsTest, LvaluesAndRvalues) {
    EXPECT_EQ(typeOf("int x;", "x"), "int lvalue");
    EXPECT_EQ(typeOf("int x;", "(x)"), "int lvalue");
    EXPECT_EQ(typeOf("int x;", "x + 1"), "int");
    EXPECT_EQ(typeOf("int *p;", "*p"), "int lvalue");
    EXPECT_EQ(typeOf("int a[3];", "a[1]"), "int lvalue");
    EXPECT_EQ(typeOf("enum { A };", "A"), "int");
    EXPECT_EQ(typeOf("int f(void);", "f"), "int (void) lvalue");
    EXPECT_EQ(typeOf("int x;", "x = 1"), "int");
    EXPECT_EQ(typeOf("int x;", "(0, x)"), "int");
    EXPECT_EQ(typeOf("struct s { int a; }; struct s f(void);", "f().a"), "int");
    EXPECT_EQ(typeOf("struct s { int a; } v;", "v.a"), "int lvalue");
    EXPECT_EQ(typeOf("int x;", "__extension__ x"), "int lvalue");
}

TEST_F(ExpressionsTest, LvalueConversionDropsQualifiers) {
    EXPECT_EQ(dumpExpr("const volatile int x; int y;", "y = x"),
              "BinaryExpr = 'int'\n"
              "  DeclRefExpr y (VarDecl at 1:27) 'int' lvalue\n"
              "  ImplicitCastExpr LValueToRValue 'int'\n"
              "    DeclRefExpr x (VarDecl at 1:20) 'const volatile int' lvalue\n");
}

TEST_F(ExpressionsTest, ArraysAndFunctionsDecay) {
    EXPECT_EQ(dumpExpr("const int a[3]; const int *p;", "p = a"),
              "BinaryExpr = 'const int *'\n"
              "  DeclRefExpr p (VarDecl at 1:28) 'const int *' lvalue\n"
              "  ImplicitCastExpr ArrayToPointerDecay 'const int *'\n"
              "    DeclRefExpr a (VarDecl at 1:11) 'const int [3]' lvalue\n");
    EXPECT_EQ(dumpExpr("int f(int); int (*fp)(int);", "fp = f"),
              "BinaryExpr = 'int (*)(int)'\n"
              "  DeclRefExpr fp (VarDecl at 1:19) 'int (*)(int)' lvalue\n"
              "  ImplicitCastExpr FunctionToPointerDecay 'int (*)(int)'\n"
              "    DeclRefExpr f (FunctionDecl at 1:5) 'int (int)' lvalue\n");
}

TEST_F(ExpressionsTest, IntegerPromotionsAndUsualArithmeticConversions) {
    EXPECT_EQ(typeOf("char c; short s;", "c + s"), "int");
    EXPECT_EQ(typeOf("unsigned char c;", "-c"), "int");
    EXPECT_EQ(typeOf("unsigned u; int i;", "u + i"), "unsigned int");
    EXPECT_EQ(typeOf("long l; unsigned u;", "l + u"), "long");
    EXPECT_EQ(typeOf("long l; unsigned u;", "l + u", DataModel::ILP32), "unsigned long");
    EXPECT_EQ(typeOf("long long l; unsigned long u;", "l + u"), "unsigned long long");
    EXPECT_EQ(typeOf("int i; float f;", "i * f"), "float");
    EXPECT_EQ(typeOf("float f; double d;", "f - d"), "double");
    EXPECT_EQ(typeOf("long double ld; long long ll;", "ld / ll"), "long double");
    EXPECT_EQ(typeOf("_Bool b;", "b + b"), "int");
    EXPECT_EQ(typeOf("enum e { A } x;", "x + 1"), "unsigned int");
    EXPECT_EQ(typeOf("enum e { A = -1 } x;", "x + 1u"), "unsigned int");
}

TEST_F(ExpressionsTest, BitFieldsPromoteByTheirWidth) {
    const std::string_view s = "struct s { unsigned a : 3; unsigned b : 32; unsigned long long c : 20; "
                               "unsigned long long d : 32; long long e : 40; } v;";
    EXPECT_EQ(typeOf(s, "v.a + 0u"), "unsigned int");
    EXPECT_EQ(typeOf(s, "v.a - 1"), "int");
    EXPECT_EQ(typeOf(s, "v.b - 1"), "unsigned int");
    EXPECT_EQ(typeOf(s, "v.c + 0"), "int");
    EXPECT_EQ(typeOf(s, "v.d + 0"), "unsigned int");
    EXPECT_EQ(typeOf(s, "v.e + 0"), "long long");
    EXPECT_EQ(typeOf(s, "(v.a = 1) - 2"), "int");
    // Also through a statement expression, whose value has the bit-field's type in GCC.
    EXPECT_EQ(typeOf(s, "({ v.a; })"), "unsigned int");
    EXPECT_EQ(typeOf(s, "({ v.a; }) - 1"), "int");
    EXPECT_EQ(typeOf(s, "({ int i = 0; v.b; }) - 1"), "unsigned int");
}

TEST_F(ExpressionsTest, ShiftsPromoteTheirOperandsSeparately) {
    EXPECT_EQ(typeOf("char c; long l;", "c << l"), "int");
    EXPECT_EQ(typeOf("unsigned short u;", "u >> 1"), "int");
}

TEST_F(ExpressionsTest, PointerArithmetic) {
    EXPECT_EQ(typeOf("int *p;", "p + 1"), "int *");
    EXPECT_EQ(typeOf("int *p;", "2 + p"), "int *");
    EXPECT_EQ(typeOf("int *p, *q;", "p - q"), "long");
    EXPECT_EQ(typeOf("int *p, *q;", "p - q", DataModel::ILP32), "int");
    EXPECT_EQ(typeOf("void *p;", "p + 1"), "void *");
    EXPECT_EQ(typeOf("int a[4];", "a + 1"), "int *");
    EXPECT_EQ(dumpExpr("int *p; char c;", "p + c"), "BinaryExpr + 'int *'\n"
                                                    "  ImplicitCastExpr LValueToRValue 'int *'\n"
                                                    "    DeclRefExpr p (VarDecl at 1:6) 'int *' lvalue\n"
                                                    "  ImplicitCastExpr IntegralCast 'int'\n"
                                                    "    ImplicitCastExpr LValueToRValue 'char'\n"
                                                    "      DeclRefExpr c (VarDecl at 1:14) 'char' lvalue\n");
    EXPECT_EQ(errorOf("struct s; struct s *p; void f(void) { p + 1; }"),
              "1:41: arithmetic on pointer to an incomplete type 'struct s'");
    EXPECT_EQ(errorOf("int *p; char *q; void f(void) { p - q; }"),
              "1:35: invalid operands to binary - (have 'int *' and 'char *')");
}

TEST_F(ExpressionsTest, SubscriptsAreNormalizedToPointerAndIndex) {
    EXPECT_EQ(dumpExpr("int a[4];", "2[a]"), "ArraySubscriptExpr 'int' lvalue\n"
                                             "  ImplicitCastExpr ArrayToPointerDecay 'int *'\n"
                                             "    DeclRefExpr a (VarDecl at 1:5) 'int [4]' lvalue\n"
                                             "  IntegerLiteral 2 'int'\n");
    EXPECT_EQ(errorOf("int x; void f(void) { x[1]; }"), "1:23: subscripted value is neither array nor pointer");
}

TEST_F(ExpressionsTest, Comparisons) {
    EXPECT_EQ(typeOf("double d; long l;", "d < l"), "int");
    EXPECT_EQ(typeOf("int *p;", "p == 0"), "int");
    EXPECT_EQ(dumpExpr("int *p;", "p != 0"), "BinaryExpr != 'int'\n"
                                             "  ImplicitCastExpr LValueToRValue 'int *'\n"
                                             "    DeclRefExpr p (VarDecl at 1:6) 'int *' lvalue\n"
                                             "  ImplicitCastExpr NullToPointer 'int *'\n"
                                             "    IntegerLiteral 0 'int'\n");
    EXPECT_EQ(typeOf("int *p; long l;", "p == l"), "int");
    EXPECT_TRUE(hasWarning("comparison between pointer and integer"));
    EXPECT_EQ(typeOf("int *p; char *q;", "p == q"), "int");
    EXPECT_TRUE(hasWarning("comparison of distinct pointer types lacks a cast"));
    EXPECT_EQ(typeOf("int *p; void *q;", "p < q"), "int");
    EXPECT_TRUE(warnings().empty());
}

TEST_F(ExpressionsTest, LogicalOperatorsTakeScalarsAndGiveInt) {
    EXPECT_EQ(typeOf("double d; int *p;", "d && p"), "int");
    EXPECT_EQ(typeOf("double d;", "!d"), "int");
    EXPECT_EQ(errorOf("struct s { int a; } v; void f(void) { !v; }"),
              "1:39: wrong type argument to unary exclamation mark");
}

TEST_F(ExpressionsTest, UnaryOperators) {
    EXPECT_EQ(typeOf("char c;", "~c"), "int");
    EXPECT_EQ(typeOf("float f;", "-f"), "float");
    EXPECT_EQ(typeOf("int x;", "&x"), "int *");
    EXPECT_EQ(typeOf("int a[3];", "&a"), "int (*)[3]");
    EXPECT_EQ(typeOf("int f(void);", "&f"), "int (*)(void)");
    EXPECT_EQ(typeOf("int *p;", "&*p"), "int *");
    EXPECT_EQ(typeOf("char c;", "c++"), "char");
    EXPECT_EQ(typeOf("int *p;", "--p"), "int *");
    EXPECT_EQ(errorOf("void f(void) { &1; }"), "1:16: lvalue required as unary '&' operand");
    EXPECT_EQ(errorOf("struct s { int b : 1; } v; void f(void) { &v.b; }"),
              "1:43: cannot take address of bit-field 'b'");
    EXPECT_EQ(errorOf("void f(void) { register int r; &r; }"), "1:32: address of register variable 'r' requested");
    EXPECT_EQ(errorOf("int x; void f(void) { *x; }"), "1:23: invalid type argument of unary '*' (have 'int')");
    EXPECT_EQ(errorOf("double d; void f(void) { ~d; }"), "1:26: wrong type argument to unary complement");
}

TEST_F(ExpressionsTest, DereferencingVoidPointersWarns) {
    EXPECT_EQ(typeOf("void *p;", "*p"), "void");
    EXPECT_TRUE(hasWarning("dereferencing 'void *' pointer"));
}

TEST_F(ExpressionsTest, AssignmentsConvertToTheLeftType) {
    EXPECT_EQ(dumpExpr("char c; double d;", "c = d"), "BinaryExpr = 'char'\n"
                                                      "  DeclRefExpr c (VarDecl at 1:6) 'char' lvalue\n"
                                                      "  ImplicitCastExpr FloatingToIntegral 'char'\n"
                                                      "    ImplicitCastExpr LValueToRValue 'double'\n"
                                                      "      DeclRefExpr d (VarDecl at 1:16) 'double' lvalue\n");
    EXPECT_EQ(errorOf("const int x; void f(void) { x = 1; }"), "1:31: assignment of read-only variable 'x'");
    EXPECT_EQ(errorOf("struct s { const int a; } v, w; void f(void) { v = w; }"),
              "1:50: assignment of read-only variable 'v'");
    EXPECT_EQ(errorOf("struct s { const int a; } v; void f(void) { v.a = 1; }"),
              "1:49: assignment of read-only member 'a'");
    EXPECT_EQ(errorOf("const int *p; void f(void) { *p = 1; }"), "1:33: assignment of read-only location");
    EXPECT_EQ(errorOf("int a[2], b[2]; void f(void) { a = b; }"), "1:34: assignment to expression with array type");
    EXPECT_EQ(errorOf("void f(void) { 1 = 2; }"), "1:18: lvalue required as left operand of assignment");
}

TEST_F(ExpressionsTest, IncrementsNameTheirOperation) {
    EXPECT_EQ(errorOf("const int x; void f(void) { x++; }"), "1:29: increment of read-only variable 'x'");
    EXPECT_EQ(errorOf("const struct { int b; } s; void f(void) { --s.b; }"),
              "1:43: decrement of member 'b' in read-only object");
    EXPECT_EQ(errorOf("void f(void) { 1++; }"), "1:16: lvalue required as increment operand");
}

TEST_F(ExpressionsTest, CompoundAssignmentsHaveAComputationType) {
    const ast::TranslationUnit* unit = analyze("char c; double d; int *p; unsigned short u;\n"
                                               "void f(void) { c += d; p += 2; u <<= 1; c *= 2; }");
    ASSERT_NE(unit, nullptr) << firstError();
    const auto* f = find<ast::FunctionDecl>(unit, "f");
    std::vector<std::string> types;
    for (const ast::Stmt* stmt : f->body()->body()) {
        const auto* binary = ast::cast<ast::BinaryExpr>(ast::cast<ast::ExpressionStmt>(stmt)->expr());
        types.push_back(ast::typeToString(binary->computationType()) + " -> " + ast::typeToString(binary->type()));
    }
    EXPECT_EQ(types,
              (std::vector<std::string>{"double -> char", "int * -> int *", "int -> unsigned short", "int -> char"}));
}

TEST_F(ExpressionsTest, ConditionalOperator) {
    EXPECT_EQ(typeOf("int i; double d;", "i ? i : d"), "double");
    EXPECT_EQ(typeOf("int *p;", "p ? p : 0"), "int *");
    EXPECT_EQ(typeOf("const int *p; volatile int *q;", "p ? p : q"), "const volatile int *");
    EXPECT_EQ(typeOf("int *p; void *q;", "p ? p : q"), "void *");
    EXPECT_EQ(typeOf("struct s { int a; } v, w;", "1 ? v : w"), "struct s");
    EXPECT_EQ(typeOf("void f(void);", "1 ? f() : f()"), "void");
    EXPECT_EQ(typeOf("int *p; char *q;", "1 ? p : q"), "void *");
    EXPECT_TRUE(hasWarning("pointer type mismatch in conditional expression"));
    EXPECT_EQ(typeOf("int *p; long l;", "1 ? p : l"), "int *");
    EXPECT_TRUE(hasWarning("pointer/integer type mismatch in conditional expression"));
}

TEST_F(ExpressionsTest, BinaryConditionalConvertsTheConditionToTheResultType) {
    EXPECT_EQ(dumpExpr("char c; long l;", "c ?: l"), "ConditionalExpr ?: 'long'\n"
                                                     "  ImplicitCastExpr IntegralCast 'long'\n"
                                                     "    ImplicitCastExpr IntegralCast 'int'\n"
                                                     "      ImplicitCastExpr LValueToRValue 'char'\n"
                                                     "        DeclRefExpr c (VarDecl at 1:6) 'char' lvalue\n"
                                                     "  ImplicitCastExpr LValueToRValue 'long'\n"
                                                     "    DeclRefExpr l (VarDecl at 1:14) 'long' lvalue\n");
}

TEST_F(ExpressionsTest, MemberAccess) {
    EXPECT_EQ(typeOf("struct s { int a; } const v;", "v.a"), "const int lvalue");
    EXPECT_EQ(typeOf("struct s { int a[2]; } *p;", "p->a"), "int [2] lvalue");
    EXPECT_EQ(errorOf("struct s { int a; } v; void f(void) { v.b; }"), "1:41: 'struct s' has no member named 'b'");
    EXPECT_EQ(errorOf("int x; void f(void) { x.a; }"), "1:25: request for member 'a' in something not a structure "
                                                       "or union");
    EXPECT_EQ(errorOf("struct s *p; void f(void) { p->a; }"), "1:32: invalid use of undefined type 'struct s'");
}

TEST_F(ExpressionsTest, MembersOfAnonymousMembersGoThroughImplicitMemberExpressions) {
    EXPECT_EQ(dumpExpr("struct s { int a; union { int b; float c; }; } *p;", "p->c"),
              "MemberExpr .c 'float' lvalue\n"
              "  MemberExpr -> 'union <anonymous>' lvalue\n"
              "    ImplicitCastExpr LValueToRValue 'struct s *'\n"
              "      DeclRefExpr p (VarDecl at 1:49) 'struct s *' lvalue\n");
}

TEST_F(ExpressionsTest, Casts) {
    EXPECT_EQ(dumpExpr("int *p;", "(long)p"), "CastExpr (long) PointerToIntegral 'long'\n"
                                              "  ImplicitCastExpr LValueToRValue 'int *'\n"
                                              "    DeclRefExpr p (VarDecl at 1:6) 'int *' lvalue\n");
    EXPECT_EQ(dumpExpr("", "(char *)0"), "CastExpr (char *) NullToPointer 'char *'\n  IntegerLiteral 0 'int'\n");
    EXPECT_EQ(dumpExpr("int x;", "(void)x"), "CastExpr (void) ToVoid 'void'\n"
                                             "  DeclRefExpr x (VarDecl at 1:5) 'int' lvalue\n");
    EXPECT_EQ(typeOf("int x;", "(const int)x"), "int");
    EXPECT_EQ(typeOf("union u { int i; float f; }; int x;", "(union u)x"), "union u");
    // GNU: the operand initializes the member of its type.
    EXPECT_EQ(dumpExpr("union u { int i; float f; }; int x;", "(union u)(float)x"),
              "CastExpr (union u) ToUnion 'union u'\n"
              "  CastExpr (float) IntegralToFloating 'float'\n"
              "    ImplicitCastExpr LValueToRValue 'int'\n"
              "      DeclRefExpr x (VarDecl at 1:34) 'int' lvalue\n");
    EXPECT_EQ(dumpExpr("struct s { int a; }; union u { struct s s; int i; }; struct s v;", "(union u)v"),
              "CastExpr (union u) ToUnion 'union u'\n"
              "  ImplicitCastExpr LValueToRValue 'struct s'\n"
              "    DeclRefExpr v (VarDecl at 1:63) 'struct s' lvalue\n");
    EXPECT_EQ(errorOf("struct s { int a; } v; void f(void) { (int)v; }"),
              "1:39: aggregate value used where a scalar was expected");
    EXPECT_EQ(errorOf("double d; void f(void) { (int *)d; }"), "1:26: cannot convert between pointer and floating "
                                                               "type");
}

TEST_F(ExpressionsTest, CallsWithPrototypesConvertTheArguments) {
    EXPECT_EQ(dumpExpr("void g(long);", "g('a')"), "CallExpr 'void'\n"
                                                   "  ImplicitCastExpr FunctionToPointerDecay 'void (*)(long)'\n"
                                                   "    DeclRefExpr g (FunctionDecl at 1:6) 'void (long)' lvalue\n"
                                                   "  ImplicitCastExpr IntegralCast 'long'\n"
                                                   "    CharacterLiteral 97 'int'\n");
    EXPECT_EQ(errorOf("void g(int); void f(void) { g(); }"), "1:31: too few arguments to function 'g'");
    EXPECT_EQ(errorOf("void g(int); void f(void) { g(1, 2); }"), "1:34: too many arguments to function 'g'");
    EXPECT_EQ(errorOf("struct s { int a; } v; void g(int); void f(void) { g(v); }"),
              "1:54: incompatible type for argument 1 of 'g'");
}

TEST_F(ExpressionsTest, CallsWithoutPrototypesAndVariadicArgumentsArePromoted) {
    EXPECT_EQ(dumpExpr("void g(); float x;", "g(x)"), "CallExpr 'void'\n"
                                                      "  ImplicitCastExpr FunctionToPointerDecay 'void (*)()'\n"
                                                      "    DeclRefExpr g (FunctionDecl at 1:6) 'void ()' lvalue\n"
                                                      "  ImplicitCastExpr FloatingCast 'double'\n"
                                                      "    ImplicitCastExpr LValueToRValue 'float'\n"
                                                      "      DeclRefExpr x (VarDecl at 1:17) 'float' lvalue\n");
    EXPECT_EQ(typeOf("int printf(const char *, ...); char c;", "printf(\"%c\", c)"), "int");
    EXPECT_EQ(dumpExpr("void h(int, ...); short s;", "h(1, s)"),
              "CallExpr 'void'\n"
              "  ImplicitCastExpr FunctionToPointerDecay 'void (*)(int, ...)'\n"
              "    DeclRefExpr h (FunctionDecl at 1:6) 'void (int, ...)' lvalue\n"
              "  IntegerLiteral 1 'int'\n"
              "  ImplicitCastExpr IntegralCast 'int'\n"
              "    ImplicitCastExpr LValueToRValue 'short'\n"
              "      DeclRefExpr s (VarDecl at 1:25) 'short' lvalue\n");
}

TEST_F(ExpressionsTest, CallsThroughPointers) {
    EXPECT_EQ(typeOf("int (*fp)(int);", "fp(1)"), "int");
    EXPECT_EQ(typeOf("int (*fp)(int);", "(*fp)(1)"), "int");
    EXPECT_EQ(typeOf("const int g(void);", "g()"), "int");
    EXPECT_EQ(errorOf("int x; void f(void) { x(); }"), "1:23: called object is not a function or function pointer");
}

TEST_F(ExpressionsTest, SizeofAndAlignofHaveTypeSizeT) {
    EXPECT_EQ(typeOf("", "sizeof(int)"), "unsigned long");
    EXPECT_EQ(typeOf("", "sizeof(int)", DataModel::ILP32), "unsigned int");
    EXPECT_EQ(typeOf("int x;", "_Alignof(x)"), "unsigned long");
    EXPECT_EQ(errorOf("struct s; void f(void) { sizeof(struct s); }"),
              "1:26: invalid application of 'sizeof' to incomplete type 'struct s'");
    EXPECT_EQ(errorOf("struct s { int b : 3; } v; void f(void) { sizeof v.b; }"), "1:50: 'sizeof' applied to a "
                                                                                  "bit-field");
}

TEST_F(ExpressionsTest, OffsetofResolvesItsFields) {
    const ast::TranslationUnit* unit =
        analyze("struct s { int a; struct { int x[3]; } in; } v; unsigned long n = __builtin_offsetof(struct s, "
                "in.x[2]);");
    ASSERT_NE(unit, nullptr) << firstError();
    const auto* offsetOf = ast::cast<ast::OffsetOfExpr>(find<ast::VarDecl>(unit, "n")->initializer());
    EXPECT_EQ(offsetOf->components()[0].field->name().text(), "in");
    EXPECT_EQ(offsetOf->components()[1].field->name().text(), "x");
    EXPECT_EQ(errorOf("struct s { int a; }; unsigned long n = __builtin_offsetof(struct s, b);"),
              "1:40: 'struct s' has no member named 'b'");
}

TEST_F(ExpressionsTest, StatementExpressionsHaveTheValueOfTheirLastStatement) {
    EXPECT_EQ(typeOf("", "({ int x = 1; x; })"), "int");
    EXPECT_EQ(typeOf("", "({ int a[2]; a; })"), "int *");
    EXPECT_EQ(typeOf("", "({ ; })"), "void");
    // A labeled expression statement at the end gives the value too (GCC).
    EXPECT_EQ(typeOf("long x;", "({ goto done; done: x; })"), "long");
}

TEST_F(ExpressionsTest, WideCharactersHaveTypeWcharT) {
    EXPECT_EQ(typeOf("", "L'a'"), "int");
    EXPECT_EQ(typeOf("", "L'a'", DataModel::ILP32), "long");
    EXPECT_EQ(typeOf("", "L\"ab\"", DataModel::ILP32), "long [3] lvalue");
    EXPECT_NE(analyze("long w[] = L\"ab\";", DataModel::ILP32), nullptr) << firstError();
    EXPECT_EQ(errorOf("int w[] = L\"ab\";", DataModel::ILP32),
              "1:11: array of inappropriate type initialized from string constant");
}

TEST_F(ExpressionsTest, FunctionPointersCannotBeSubscripted) {
    EXPECT_EQ(errorOf("void (*fp)(void); void f(void) { fp[0]; }"), "1:34: subscripted value is pointer to function");
}

TEST_F(ExpressionsTest, GenericSelection) {
    EXPECT_EQ(typeOf("const int c;", "_Generic(c, int: 1.0, default: 'a')"), "double");
    EXPECT_EQ(typeOf("char a[3];", "_Generic(a, char *: 1, default: 2.0f)"), "int");
    EXPECT_EQ(typeOf("int x;", "_Generic(x, int: x, default: 0)"), "int lvalue");
    EXPECT_EQ(errorOf("void f(void) { _Generic(1, long: 2); }"),
              "1:25: '_Generic' selector of type 'int' is not compatible with any association");
}

TEST_F(ExpressionsTest, ChooseExpression) {
    EXPECT_EQ(typeOf("", "__builtin_choose_expr(1, 1.0, 'a')"), "double");
    EXPECT_EQ(typeOf("int x;", "__builtin_choose_expr(0, 1.0, x)"), "int lvalue");
    EXPECT_EQ(errorOf("int n; void f(void) { __builtin_choose_expr(n, 1, 2); }"),
              "1:45: first argument to '__builtin_choose_expr' is not an integer constant");
}

TEST_F(ExpressionsTest, VaArg) {
    EXPECT_EQ(typeOf("__builtin_va_list ap;", "__builtin_va_arg(ap, long)"), "long");
    EXPECT_EQ(errorOf("int x; void f(void) { __builtin_va_arg(x, int); }"),
              "1:40: first argument to 'va_arg' not of type 'va_list'");
}

TEST_F(ExpressionsTest, ComplexValuesAreRejected) {
    EXPECT_NE(analyze("_Complex double z; _Complex double *p = &z;"), nullptr) << firstError();
    EXPECT_EQ(errorOf("_Complex double z; void f(void) { z + 1; }"), "1:37: complex arithmetic is not supported");
    EXPECT_EQ(typeOf("_Complex double z;", "__real__ z"), "double lvalue");
}

} // namespace
} // namespace nitwit::sema
