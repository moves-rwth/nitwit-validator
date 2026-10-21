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
};

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
