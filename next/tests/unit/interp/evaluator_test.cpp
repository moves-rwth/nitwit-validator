// The evaluator: operators, conversions, objects and calls, in both data models. The expected outputs are those of
// the same programs compiled with GCC (-m32 -msse2 -mfpmath=sse, -m64).

#include <gtest/gtest.h>

#include "interpreter_test_support.hpp"

namespace nitwit::interp::test {
namespace {

class EvaluatorTest : public DataModelTest {
protected:
    [[nodiscard]] bool is64() const { return GetParam() == DataModel::LP64; }

    /// The output of a program whose main body is `body`, after `declarations`; printf is declared.
    std::string print(std::string_view declarations, std::string_view body) {
        return outputOf(std::format("int printf(const char *, ...);\n{}\nint main(void) {{\n{}\nreturn 0;\n}}\n",
                                    declarations, body),
                        GetParam());
    }
};

INSTANTIATE_TEST_SUITE_P(DataModels, EvaluatorTest, ::testing::Values(DataModel::ILP32, DataModel::LP64),
                         dataModelName);

TEST_P(EvaluatorTest, IntegerArithmeticTruncatesTowardZero) {
    EXPECT_EQ(print("", R"(int a = -7, b = 2;
printf("%d %d %d %d %d\n", a + b, a - b, a * b, a / b, a % b);
printf("%d %d\n", 7 / -2, 7 % -2);)"),
              "-5 -9 -14 -3 -1\n-3 1\n");
}

TEST_P(EvaluatorTest, UnsignedArithmeticWrapsAndSmallTypesArePromoted) {
    EXPECT_EQ(print("", R"(unsigned u = 0; unsigned char c = 255;
printf("%u %d %d %u\n", u - 1, c + 1, (unsigned char)(c + 1), -1 / 2u);)"),
              "4294967295 256 0 2147483647\n");
}

TEST_P(EvaluatorTest, UsualArithmeticConversionsDependOnTheDataModel) {
    // C17 6.3.1.8: long and unsigned int meet as long on LP64, as unsigned long on ILP32.
    EXPECT_EQ(
        print("", R"(printf("%d %d %d %u\n", -1 < 1u, (long)-1 < 1u, (long long)-1 < 1u, (unsigned)sizeof(long));)"),
        is64() ? "0 1 1 8\n" : "0 0 1 4\n");
}

TEST_P(EvaluatorTest, ShiftsAreArithmeticForNegativeValues) {
    EXPECT_EQ(print("", R"(int m = -16;
printf("%d %d %u %d %lld\n", 1 << 4, m >> 2, 1u << 31, (unsigned char)0x80 << 1, 1LL << 40);)"),
              "16 -4 2147483648 256 1099511627776\n");
}

TEST_P(EvaluatorTest, BitwiseAndLogicalOperators) {
    EXPECT_EQ(print("", R"(int x = 0xF0, y = 0x3C;
printf("%d %d %d %d %d %d\n", x & y, x | y, x ^ y, ~0, !5, !0);)"),
              "48 252 204 -1 0 1\n");
}

TEST_P(EvaluatorTest, LogicalOperatorsShortCircuit) {
    EXPECT_EQ(print("int calls = 0;\nint f(void) { calls++; return 1; }", R"(int a = 0 && f();
int b = 1 || f();
int c = 1 && f();
printf("%d %d %d %d\n", a, b, c, calls);)"),
              "0 1 1 1\n");
}

TEST_P(EvaluatorTest, FloatingArithmeticAndTruncation) {
    EXPECT_EQ(print("", R"(double d = 7.0 / 2; float f = 1.0f / 3; long double l = 1.0L / 3;
printf("%.1f %.6f %d %d %.10Lf\n", d, f, (int)2.9, (int)-2.9, l);)"),
              "3.5 0.333333 2 -2 0.3333333333\n");
}

TEST_P(EvaluatorTest, IntegerConversionsWrapAndBoolTestsForZero) {
    EXPECT_EQ(
        print("", R"(printf("%d %d %d %d %d %d %u\n", (char)300, (signed char)200, (unsigned char)-1, (short)70000,
       (_Bool)0.5, (_Bool)256, (unsigned)3.99);)"),
        "44 -56 255 4464 1 1 3\n");
}

TEST_P(EvaluatorTest, CompoundAssignmentsComputeInTheComputationType) {
    EXPECT_EQ(print("", R"(char c = 100; c += 100;
int i = 10; i *= 2.5;
unsigned short us = 65535; us++;
_Bool b = 1; b++;
_Bool z = 1; z--;
int s = 1; s <<= 3;
long l = 5; l /= 2;
double d = 1.5; d++;
printf("%d %d %d %d %d %d %ld %.1f\n", c, i, us, b, z, s, l, d);)"),
              "-56 25 0 1 0 8 2 2.5\n");
}

TEST_P(EvaluatorTest, PointerArithmeticMovesByElements) {
    EXPECT_EQ(print("", R"(int a[5] = {1, 2, 3, 4, 5}; int *p = a; p++; p += 2; int *q = &a[4];
printf("%d %d %d\n", *p, (int)(q - p), p < q);
--p;
printf("%d %d\n", *p, p[-1]);
double x[3]; double *r = x + 2;
printf("%d\n", (int)((char *)r - (char *)x));)"),
              "4 1 1\n3 2\n16\n");
}

TEST_P(EvaluatorTest, SizesFollowTheDataModel) {
    EXPECT_EQ(print("struct S { char c; double d; };",
                    R"(printf("%u %u %u %u\n", (unsigned)sizeof(int *), (unsigned)sizeof(long),
       (unsigned)sizeof(long double), (unsigned)sizeof(struct S));)"),
              is64() ? "8 8 16 16\n" : "4 4 12 12\n");
}

