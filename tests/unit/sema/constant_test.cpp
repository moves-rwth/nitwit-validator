// Integer constant expressions (constant.hpp): C17 6.6 and the GNU foldings GCC applies in their place.

#include "sema/constant.hpp"

#include <gtest/gtest.h>

#include "sema_test_support.hpp"

namespace nitwit::sema {
namespace {

class ConstantTest : public SemaTest {
protected:
    // The result of evaluateIntegerConstant for the expression statement `expression` after `declarations`.
    std::optional<Int128> evaluate(std::string_view declarations, std::string_view expression) {
        const ast::TranslationUnit* unit =
            analyze(std::format("{}\nvoid test(void) {{\n{};\n}}\n", declarations, expression));
        EXPECT_NE(unit, nullptr) << firstError();
        if (unit == nullptr) {
            return std::nullopt;
        }
        const auto* function = ast::cast<ast::FunctionDecl>(unit->decls.back());
        return evaluateIntegerConstant(context(),
                                       ast::cast<ast::ExpressionStmt>(function->body()->body().front())->expr());
    }

    // knownObjectSize for the call `call` in a function with a local `char local[32]`.
    std::optional<std::uint64_t> objectSize(std::string_view call, DataModel model = DataModel::LP64) {
        const ast::TranslationUnit* unit =
            analyze(std::format("struct S {{ char a[10]; int b; char c[3]; }} s; char buf[64]; struct S *p;\n"
                                "void test(void) {{\nchar local[32];\n{};\n}}\n",
                                call),
                    model);
        EXPECT_NE(unit, nullptr) << firstError();
        if (unit == nullptr) {
            return std::nullopt;
        }
        const auto* function = ast::cast<ast::FunctionDecl>(unit->decls.back());
        const auto* statement = ast::cast<ast::ExpressionStmt>(function->body()->body()[1]);
        return knownObjectSize(context(), ast::cast<ast::CallExpr>(statement->expr()));
    }
};

// The values of gcc-14 -O0 on x86-64 and i386.
TEST_F(ConstantTest, KnownObjectSizesAsGccComputesThemAtO0) {
    for (const int type : {0, 1, 2, 3}) {
        EXPECT_EQ(objectSize(std::format("__builtin_object_size(buf, {})", type)), 64u);
    }
    EXPECT_EQ(objectSize("__builtin_object_size(s.a, 0)"), 20u);
    EXPECT_EQ(objectSize("__builtin_object_size(s.a, 1)"), 10u);
    EXPECT_EQ(objectSize("__builtin_object_size(s.a, 2)"), 20u);
    EXPECT_EQ(objectSize("__builtin_object_size(s.a, 3)"), 10u);
    EXPECT_EQ(objectSize("__builtin_object_size(&s.b, 0)"), 8u);
    EXPECT_EQ(objectSize("__builtin_object_size(&s.b, 1)"), 4u);
    EXPECT_EQ(objectSize("__builtin_object_size(&buf[4], 0)"), 60u);
    EXPECT_EQ(objectSize("__builtin_object_size(&s.a[3], 1)"), 7u);
    EXPECT_EQ(objectSize("__builtin_object_size(buf + 4, 0)"), 60u);
    EXPECT_EQ(objectSize("__builtin_object_size(local, 0)"), 32u);
    EXPECT_EQ(objectSize("__builtin_object_size(&s, 0)"), 20u);
    EXPECT_EQ(objectSize("__builtin_object_size((char *)&s + 2, 0)"), 18u);
    EXPECT_EQ(objectSize("__builtin_object_size((void *)buf, 0)"), 64u);
    EXPECT_EQ(objectSize("__builtin_dynamic_object_size(buf, 0)"), 64u);
    EXPECT_EQ(objectSize("__builtin_dynamic_object_size(s.c, 1)"), 3u);
    EXPECT_EQ(objectSize("__builtin_object_size(s.a, 1)", DataModel::ILP32), 10u);
    // Unknown: the library answers (size_t)-1 or 0.
    EXPECT_EQ(objectSize("__builtin_object_size(p->a, 1)"), std::nullopt);
    EXPECT_EQ(objectSize("__builtin_object_size(p, 0)"), std::nullopt);
}

TEST_F(ConstantTest, ArithmeticInTheTypesOfTheOperands) {
    EXPECT_EQ(constantOf("", "1 + 2 * 3"), 7);
    EXPECT_EQ(constantOf("", "7 / -2"), -3);
    EXPECT_EQ(constantOf("", "-7 % 2"), -1);
    EXPECT_EQ(constantOf("", "(int)(-1u >> 1)"), 0x7fffffff);
    EXPECT_EQ(constantOf("", "-1 < 0u"), 0);
    EXPECT_EQ(constantOf("", "(int)(0xffffffffu + 1u)"), 0);
    EXPECT_EQ(constantOf("", "(unsigned char)200 + (signed char)-100"), 100);
    EXPECT_EQ(constantOf("", "5 ^ 3 | 8 & 12"), 14);
    EXPECT_EQ(constantOf("", "~5"), -6);
    EXPECT_EQ(constantOf("", "!5"), 0);
}

TEST_F(ConstantTest, ConversionsTruncate) {
    EXPECT_EQ(constantOf("", "(int)(unsigned char)-1"), 255);
    EXPECT_EQ(constantOf("", "(int)(signed char)255"), -1);
    EXPECT_EQ(constantOf("", "(int)(short)70000"), 4464);
    EXPECT_EQ(constantOf("", "(_Bool)256"), 1);
}

TEST_F(ConstantTest, LongDependsOnTheDataModel) {
    EXPECT_EQ(constantOf("", "(int)(-1L < 0u)", DataModel::ILP32), 0);
    EXPECT_EQ(constantOf("", "(int)(-1L < 0u)", DataModel::LP64), 1);
    EXPECT_EQ(constantOf("", "sizeof(long)", DataModel::ILP32), 4);
}

TEST_F(ConstantTest, LogicalAndConditionalOperatorsSkipUnevaluatedOperands) {
    int dummy = 0;
    (void)dummy;
    EXPECT_EQ(constantOf("int f(void);", "0 && f()"), 0);
    EXPECT_EQ(constantOf("int f(void);", "1 || f()"), 1);
    EXPECT_EQ(constantOf("int f(void);", "1 ? 2 : f()"), 2);
    EXPECT_EQ(constantOf("", "0 ?: 5"), 5);
    EXPECT_EQ(constantOf("", "3 ?: 5"), 3);
}

TEST_F(ConstantTest, ShiftsIntoTheSignBitAreFoldedAsInGcc) {
    EXPECT_EQ(constantOf("", "1 << 31 >> 31"), -1);
    EXPECT_EQ(constantOf("", "-1 << 3"), -8);
}

TEST_F(ConstantTest, UndefinedBehaviourIsNotConstant) {
    EXPECT_EQ(errorOf("enum { A = 2147483647 + 1 };"), "1:12: enumerator value for 'A' is not an integer constant");
    EXPECT_EQ(errorOf("enum { A = 1 / 0 };"), "1:12: enumerator value for 'A' is not an integer constant");
    EXPECT_EQ(errorOf("enum { A = 1 << 32 };"), "1:12: enumerator value for 'A' is not an integer constant");
    EXPECT_EQ(errorOf("enum { A = (int)1e10 };"), "1:12: enumerator value for 'A' is not an integer constant");
}

TEST_F(ConstantTest, ObjectsAndCommaExpressionsAreNotConstant) {
    EXPECT_EQ(errorOf("const int n = 1; enum { A = n };"), "1:29: enumerator value for 'A' is not an integer constant");
    EXPECT_EQ(errorOf("enum { A = (1, 2) };"), "1:12: enumerator value for 'A' is not an integer constant");
    EXPECT_EQ(evaluate("int x;", "x + 1"), std::nullopt);
}

TEST_F(ConstantTest, FloatingOperandsOfCastsAreFolded) {
    EXPECT_EQ(constantOf("", "(int)2.5"), 2);
    EXPECT_EQ(constantOf("", "(int)(1.5 * 3)"), 4);
    EXPECT_EQ(constantOf("", "(int)-2.9"), -2);
    EXPECT_EQ(constantOf("", "1.5 > 1"), 1);
    EXPECT_EQ(constantOf("", "(int)(0.1f + 0.2f > 0.3f)"), 0);
}

TEST_F(ConstantTest, SizeofAndAlignof) {
    EXPECT_EQ(constantOf("struct s { char c; double d; };", "sizeof(struct s)", DataModel::ILP32), 12);
    EXPECT_EQ(constantOf("", "_Alignof(double)", DataModel::ILP32), 4);
    EXPECT_EQ(constantOf("", "__alignof__(double)", DataModel::ILP32), 8);
    EXPECT_EQ(constantOf("int a[10];", "sizeof a / sizeof a[0]"), 10);
    EXPECT_EQ(constantOf("", "sizeof(\"abc\")"), 4);
    EXPECT_EQ(constantOf("", "sizeof(L\"abc\")"), 16);
}

TEST_F(ConstantTest, AlignofAnExpressionUsesItsDeclaration) {
    EXPECT_EQ(constantOf("double d;", "__alignof__(d)", DataModel::ILP32), 8);
    EXPECT_EQ(constantOf("char c __attribute__((aligned(16)));", "__alignof__(c)"), 16);
    EXPECT_EQ(constantOf("struct s { char c; double d; } v;", "__alignof__(v.d)", DataModel::ILP32), 4);
    EXPECT_EQ(constantOf("struct __attribute__((packed)) s { char c; int i; } v;", "__alignof__(v.i)"), 1);
}

TEST_F(ConstantTest, OffsetofAndTheNullPointerIdiom) {
    const std::string_view s = "struct s { char c; int i; struct { short x[3]; } in; long long t[2]; };";
    EXPECT_EQ(constantOf(s, "__builtin_offsetof(struct s, i)"), 4);
    EXPECT_EQ(constantOf(s, "__builtin_offsetof(struct s, in.x[2])"), 12);
    EXPECT_EQ(constantOf(s, "__builtin_offsetof(struct s, t[1])"), 24);
    EXPECT_EQ(constantOf(s, "(int)(long)&((struct s *)0)->in.x[1]"), 10);
    EXPECT_EQ(constantOf(s, "(int)(long)((struct s *)0 + 2)"), 64);
    EXPECT_EQ(constantOf("", "(int)(long)((int *)16 - (int *)4)"), 3);
    EXPECT_EQ(constantOf("", "(int)(long)(char *)0 + 4"), 4);
}

TEST_F(ConstantTest, OffsetofThroughAnonymousMembers) {
    EXPECT_EQ(constantOf("struct s { char c; union { int a; struct { char b; short d; }; }; };",
                         "__builtin_offsetof(struct s, d)"),
              6);
}

TEST_F(ConstantTest, EnumerationConstantsAndCharacters) {
    EXPECT_EQ(constantOf("enum e { A = 3, B };", "B * 2"), 8);
    EXPECT_EQ(constantOf("", "'a'"), 97);
    EXPECT_EQ(constantOf("", "(char)'\\xff'"), -1);
    EXPECT_EQ(constantOf("", "'ab'"), 24930);
}

TEST_F(ConstantTest, BuiltinsWithConstantArguments) {
    EXPECT_EQ(constantOf("", "__builtin_constant_p(5)"), 1);
    EXPECT_EQ(constantOf("int x;", "__builtin_constant_p(x)"), 0);
    EXPECT_EQ(constantOf("", "__builtin_expect(7, 1)"), 7);
    EXPECT_EQ(constantOf("", "__builtin_bswap32(0x12345678) == 0x78563412"), 1);
    EXPECT_EQ(constantOf("", "__builtin_bswap16(0x1234)"), 0x3412);
    EXPECT_EQ(constantOf("", "__builtin_clz(1)"), 31);
    EXPECT_EQ(constantOf("", "__builtin_clzll(1)"), 63);
    EXPECT_EQ(constantOf("", "__builtin_ctzll(0x100000000ULL)"), 32);
    EXPECT_EQ(constantOf("", "__builtin_popcount(0xff00ff)"), 16);
    EXPECT_EQ(constantOf("", "__builtin_parity(7)"), 1);
    EXPECT_EQ(constantOf("", "__builtin_ffs(8)"), 4);
    EXPECT_EQ(errorOf("enum { A = __builtin_clz(0) };"), "1:12: enumerator value for 'A' is not an integer constant");
    EXPECT_EQ(constantOf("", "__builtin_constant_p(1.5)"), 1);
    EXPECT_EQ(constantOf("", "__builtin_constant_p(\"abc\")"), 1);
    EXPECT_EQ(constantOf("char a[2];", "__builtin_constant_p(a)"), 0);
}

TEST_F(ConstantTest, BuiltinsOfFloatingArgumentsAreNotFolded) {
    for (const std::string_view call :
         {"__builtin_isnan(1.0)", "__builtin_lround(2.5)", "__builtin_signbit(-1.0)", "__builtin_isgreater(1.0, 2.0)",
          "__builtin_fpclassify(0, 1, 2, 3, 4, 1.0)", "__builtin_isinf_sign(1.0)", "__builtin_ilogb(8.0)"}) {
        EXPECT_EQ(errorOf(std::format("enum {{ A = {} }};", call)),
                  "1:12: enumerator value for 'A' is not an integer constant")
            << call;
    }
    // Where a null pointer constant could stand, sema evaluates them too.
    EXPECT_NE(analyze("int a = __builtin_isnan(1.0); long b = __builtin_lround(2.5); int *p = __builtin_signbit(1.0);"),
              nullptr)
        << firstError();
}

TEST_F(ConstantTest, PointersSignExtendToWiderIntegersAsInGcc) {
    EXPECT_EQ(constantOf("", "(long long)(char *)-1 < 0", DataModel::ILP32), 1);
    EXPECT_EQ(constantOf("", "(unsigned long long)(char *)0x80000000u >> 32 == 0xffffffff", DataModel::ILP32), 1);
    EXPECT_EQ(constantOf("", "(__int128)(char *)-1 < 0"), 1);
}

TEST_F(ConstantTest, AlignofAVariableUsesItsRequestedAlignment) {
    EXPECT_EQ(constantOf("int x __attribute__((aligned(1)));", "__alignof__(x)"), 1);
    EXPECT_EQ(constantOf("_Alignas(0) double x;", "__alignof__(x)", DataModel::ILP32), 8);
    EXPECT_EQ(constantOf("_Alignas(32) char x;", "__alignof__(x)"), 32);
}

TEST_F(ConstantTest, GnuTypeBuiltins) {
    EXPECT_EQ(constantOf("", "__builtin_types_compatible_p(int, const int)"), 1);
    EXPECT_EQ(constantOf("", "__builtin_types_compatible_p(int, long)"), 0);
    EXPECT_EQ(constantOf("typedef int T;", "__builtin_types_compatible_p(T[3], int[3])"), 1);
    EXPECT_EQ(constantOf("", "__builtin_choose_expr(1, 3, 4.0)"), 3);
    EXPECT_EQ(constantOf("", "_Generic(1L, int: 1, long: 2, default: 3)"), 2);
}

TEST_F(ConstantTest, ResultsAreConvertedToTheExpressionsType) {
    EXPECT_EQ(evaluate("", "(unsigned char)511"), 255);
    EXPECT_EQ(evaluate("", "-1u"), 4294967295);
    EXPECT_EQ(evaluate("", "(long long)-1"), -1);
    EXPECT_EQ(evaluate("", "1.5"), std::nullopt);
}

} // namespace
} // namespace nitwit::sema
