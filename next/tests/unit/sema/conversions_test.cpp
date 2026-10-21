// Conversions as if by assignment (C17 6.5.16.1) with GCC's diagnostics, and the kinds of implicit casts.

#include <gtest/gtest.h>

#include "sema_test_support.hpp"

namespace nitwit::sema {
namespace {

class ConversionsTest : public SemaTest {
protected:
    // The cast kinds of the implicit casts at the top of the right operand of `x = value`, outermost first.
    std::string castsOf(std::string_view declarations, std::string_view value) {
        const ast::TranslationUnit* unit =
            analyze(std::format("{}\nvoid test(void) {{\nx = {};\n}}\n", declarations, value));
        EXPECT_NE(unit, nullptr) << firstError();
        if (unit == nullptr) {
            return "";
        }
        const auto* function = ast::cast<ast::FunctionDecl>(unit->decls.back());
        const auto* assignment =
            ast::cast<ast::BinaryExpr>(ast::cast<ast::ExpressionStmt>(function->body()->body().front())->expr());
        std::string kinds;
        for (const ast::Expr* expr = assignment->rhs(); const auto* cast = ast::dyn_cast<ast::ImplicitCastExpr>(expr);
             expr = cast->operand()) {
            kinds += kinds.empty() ? "" : " ";
            kinds += castKindName(cast->castKind());
        }
        return kinds;
    }