TEST_P(EvaluatorTest, BitFieldsTruncateAndSignExtend) {
    EXPECT_EQ(print("struct B { int a : 3; unsigned b : 5; signed char c : 4; };", R"(struct B s = {0};
s.a = 7;
s.b = 33;
s.c = -9;
int r = (s.a = 5);
s.b += 31;
printf("%d %d %d %d %u\n", s.a, s.b, s.c, r, (unsigned)sizeof(struct B));)"),
              "-3 0 7 -3 4\n");
}

TEST_P(EvaluatorTest, StructsAreCopiedAndReturnedByValue) {
    EXPECT_EQ(print(R"(struct P { int x; int y; char name[8]; };
struct P make(int x) { struct P p = {x, x * 2, "pt"}; return p; })",
                    R"(struct P a = make(3); struct P b; b = a; b.x = 10;
printf("%d %d %d %s %d %d\n", a.x, b.x, b.y, b.name, make(4).name[1], make(5).y);)"),
              "3 10 6 pt 116 10\n");
}

TEST_P(EvaluatorTest, UnionsShareTheirBytes) {
    EXPECT_EQ(print("union U { float f; unsigned u; unsigned char bytes[4]; };", R"(union U u; u.f = 1.0f;
printf("%x %d\n", u.u, u.bytes[3]);)"),
              "3f800000 63\n");
}

TEST_P(EvaluatorTest, CommaAndBinaryConditional) {
    EXPECT_EQ(print("", R"(int x = 0, y = 5;
int a = x ?: 7; int b = y ?: 7; int c = (x = 3, x + 1);
printf("%d %d %d\n", a, b, c);)"),
              "7 5 4\n");
}

TEST_P(EvaluatorTest, PointersSignExtendToWiderIntegers) {
    // GCC, "Arrays and pointers": a pointer converted to a wider integer type is sign-extended.
    EXPECT_EQ(print("", R"(long long v = (long long)(char *)0xfffffff0u;
printf("%d\n", v < 0);)"),
              is64() ? "0\n" : "1\n");
}

TEST_P(EvaluatorTest, StringsAndCharacterConstants) {
    EXPECT_EQ(print("", R"(char s[] = "hello"; const char *t = "world" + 1; char u[3] = "abc";
printf("%d %c %s %d %d %d %c\n", (int)sizeof(s), s[1], t, 'a', '\377', (int)sizeof(L"ab"), u[2]);)"),
              "6 e orld 97 -1 12 c\n");
}

