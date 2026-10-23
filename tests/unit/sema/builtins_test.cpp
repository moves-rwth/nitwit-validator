// Builtin functions: GCC builtin signatures, library signatures for implicit declarations, and the type-generic
// builtins that sema types at each call.

#include <gtest/gtest.h>

#include "sema_test_support.hpp"

namespace nitwit::sema {
namespace {

class BuiltinsTest : public SemaTest {
protected:
    // The type of the function `name` after a call `call` in a function body.
    std::string signatureAfter(std::string_view call, std::string_view name, DataModel model = DataModel::LP64) {
        const ast::TranslationUnit* unit = analyze(std::format("void f(void) {{ {}; }}", call), model);
        EXPECT_NE(unit, nullptr) << firstError();
        return unit != nullptr ? ast::typeToString(find<ast::FunctionDecl>(unit, name)->type()) : "";
    }
};

TEST_F(BuiltinsTest, GccBuiltinsGetTheirSignatures) {
    EXPECT_EQ(signatureAfter("__builtin_expect(1, 0)", "__builtin_expect"), "long (long, long)");
    EXPECT_EQ(signatureAfter("__builtin_memcpy(0, 0, 1)", "__builtin_memcpy"),
              "void *(void *, const void *, unsigned long)");
    EXPECT_EQ(signatureAfter("__builtin_bswap64(1)", "__builtin_bswap64", DataModel::ILP32),
              "unsigned long long (unsigned long long)");
    EXPECT_EQ(signatureAfter("__builtin_bswap64(1)", "__builtin_bswap64"), "unsigned long (unsigned long)");
    EXPECT_EQ(signatureAfter("__builtin_clzl(1)", "__builtin_clzl"), "int (unsigned long)");
    EXPECT_EQ(signatureAfter("__builtin_inff()", "__builtin_inff"), "float (void)");
    EXPECT_EQ(signatureAfter("__builtin_fabsl(1)", "__builtin_fabsl"), "long double (long double)");
    EXPECT_EQ(signatureAfter("__builtin_object_size(0, 0)", "__builtin_object_size"),
              "unsigned long (const void *, int)");
    EXPECT_EQ(signatureAfter("__builtin_sadd_overflow(1, 2, 0)", "__builtin_sadd_overflow"), "_Bool (int, int, int *)");
}

TEST_F(BuiltinsTest, LibraryFunctionsGetTheirSignatures) {
    EXPECT_EQ(signatureAfter("printf(\"x\")", "printf"), "int (const char *, ...)");
    EXPECT_EQ(signatureAfter("exit(1)", "exit"), "void (int)");
    EXPECT_EQ(signatureAfter("sqrtf(1)", "sqrtf"), "float (float)");
    EXPECT_EQ(signatureAfter("isdigit('1')", "isdigit"), "int (int)");
    // Names of builtins without a library function stay `int ()` without the prefix.
    EXPECT_EQ(signatureAfter("expect(1, 0)", "expect"), "int ()");
}

TEST_F(BuiltinsTest, NoreturnBuiltins) {
    const ast::TranslationUnit* unit = analyze("void f(void) { abort(); __builtin_trap(); }");
    ASSERT_NE(unit, nullptr) << firstError();
    EXPECT_TRUE(find<ast::FunctionDecl>(unit, "abort")->isNoreturn());
    EXPECT_TRUE(find<ast::FunctionDecl>(unit, "__builtin_trap")->isNoreturn());
}

TEST_F(BuiltinsTest, UnknownBuiltinsAreErrors) {
    EXPECT_EQ(errorOf("void f(void) { __builtin_frobnicate(1); }"),
              "1:16: implicit declaration of function '__builtin_frobnicate'");
}

TEST_F(BuiltinsTest, FortifyCheckingBuiltins) {
    EXPECT_EQ(signatureAfter("__builtin___memcpy_chk(0, 0, 1, 1)", "__builtin___memcpy_chk"),
              "void *(void *, const void *, unsigned long, unsigned long)");
    EXPECT_EQ(signatureAfter("__builtin___mempcpy_chk(0, 0, 1, 1)", "__builtin___mempcpy_chk"),
              "void *(void *, const void *, unsigned long, unsigned long)");
    EXPECT_EQ(signatureAfter("__builtin___stpcpy_chk(0, 0, 1)", "__builtin___stpcpy_chk"),
              "char *(char *, const char *, unsigned long)");
    EXPECT_EQ(signatureAfter("__builtin___strncat_chk(0, 0, 1, 1)", "__builtin___strncat_chk"),
              "char *(char *, const char *, unsigned long, unsigned long)");
    EXPECT_EQ(signatureAfter("__builtin___sprintf_chk(0, 1, 1, \"\")", "__builtin___sprintf_chk"),
              "int (char *, int, unsigned long, const char *, ...)");
    EXPECT_EQ(signatureAfter("__builtin___printf_chk(1, \"\")", "__builtin___printf_chk", DataModel::ILP32),
              "int (int, const char *, ...)");
    const ast::TranslationUnit* unit =
        analyze("void f(char *s, __builtin_va_list ap) { __builtin___vsnprintf_chk(s, 4, 1, 4, \"%d\", ap); }");
    ASSERT_NE(unit, nullptr) << firstError();
    EXPECT_EQ(ast::typeToString(find<ast::FunctionDecl>(unit, "__builtin___vsnprintf_chk")->type()),
              "int (char *, unsigned long, int, unsigned long, const char *, __builtin_va_list)");
}

TEST_F(BuiltinsTest, MathBuiltinsForOneType) {
    EXPECT_EQ(signatureAfter("__builtin_signbitf(1.0f)", "__builtin_signbitf"), "int (float)");
    EXPECT_EQ(signatureAfter("__builtin_signbitl(1.0L)", "__builtin_signbitl"), "int (long double)");
    EXPECT_EQ(signatureAfter("__builtin_isinff(1.0f)", "__builtin_isinff"), "int (float)");
    EXPECT_EQ(signatureAfter("isnanl(1.0L)", "isnanl"), "int (long double)");
    EXPECT_EQ(signatureAfter("__builtin_expm1l(1)", "__builtin_expm1l"), "long double (long double)");
    EXPECT_EQ(signatureAfter("__builtin_scalbnf(1, 2)", "__builtin_scalbnf"), "float (float, int)");
    EXPECT_EQ(signatureAfter("__builtin_dynamic_object_size(0, 0)", "__builtin_dynamic_object_size"),
              "unsigned long (const void *, int)");
}

TEST_F(BuiltinsTest, FpclassifyKeepsItsLastArgument) {
    EXPECT_EQ(dumpExpr("float x;", "__builtin_fpclassify(0, 1, 4, 3, 2, x)"),
              "CallExpr 'int'\n"
              "  ImplicitCastExpr FunctionToPointerDecay 'int (*)()'\n"
              "    DeclRefExpr __builtin_fpclassify (FunctionDecl at 3:1) 'int ()' lvalue\n"
              "  IntegerLiteral 0 'int'\n"
              "  IntegerLiteral 1 'int'\n"
              "  IntegerLiteral 4 'int'\n"
              "  IntegerLiteral 3 'int'\n"
              "  IntegerLiteral 2 'int'\n"
              "  ImplicitCastExpr LValueToRValue 'float'\n"
              "    DeclRefExpr x (VarDecl at 1:7) 'float' lvalue\n");
    EXPECT_EQ(errorOf("int f(void) { return __builtin_fpclassify(0, 1, 2, 3, 4, 1); }"),
              "1:58: non-floating-point argument in call to function '__builtin_fpclassify'");
    EXPECT_EQ(errorOf("int f(void) { return __builtin_fpclassify(0, 1, 2, 3, 4.0, 1.0); }"),
              "1:55: non-integer argument 5 in call to function '__builtin_fpclassify'");
}

TEST_F(BuiltinsTest, FloatComparisonsNeedAFloatingOperand) {
    EXPECT_EQ(typeOf("double x;", "__builtin_isgreater(x, 2)"), "int");
    EXPECT_EQ(typeOf("float x;", "__builtin_isless((_Bool)1, x)"), "int");
    EXPECT_EQ(errorOf("int f(void) { return __builtin_isgreater(1, 2); }"),
              "1:42: non-floating-point arguments in call to function '__builtin_isgreater'");
}

TEST_F(BuiltinsTest, AtomicsAreTypedByTheObject) {
    const std::string_view objects =
        "struct S { int a, b; } s, t; int x; long l; char c; _Bool b; int *p; const volatile long cvl; "
        "enum e { E1, E2 } en;";
    EXPECT_EQ(typeOf(objects, "__atomic_load_n(&x, 5)"), "int");
    EXPECT_EQ(typeOf(objects, "__atomic_load_n(&cvl, 0)"), "long");
    EXPECT_EQ(typeOf(objects, "__atomic_load_n(&en, 0)"), "enum e");
    EXPECT_EQ(typeOf(objects, "__atomic_exchange_n(&b, 1, 0)"), "_Bool");
    EXPECT_EQ(typeOf(objects, "__atomic_fetch_add(&p, 1, 0)"), "int *");
    EXPECT_EQ(typeOf(objects, "__atomic_or_fetch(&c, 1, 0)"), "char");
    EXPECT_EQ(typeOf(objects, "__atomic_compare_exchange_n(&x, &x, 1, 0, 0, 0)"), "_Bool");
    EXPECT_EQ(typeOf(objects, "__atomic_compare_exchange(&s, &t, &t, 0, 0, 0)"), "_Bool");
    EXPECT_EQ(typeOf(objects, "__atomic_load(&s, &t, 0)"), "void");
    EXPECT_EQ(typeOf(objects, "__atomic_store(&s, &t, 0)"), "void");
    EXPECT_EQ(typeOf(objects, "__atomic_exchange(&s, &t, &t, 0)"), "void");
    EXPECT_EQ(typeOf(objects, "__atomic_store_n(&l, 1, 0)"), "void");
    EXPECT_EQ(typeOf(objects, "__sync_fetch_and_add(&c, 1)"), "char");
    EXPECT_EQ(typeOf(objects, "__sync_fetch_and_add(&x, 1, x, l)"), "int");
    EXPECT_EQ(typeOf(objects, "__sync_bool_compare_and_swap(&x, 1, 2)"), "_Bool");
    EXPECT_EQ(typeOf(objects, "__sync_val_compare_and_swap(&l, 1, 2)"), "long");
    EXPECT_EQ(typeOf(objects, "__sync_lock_test_and_set(&x, 1)"), "int");
    EXPECT_EQ(typeOf(objects, "__sync_lock_release(&x)"), "void");
    EXPECT_EQ(typeOf(objects, "__sync_synchronize()"), "void");
    EXPECT_EQ(typeOf(objects, "__atomic_thread_fence(5)"), "void");
    EXPECT_EQ(typeOf(objects, "__atomic_test_and_set(&b, 0)"), "_Bool");
    EXPECT_EQ(typeOf(objects, "__atomic_clear(&b, 0)"), "void");
    EXPECT_NE(analyze(std::format("{} _Static_assert(__builtin_types_compatible_p("
                                  "__typeof__(__atomic_load_n(&x, 0)), int), \"\");",
                                  objects)),
              nullptr)
        << firstError();
}

TEST_F(BuiltinsTest, AtomicValuesAreConvertedToTheObjectType) {
    EXPECT_EQ(dumpExpr("long l;", "__atomic_store_n(&l, 1, 0)"),
              "CallExpr 'void'\n"
              "  ImplicitCastExpr FunctionToPointerDecay 'int (*)()'\n"
              "    DeclRefExpr __atomic_store_n (FunctionDecl at 3:1) 'int ()' lvalue\n"
              "  UnaryExpr & 'long *'\n"
              "    DeclRefExpr l (VarDecl at 1:6) 'long' lvalue\n"
              "  ImplicitCastExpr IntegralCast 'long'\n"
              "    IntegerLiteral 1 'int'\n"
              "  IntegerLiteral 0 'int'\n");
    // Arithmetic on a pointer adds bytes, as on an integer of the pointer's size.
    EXPECT_EQ(dumpExpr("int *p;", "__atomic_fetch_add(&p, 1, 0)", DataModel::ILP32),
              "CallExpr 'int *'\n"
              "  ImplicitCastExpr FunctionToPointerDecay 'int (*)()'\n"
              "    DeclRefExpr __atomic_fetch_add (FunctionDecl at 3:1) 'int ()' lvalue\n"
              "  UnaryExpr & 'int **'\n"
              "    DeclRefExpr p (VarDecl at 1:6) 'int *' lvalue\n"
              "  ImplicitCastExpr IntegralCast 'unsigned int'\n"
              "    IntegerLiteral 1 'int'\n"
              "  IntegerLiteral 0 'int'\n");
}

TEST_F(BuiltinsTest, AtomicErrorsAsInGcc) {
    const std::string_view objects = "struct S { int a, b; } s; _Bool b; long l; char c; const int ci;";
    EXPECT_EQ(errorOf(std::format("{}\nvoid f(void) {{ __atomic_load_n(&s, 0); }}", objects)),
              "2:32: operand type 'struct S *' is incompatible with argument 1 of '__atomic_load_n'");
    EXPECT_EQ(errorOf(std::format("{}\nvoid f(void) {{ __atomic_fetch_add(&b, 1, 0); }}", objects)),
              "2:35: operand type '_Bool *' is incompatible with argument 1 of '__atomic_fetch_add'");
    EXPECT_EQ(errorOf(std::format("{}\nvoid f(void) {{ __atomic_load_n(l, 0); }}", objects)),
              "2:32: operand type 'long' is incompatible with argument 1 of '__atomic_load_n'");
    EXPECT_EQ(errorOf(std::format("{}\nvoid f(void) {{ __atomic_load((void *)0, &l, 0); }}", objects)),
              "2:30: argument 1 of '__atomic_load' must be a non-void pointer type");
    EXPECT_EQ(errorOf(std::format("{}\nvoid f(void) {{ __atomic_load(&s, &c, 0); }}", objects)),
              "2:34: size mismatch in argument 2 of '__atomic_load'");
    EXPECT_EQ(errorOf(std::format("{}\nvoid f(void) {{ __atomic_exchange_n(&l, s, 0); }}", objects)),
              "2:40: aggregate value used where an integer was expected");
    EXPECT_EQ(errorOf(std::format("{}\nvoid f(void) {{ __atomic_load_n(&l); }}", objects)),
              "2:34: too few arguments to function '__atomic_load_n'");
    EXPECT_EQ(errorOf(std::format("{}\nvoid f(void) {{ __atomic_load_n(&l, 0, 0); }}", objects)),
              "2:39: too many arguments to function '__atomic_load_n'");
    EXPECT_EQ(errorOf(std::format("{}\nvoid f(void) {{ __atomic_load(&s, &s, &c); }}", objects)),
              "2:38: non-integer memory model argument 3 of '__atomic_load'");
    EXPECT_NE(analyze(std::format("{}\nvoid f(void) {{ __atomic_store_n(&ci, 1, 0); }}", objects)), nullptr)
        << firstError();
    EXPECT_TRUE(hasWarning("passing argument 1 of '__atomic_store_n' discards 'const' qualifier from pointer "
                           "target type"));
}

TEST_F(BuiltinsTest, OverflowBuiltinsTakeAnyIntegerTypes) {
    EXPECT_EQ(typeOf("long a; unsigned char b; short r;", "__builtin_add_overflow(a, b, &r)"), "_Bool");
    EXPECT_EQ(errorOf("double d; int r; void f(void) { __builtin_mul_overflow(d, 1, &r); }"),
              "1:56: argument 1 in call to function '__builtin_mul_overflow' does not have integral type");
    EXPECT_EQ(errorOf("int r; void f(void) { __builtin_sub_overflow(1, 1, r); }"),
              "1:52: argument 3 in call to function '__builtin_sub_overflow' does not have pointer to integral type");
}

TEST_F(BuiltinsTest, FloatingPointClassificationIsTypeGeneric) {
    EXPECT_EQ(dumpExpr("float x;", "__builtin_isnan(x)"),
              "CallExpr 'int'\n"
              "  ImplicitCastExpr FunctionToPointerDecay 'int (*)()'\n"
              "    DeclRefExpr __builtin_isnan (FunctionDecl at 3:1) 'int ()' lvalue\n"
              "  ImplicitCastExpr LValueToRValue 'float'\n"
              "    DeclRefExpr x (VarDecl at 1:7) 'float' lvalue\n");
    EXPECT_EQ(typeOf("double x, y;", "__builtin_isgreater(x, y)"), "int");
    EXPECT_EQ(errorOf("void f(void) { __builtin_isinf(1); }"),
              "1:32: non-floating-point argument in call to function '__builtin_isinf'");
}

TEST_F(BuiltinsTest, ConstantPDoesNotConvertItsOperand) {
    EXPECT_EQ(dumpExpr("int x;", "__builtin_constant_p(x)"),
              "CallExpr 'int'\n"
              "  ImplicitCastExpr FunctionToPointerDecay 'int (*)()'\n"
              "    DeclRefExpr __builtin_constant_p (FunctionDecl at 3:1) 'int ()' lvalue\n"
              "  DeclRefExpr x (VarDecl at 1:5) 'int' lvalue\n");
}

TEST_F(BuiltinsTest, VaStartKeepsItsVaListAnLvalue) {
    const std::string dump = dumpOf("void f(int n, ...) { __builtin_va_list ap; __builtin_va_start(ap, n); "
                                    "__builtin_va_end(ap); }");
    EXPECT_NE(dump.find("    DeclRefExpr __builtin_va_start (FunctionDecl at 1:44) 'int ()' lvalue\n"
                        "        DeclRefExpr ap"),
              std::string::npos)
        << dump;
    EXPECT_EQ(errorOf("void f(int n) { __builtin_va_list ap; __builtin_va_start(ap, n); }"),
              "1:39: 'va_start' used in function with fixed arguments");
    EXPECT_EQ(errorOf("void f(int n, ...) { int x; __builtin_va_start(x, n); }"),
              "1:48: argument of '__builtin_va_start' is not an lvalue of type 'va_list'");
    EXPECT_NE(analyze("void f(int n, ...) { __builtin_va_list a, b; __builtin_va_start(a, n); "
                      "__builtin_va_copy(b, a); __builtin_va_end(b); __builtin_va_end(a); }"),
              nullptr)
        << firstError();
}

TEST_F(BuiltinsTest, VaListIsCopiedWhenPassed) {
    EXPECT_EQ(dumpExpr("void g(__builtin_va_list); __builtin_va_list ap;", "g(ap)"),
              "CallExpr 'void'\n"
              "  ImplicitCastExpr FunctionToPointerDecay 'void (*)(__builtin_va_list)'\n"
              "    DeclRefExpr g (FunctionDecl at 1:6) 'void (__builtin_va_list)' lvalue\n"
              "  ImplicitCastExpr LValueToRValue '__builtin_va_list'\n"
              "    DeclRefExpr ap (VarDecl at 1:46) '__builtin_va_list' lvalue\n");
}

} // namespace
} // namespace nitwit::sema
