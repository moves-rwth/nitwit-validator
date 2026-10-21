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

TEST_F(BuiltinsTest, UnknownBuiltinsWarn) {
    EXPECT_EQ(signatureAfter("__builtin_frobnicate(1)", "__builtin_frobnicate"), "int ()");
    EXPECT_TRUE(hasWarning("unknown builtin function '__builtin_frobnicate'"));
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