TEST_P(EvaluatorTest, StaticInitializersUseAddresses) {
    EXPECT_EQ(print(R"(int a[4] = {1, 2, 3, 4};
int *p = &a[2];
char *s = "abc" + 1;
struct S { int x; int *q; } g = {5, &a[1]};
int (*fp)(const char *, ...) = printf;)",
                    R"(fp("%d %s %d %d\n", *p, s, g.x, *g.q);)"),
              "3 bc 5 2\n");
}

TEST_P(EvaluatorTest, StaticLocalsKeepTheirValues) {
    EXPECT_EQ(print("int counter(void) { static int n = 10; return n++; }", R"(counter(); counter();
printf("%d\n", counter());)"),
              "12\n");
}

TEST_P(EvaluatorTest, FunctionPointers) {
    EXPECT_EQ(print(R"(int twice(int x) { return 2 * x; }
int apply(int (*f)(int), int v) { return f(v); })",
                    R"(int (*fp)(int) = twice;
printf("%d %d %d\n", apply(twice, 4), (*fp)(5), fp == &twice);)"),
              "8 10 1\n");
}

TEST_P(EvaluatorTest, VariadicArgumentsAndVaLists) {
    EXPECT_EQ(print(R"(typedef __builtin_va_list va_list;
int vprintf(const char *, va_list);
int sum(int n, ...) {
    va_list ap; __builtin_va_start(ap, n);
    int s = 0;
    for (int i = 0; i < n; i++) s += __builtin_va_arg(ap, int);
    __builtin_va_end(ap);
    return s;
}
double average(int n, ...) {
    va_list ap; __builtin_va_start(ap, n);
    double s = 0;
    for (int i = 0; i < n; i++) s += __builtin_va_arg(ap, double);
    __builtin_va_end(ap);
    return s / n;
}
int forward(const char *format, ...) {
    va_list ap; __builtin_va_start(ap, format);
    int r = vprintf(format, ap);
    __builtin_va_end(ap);
    return r;
}
long copied(int n, ...) {
    va_list a, b; __builtin_va_start(a, n); __builtin_va_copy(b, a);
    long x = __builtin_va_arg(a, long); long y = __builtin_va_arg(b, long);
    __builtin_va_end(a); __builtin_va_end(b);
    return x + y;
})",
                    R"(printf("%d %.2f\n", sum(3, 1, 2, 3), average(2, 1.0, 2.0f));
forward("%s-%d\n", "x", 5);
printf("%ld\n", copied(1, 21L));)"),
              "6 1.50\nx-5\n42\n");
}

TEST_P(EvaluatorTest, OldStyleDefinitionsConvertTheirArguments) {
    // C17 6.9.1p10: the promoted arguments are converted to the parameter types on entry.
    EXPECT_EQ(print("int kr(a, b) float a; char b; { return (int)(a * 10) + b; }", R"(printf("%d\n", kr(1.5, 300));)"),
              "59\n");
}

TEST_P(EvaluatorTest, AliasesAndWeakFunctions) {
    EXPECT_EQ(print(R"(int real(void) { return 7; }
int other(void) __attribute__((alias("real")));
#pragma weak missing
int missing(void);)",
                    R"(printf("%d %d %d\n", other(), &other == &real, &missing == 0);)"),
              "7 1 1\n");
}

TEST_P(EvaluatorTest, AllocaBlocksBelongToTheFrame) {
    EXPECT_EQ(print("", R"(char *p = __builtin_alloca(8); p[0] = 'a'; p[7] = 'b';
printf("%c%c\n", p[0], p[7]);)"),
              "ab\n");
}

TEST_P(EvaluatorTest, AtomicBuiltinsAreSingleThreadedMemoryOperations) {
    EXPECT_EQ(print("", R"(int x = 5;
int old = __atomic_fetch_add(&x, 3, 5);
int now = __atomic_add_fetch(&x, 2, 5);
int ok = __sync_bool_compare_and_swap(&x, 10, 1);
int expected = 2;
int exchanged = __atomic_compare_exchange_n(&x, &expected, 9, 0, 5, 5);
__atomic_store_n(&x, 4, 5);
printf("%d %d %d %d %d %d\n", old, now, ok, exchanged, expected, __atomic_load_n(&x, 5));)"),
              "5 10 1 0 1 4\n");
}