    static std::string_view castKindName(ast::CastKind kind) {
        switch (kind) {
        case ast::CastKind::LValueToRValue:
            return "LValueToRValue";
        case ast::CastKind::ArrayToPointerDecay:
            return "ArrayToPointerDecay";
        case ast::CastKind::FunctionToPointerDecay:
            return "FunctionToPointerDecay";
        case ast::CastKind::NoOp:
            return "NoOp";
        case ast::CastKind::IntegralCast:
            return "IntegralCast";
        case ast::CastKind::IntegralToBoolean:
            return "IntegralToBoolean";
        case ast::CastKind::IntegralToFloating:
            return "IntegralToFloating";
        case ast::CastKind::IntegralToPointer:
            return "IntegralToPointer";
        case ast::CastKind::PointerToIntegral:
            return "PointerToIntegral";
        case ast::CastKind::PointerToBoolean:
            return "PointerToBoolean";
        case ast::CastKind::NullToPointer:
            return "NullToPointer";
        case ast::CastKind::BitCast:
            return "BitCast";
        case ast::CastKind::FloatingCast:
            return "FloatingCast";
        case ast::CastKind::FloatingToIntegral:
            return "FloatingToIntegral";
        case ast::CastKind::FloatingToBoolean:
            return "FloatingToBoolean";
        case ast::CastKind::ToVoid:
            return "ToVoid";
        case ast::CastKind::ToUnion:
            return "ToUnion";
        }
        return "?";
    }
};

TEST_F(ConversionsTest, ArithmeticConversions) {
    EXPECT_EQ(castsOf("long x; int i;", "i"), "IntegralCast LValueToRValue");
    EXPECT_EQ(castsOf("_Bool x; int i;", "i"), "IntegralToBoolean LValueToRValue");
    EXPECT_EQ(castsOf("_Bool x; double d;", "d"), "FloatingToBoolean LValueToRValue");
    EXPECT_EQ(castsOf("double x; int i;", "i"), "IntegralToFloating LValueToRValue");
    EXPECT_EQ(castsOf("float x; double d;", "d"), "FloatingCast LValueToRValue");
    EXPECT_EQ(castsOf("int x; double d;", "d"), "FloatingToIntegral LValueToRValue");
    EXPECT_EQ(castsOf("enum e { A } x;", "1"), "IntegralCast");
    EXPECT_EQ(castsOf("int x; typedef int T; T t;", "t"), "LValueToRValue");
}

TEST_F(ConversionsTest, PointerConversions) {
    EXPECT_EQ(castsOf("int *x;", "0"), "NullToPointer");
    EXPECT_EQ(castsOf("int *x;", "(void *)0"), "NullToPointer");
    EXPECT_EQ(castsOf("int *x;", "1 - 1"), "NullToPointer");
    EXPECT_EQ(castsOf("int *x; void *v;", "v"), "BitCast LValueToRValue");
    EXPECT_EQ(castsOf("const int *x; int *p;", "p"), "NoOp LValueToRValue");
    EXPECT_EQ(castsOf("_Bool x; int *p;", "p"), "PointerToBoolean LValueToRValue");
    EXPECT_EQ(castsOf("void (*x)(void); void f(void);", "f"), "FunctionToPointerDecay");
    EXPECT_TRUE(warnings().empty());
}

TEST_F(ConversionsTest, IntegersAndPointersConvertWithAWarning) {
    EXPECT_EQ(castsOf("int *x; long l;", "l"), "IntegralToPointer LValueToRValue");
    EXPECT_TRUE(hasWarning("assignment to 'int *' from 'long' makes pointer from integer without a cast"));
    EXPECT_EQ(castsOf("long x; int *p;", "p"), "PointerToIntegral LValueToRValue");
    EXPECT_TRUE(hasWarning("assignment to 'long' from 'int *' makes integer from pointer without a cast"));
}

TEST_F(ConversionsTest, IncompatiblePointersConvertWithAWarning) {
    EXPECT_EQ(castsOf("int *x; double *p;", "p"), "BitCast LValueToRValue");
    EXPECT_TRUE(hasWarning("assignment to 'int *' from incompatible pointer type 'double *'"));
    EXPECT_EQ(castsOf("char **x; const char **p;", "p"), "BitCast LValueToRValue");
    EXPECT_TRUE(hasWarning("from incompatible pointer type"));
    // GCC accepts pointers to integer types that differ only in signedness without a warning by default.
    EXPECT_EQ(castsOf("char *x; unsigned char *p;", "p"), "BitCast LValueToRValue");
    EXPECT_TRUE(warnings().empty());
}

TEST_F(ConversionsTest, DiscardingQualifiersWarns) {
    EXPECT_EQ(castsOf("int *x; const int *p;", "p"), "NoOp LValueToRValue");
    EXPECT_TRUE(hasWarning("assignment discards 'const' qualifier from pointer target type"));
    EXPECT_NE(analyze("const char *s; char *t = s;"), nullptr) << firstError();
    EXPECT_TRUE(hasWarning("initialization discards 'const' qualifier from pointer target type"));
    EXPECT_NE(analyze("volatile int *s; void g(int *); void f(void) { g(s); }"), nullptr) << firstError();
    EXPECT_TRUE(hasWarning("passing argument 1 of 'g' discards 'volatile' qualifier from pointer target type"));
}

TEST_F(ConversionsTest, DiagnosticsNameTheirContext) {
    EXPECT_NE(analyze("int *f(void) { return 1; }"), nullptr) << firstError();
    EXPECT_TRUE(hasWarning("returning 'int' from a function with return type 'int *' makes pointer from integer "
                           "without a cast"));
    EXPECT_NE(analyze("void g(int *); void f(void) { g(5); }"), nullptr) << firstError();
    EXPECT_TRUE(hasWarning("passing argument 1 of 'g' makes pointer from integer without a cast"));
    EXPECT_NE(analyze("int i = (int)0; int *p = 4;"), nullptr) << firstError();
    EXPECT_TRUE(hasWarning("initialization of 'int *' from 'int' makes pointer from integer without a cast"));
}

TEST_F(ConversionsTest, IncompatibleTypesAreErrors) {
    EXPECT_EQ(errorOf("struct s { int a; } v; int x; void f(void) { x = v; }"),
              "1:48: incompatible types when assigning to type 'int' from type 'struct s'");
    EXPECT_EQ(errorOf("struct s { int a; } v; struct t { int a; } w; void f(void) { v = w; }"),
              "1:64: incompatible types when assigning to type 'struct s' from type 'struct t'");
    EXPECT_EQ(errorOf("double *p; double d = p;"), "1:23: incompatible types when initializing type 'double' using "
                                                   "type 'double *'");
}

TEST_F(ConversionsTest, StructuresAreCopied) {
    EXPECT_EQ(castsOf("struct s { int a; } x, y;", "y"), "LValueToRValue");
}

TEST_F(ConversionsTest, TransparentUnionsAcceptTheirMembers) {
    EXPECT_NE(analyze("typedef union { int *ip; char *cp; } arg __attribute__((transparent_union));\n"
                      "void g(arg a); int i; void f(void) { g(&i); }"),
              nullptr)
        << firstError();
    EXPECT_NE(analyze("union __attribute__((transparent_union)) u { int *ip; long l; };\n"
                      "void g(union u a); void f(void) { g(0); }"),
              nullptr)
        << firstError();
}

TEST_F(ConversionsTest, VaListAddressesOfCilStandForTheirVaList) {
    // CIL passes `(__va_list_tag *)&ap` where GCC's x86-64 va_list decays to a pointer.
    const ast::TranslationUnit* unit =
        analyze("typedef struct __va_list_tag __va_list_tag; void g(__builtin_va_list l);\n"
                "void f(int n, ...) { __builtin_va_list ap; __builtin_va_start((__va_list_tag *)&ap, n); "
                "g((__va_list_tag *)&ap); __builtin_va_end((__va_list_tag *)&ap); }");
    ASSERT_NE(unit, nullptr) << firstError();
    EXPECT_NE(analyze("void g(__builtin_va_list l);\n"
                      "void f(int n, ...) { __builtin_va_list ap; __builtin_va_start(&ap, n); g(&ap); "
                      "__builtin_va_end(&ap); }"),
              nullptr)
        << firstError();
    EXPECT_NE(analyze("typedef __builtin_va_list gnu_va_list[1U]; typedef gnu_va_list va_list[1U];\n"
                      "void f(int n, ...) { va_list va; __builtin_va_start(&va, n); __builtin_va_end(&va); }"),
              nullptr)
        << firstError();
    EXPECT_EQ(errorOf("typedef struct __va_list_tag __va_list_tag; void g(__builtin_va_list l);\n"
                      "void f(__va_list_tag *p) { g(p); }",
                      DataModel::ILP32),
              "2:30: incompatible type for argument 1 of 'g'");
}

} // namespace
} // namespace nitwit::sema