TEST_P(EvaluatorTest, OverflowBuiltinsComputeExactly) {
    EXPECT_EQ(print("", R"(int r; long long big; unsigned u;
int o1 = __builtin_add_overflow(2147483647, 1, &r);
int o2 = __builtin_mul_overflow(65536LL, 65536LL, &big);
int o3 = __builtin_sub_overflow(0, 1, &u);
signed char c;
int o4 = __builtin_add_overflow(100, 27, &c);
int o5 = __builtin_add_overflow(100, 28, &c);
printf("%d %d %d %lld %d %u %d %d %d\n", o1, r, o2, big, o3, u, o4, o5, c);)"),
              "1 -2147483648 0 4294967296 1 4294967295 0 1 -128\n");
}

TEST_P(EvaluatorTest, ObjectSizesOfKnownObjects) {
    // GCC knows the objects at -O0 only where the pointer is the address of a declared object (sema::knownObjectSize).
    EXPECT_EQ(print("char buffer[10];\nstruct { int a; char b[6]; } s;", R"(char *p = buffer;
printf("%u %u %u %d %u\n", (unsigned)__builtin_object_size(buffer + 4, 0), (unsigned)__builtin_object_size(s.b, 1),
       (unsigned)__builtin_object_size(&s, 0), (int)__builtin_object_size(p, 0), (unsigned)__builtin_object_size(p, 2));)"),
              "6 6 12 -1 0\n");
}

TEST_P(EvaluatorTest, AtomicFlagsAndLocks) {
    EXPECT_EQ(print("", R"(_Bool flag = 0; long lock = 7;
int was = __atomic_test_and_set(&flag, 5);
int again = __atomic_test_and_set(&flag, 5);
__atomic_clear(&flag, 5);
long old = __sync_lock_test_and_set(&lock, 9);
__sync_lock_release(&lock);
printf("%d %d %d %ld %ld\n", was, again, flag, old, lock);)"),
              "0 1 0 7 0\n");
}

TEST_P(EvaluatorTest, ConstantPFoldsIntegerConstantExpressions) {
    EXPECT_EQ(print("", R"(int x = 3;
printf("%d %d\n", __builtin_constant_p(5 * 2), __builtin_constant_p(x));)"),
              "1 0\n");
}

TEST_P(EvaluatorTest, VariableLengthArrays) {
    EXPECT_EQ(print("int last(int n, int (*rows)[n]) { return rows[1][n - 1]; }", R"(int n = 3;
int a[n][n];
a[2][1] = 5;
a[1][2] = 8;
typedef int Row[n];
n = 10;
printf("%d %d %d %d\n", (int)sizeof(a), a[2][1], (int)sizeof(Row), last(3, a));)"),
              "36 5 12 8\n");
}

TEST_P(EvaluatorTest, CompoundLiteralsAreObjects) {
    EXPECT_EQ(print("struct P { int x, y; };\nint *g = (int[]){4, 5, 6};", R"(int *p = (int[]){1, 2, 3};
p[1] = 20;
struct P *q = &(struct P){.y = 7};
q->x += 1;
printf("%d %d %d %d %d\n", p[1], q->x, q->y, g[2], ((char[]){"abc"})[1]);)"),
              "20 1 7 6 98\n");
}

TEST_P(EvaluatorTest, Recursion) {
    EXPECT_EQ(print("int fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }", R"(printf("%d\n", fib(15));)"),
              "610\n");
}

TEST_P(EvaluatorTest, EnumerationsAndGenericSelections) {
    EXPECT_EQ(print("enum E { A = 3, B, C = -1 };", R"(enum E e = B;
printf("%d %d %d %d\n", e, C, _Generic(e, enum E: 1, default: 2), __builtin_choose_expr(1, 10, 20));)"),
              "4 -1 1 10\n");
}

} // namespace
} // namespace nitwit::interp::test
