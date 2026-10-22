// Terms (interp/README.md, "Terms"): the term the interpreter records for each value derived from an input, opaque
// values, the terms of bytes in memory, the terms of conditions, limits, and the consistency check. Every run checks
// its terms (RunOptions::checkTerms).

#include <array>
#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "interp/execution.hpp"
#include "interpreter_test_support.hpp"

namespace nitwit::interp::test {
namespace {

using ::testing::ElementsAre;
using ::testing::HasSubstr;
using value::TermKind;

/// Builds an expression for the function whose frame is current.
using Builder = std::function<ast::Expr*(const ast::FunctionDecl* function)>;

std::string formatName(const value::ScalarFormat& format) {
    if (const auto* integer = std::get_if<value::IntegerFormat>(&format)) {
        return std::format("{}{}", integer->isSigned ? 's' : 'u', integer->bits);
    }
    switch (std::get<value::FloatFormat>(format)) {
    case value::FloatFormat::Float:
        return "float";
    case value::FloatFormat::Double:
        return "double";
    case value::FloatFormat::LongDouble:
        return "long double";
    }
    return "?";
}

/// A term as text: inputs as x0, x1, ...; constants as their values; operations in C syntax; conversions as casts to
/// formats, such as (s64) or (double); the operand of `&&`, `||` or `?:` that the run did not evaluate as `_`.
std::string termText(const value::TermTable& table, value::TermId id) {
    static constexpr std::array<std::string_view, 8> integerOperators = {"+", "-", "*", "/", "%", "&", "|", "^"};
    static constexpr std::array<std::string_view, 4> floatOperators = {"+", "-", "*", "/"};
    static constexpr std::array<std::string_view, 6> comparisons = {"==", "!=", "<", "<=", ">", ">="};
    const value::Term& term = table.term(id);
    const auto operand = [&](std::size_t i) {
        return i < term.operandCount ? termText(table, term.operands[i]) : "_";
    };
    switch (term.kind) {
    case TermKind::Input:
        return std::format("x{}", std::to_underlying(table.inputOf(term)));
    case TermKind::Constant:
        return valueText(table.constantOf(term));
    case TermKind::Opaque:
        return "opaque";
    case TermKind::IntegerBinary:
        return std::format("({} {} {})", operand(0), integerOperators.at(term.op), operand(1));
    case TermKind::Shift:
        return std::format("({} {} {})", operand(0), term.op == 0 ? "<<" : ">>", operand(1));
    case TermKind::IntegerNegate:
    case TermKind::FloatNegate:
        return "-" + operand(0);
    case TermKind::IntegerComplement:
        return "~" + operand(0);
    case TermKind::IntegerCompare:
    case TermKind::FloatCompare:
        return std::format("({} {} {})", operand(0), comparisons.at(term.op), operand(1));
    case TermKind::FloatBinary:
        return std::format("({} {} {})", operand(0), floatOperators.at(term.op), operand(1));
    case TermKind::ConvertInteger:
    case TermKind::IntegerToFloat:
    case TermKind::FloatToInteger:
    case TermKind::ConvertFloat:
        return std::format("({}){}", formatName(term.secondFormat), operand(0));
    case TermKind::ToBool:
        return "(bool)" + operand(0);
    case TermKind::LogicalNot:
        return "!" + operand(0);
    case TermKind::LogicalAnd:
        return std::format("({} && {})", operand(0), operand(1));
    case TermKind::LogicalOr:
        return std::format("({} || {})", operand(0), operand(1));
    case TermKind::Conditional:
        return term.op == 1 ? std::format("({} ? {} : _)", operand(0), operand(1))
                            : std::format("({} ? _ : {})", operand(0), operand(1));
    }
    return "?";
}

std::string termText(const value::TermTable& table, std::optional<value::TermId> id) {
    return id ? termText(table, *id) : "-";
}

class TermsTest : public InterpreterTest {
protected:
    /// Runs `text` with the check of the terms; it must return normally.
    RunResult runChecked(std::string text, DataModel model = DataModel::LP64, RunOptions options = {}) {
        options.checkTerms = true;
        RunResult result = run(std::move(text), model, options);
        EXPECT_EQ(result.termination, Termination::Returned) << result.message;
        return result;
    }

    /// The terms of the values of `expressions`, evaluated before the statement in line `line` (each time it is
    /// reached), as text (termText; "-" for none).
    std::vector<std::string> termsAt(std::string program, std::uint32_t line, std::vector<Builder> expressions,
                                     DataModel model = DataModel::LP64, RunOptions options = {}) {
        std::vector<std::string> terms;
        observer_.onStatement = [&](const StatementEvent& event, EvaluationContext& context) {
            if (sources_.lineColumn(event.stmt->location()).line != line) {
                return Directive::Continue;
            }
            for (const Builder& build : expressions) {
                const ast::Expr* expr = analyzed(build(context.function()));
                if (expr == nullptr) {
                    terms.emplace_back("invalid");
                    continue;
                }
                const auto value = context.evaluate(expr);
                terms.push_back(value ? termText(context.terms(), value->term()) : "error: " + value.error());
            }
            return Directive::Continue;
        };
        runChecked(std::move(program), model, options);
        return terms;
    }

    /// A reference to a variable of the current function, or a global.
    Builder variable(std::string name) {
        return [this, name](const ast::FunctionDecl* function) {
            return ref(name, function);
        };
    }

    /// The member `name` of the struct or union variable `record`.
    Builder memberOf(std::string record, std::string name) {
        return [this, record, name](const ast::FunctionDecl* function) {
            return member(ref(record, function), name);
        };
    }
};

// ---- Inputs and copies ----

TEST_F(TermsTest, InputsAreRegisteredWithTheirValues) {
    oracle_.answers = {7, 8};
    std::vector<std::string> inputs;
    observer_.onStatement = [&](const StatementEvent&, EvaluationContext& context) {
        inputs.clear();
        for (std::size_t id = 0; id < context.terms().size(); ++id) {
            const value::Term& term = context.terms().term(value::TermId{static_cast<std::uint32_t>(id)});
            if (term.kind == TermKind::Input) {
                inputs.push_back(
                    std::format("x{} {}", std::to_underlying(context.terms().inputOf(term)), formatName(term.format)));
            }
        }
        return Directive::Continue;
    };
    runChecked(R"(unsigned char __VERIFIER_nondet_uchar(void);
int main(void) {
    unsigned char c = __VERIFIER_nondet_uchar();
    long l;
    long m = l;
    return 0;
})");
    EXPECT_THAT(inputs, ElementsAre("x0 u8", "x1 s64"));
}

TEST_F(TermsTest, CopiesKeepTheTerm) {
    EXPECT_THAT(termsAt(R"(int __VERIFIER_nondet_int(void);
int g;
int id(int v) { return v; }
int main(void) {
    int x = __VERIFIER_nondet_int();
    int y = x;
    g = y;
    int r = id(x);
    int *p = &x;
    int d = *p;
    int u;
    int v = u;
    return 0;
})",
                        13, {variable("x"), variable("y"), variable("g"), variable("r"), variable("d"), variable("v")}),
                ElementsAre("x0", "x0", "x0", "x0", "x0", "x1"));
}

TEST_F(TermsTest, RecordsKeepTheTermsOfTheirMembers) {
    EXPECT_THAT(termsAt(R"(int __VERIFIER_nondet_int(void);
struct P { int a; int b; };
int main(void) {
    struct P p;
    p.a = 1;
    p.b = __VERIFIER_nondet_int();
    struct P q = p;
    q.a = __VERIFIER_nondet_int();
    struct P r = 1 ? q : p;
    return 0;
})",
                        10, {memberOf("q", "b"), memberOf("q", "a"), memberOf("p", "a"), memberOf("r", "a")}),
                ElementsAre("x0", "x1", "-", "x1"));
}

constexpr const char* recordProgram = R"(int __VERIFIER_nondet_int(void);
struct S { int a; int b; };
struct S id(struct S s) { return s; }
int g(struct S s) {
    return s.a;
}
int main(void) {
    struct S s;
    s.a = __VERIFIER_nondet_int();
    struct S r = id(s);
    int v = g(s);
    return 0;
})";

TEST_F(TermsTest, RecordArgumentsKeepTermsAndInitializationState) {
    // In g, s.a is still the input, and s.b is still uninitialized: reading it is another input.
    EXPECT_THAT(termsAt(recordProgram, 5, {memberOf("s", "a"), memberOf("s", "b")}), ElementsAre("x0", "x1"));
    ASSERT_EQ(oracle_.requests.size(), 2);
    EXPECT_EQ(oracle_.requests[1].kind, InputRequest::Kind::UninitializedRead);
}

TEST_F(TermsTest, RecordResultsKeepTerms) {
    EXPECT_THAT(termsAt(recordProgram, 12, {memberOf("r", "a")}), ElementsAre("x0"));
}

// ---- Operations ----

TEST_F(TermsTest, ConversionsAreRecorded) {
    EXPECT_THAT(
        termsAt(R"(int __VERIFIER_nondet_int(void);
double __VERIFIER_nondet_double(void);
int main(void) {
    int x = __VERIFIER_nondet_int();
    double d = __VERIFIER_nondet_double();
    long l = x;
    unsigned char c = x;
    _Bool b = x;
    float f = d;
    int i = d;
    double e = x;
    long w = __builtin_expect(x, 0);
    return 0;
})",
                13,
                {variable("l"), variable("c"), variable("b"), variable("f"), variable("i"), variable("e"),
                 variable("w"),
                 [&](const ast::FunctionDecl* f) {
                     return castTo(ast::BuiltinKind::Short, ref("x", f));
                 }}),
        ElementsAre("(s64)x0", "(u8)x0", "(bool)x0", "(float)x1", "(s32)x1", "(double)x0", "(s64)x0", "(s16)x0"));
}

TEST_F(TermsTest, PointersConvertedToWiderIntegersAreSignExtended) {
    // GCC sign-extends: 0x80000000 becomes 0xffffffff80000000 (interp/README.md, "Conversions").
    oracle_.answers = {0x80000000};
    EXPECT_THAT(termsAt(R"(void *__VERIFIER_nondet_pointer(void);
int main(void) {
    void *p = __VERIFIER_nondet_pointer();
    long long l = (long long)p;
    unsigned long long u = (unsigned long long)p;
    unsigned i = (unsigned)p;
    _Bool b = p;
    return l == -2147483648LL && u == 0xffffffff80000000ULL;
})",
                        8, {variable("l"), variable("u"), variable("i"), variable("b")}, DataModel::ILP32),
                ElementsAre("(s64)(s32)x0", "(u64)(s32)x0", "x0", "(bool)x0"));
}

TEST_F(TermsTest, ArithmeticIsRecorded) {
    oracle_.answers = {2147483647};
    EXPECT_THAT(termsAt(R"(int __VERIFIER_nondet_int(void);
int main(void) {
    int x = __VERIFIER_nondet_int();
    int a = x * 2 + 1;
    int s = x >> 3;
    int n = -x;
    int t = ~x;
    int q = x / 3;
    int m = x;
    m += 5;
    m++;
    char k = x;
    k--;
    return 0;
})",
                        14,
                        {variable("a"), variable("s"), variable("n"), variable("t"), variable("q"), variable("m"),
                         variable("k")}),
                ElementsAre("((x0 * 2) + 1)", "(x0 >> 3)", "-x0", "~x0", "(x0 / 3)", "((x0 + 5) + 1)",
                            "(s8)((s32)(s8)x0 - 1)"));
    // Signed overflow in `x * 2` and `m += 5`: the terms give the values the run continues with, as the check
    // confirmed.
    EXPECT_EQ(observer_.only("ub signed overflow").size(), 2u);
}

TEST_F(TermsTest, FloatingArithmeticIsRecorded) {
    oracle_.answers = {3};
    EXPECT_THAT(termsAt(R"(double __VERIFIER_nondet_double(void);
int main(void) {
    double d = __VERIFIER_nondet_double();
    double e = d * 2.0 - 0.5;
    double n = -d;
    float f = d;
    f++;
    return 0;
})",
                        8, {variable("e"), variable("n"), variable("f")}),
                ElementsAre("((x0 * 2) - 0.5)", "-x0", "((float)x0 + 1)"));
}

TEST_F(TermsTest, ComparisonsAndLogicalOperatorsAreRecorded) {
    oracle_.answers = {0, 5};
    EXPECT_THAT(termsAt(R"(int __VERIFIER_nondet_int(void);
int main(void) {
    int x = __VERIFIER_nondet_int();
    int y = __VERIFIER_nondet_int();
    int c = x < y;
    int e = (double)y == 5.0;
    int a = x && y;
    int o = x || y;
    int k = 1 && y;
    int n = !y;
    return 0;
})",
                        11, {variable("c"), variable("e"), variable("a"), variable("o"), variable("k"), variable("n")}),
                ElementsAre("(x0 < x1)", "((double)x1 == 5)", "(x0 && _)", "(x0 || x1)", "(1 && x1)", "!x1"));
}

TEST_F(TermsTest, ConditionalExpressionsAreRecorded) {
    oracle_.answers = {2, 5};
    EXPECT_THAT(termsAt(R"(int __VERIFIER_nondet_int(void);
int main(void) {
    int x = __VERIFIER_nondet_int();
    int y = __VERIFIER_nondet_int();
    int r = x ? y : 7;
    int t = x ? 7 : y;
    int f = !x ? y : 7;
    int s = 1 ? y : 7;
    int g = x ?: 3;
    return 0;
})",
                        10, {variable("r"), variable("t"), variable("f"), variable("s"), variable("g")}),
                ElementsAre("(x0 ? x1 : _)", "(x0 ? 7 : _)", "(!x0 ? _ : 7)", "x1", "(x0 ? x0 : _)"));
}

// ---- Opaque values ----

TEST_F(TermsTest, PartialOverwritesMakeTheRestOpaque) {
    oracle_.answers = {0x11223344, 0x11};
    EXPECT_THAT(termsAt(R"(int __VERIFIER_nondet_int(void);
int main(void) {
    int x = __VERIFIER_nondet_int();
    ((unsigned char *)&x)[1] = 0;
    int y = x;
    union { int i; unsigned char c; } u;
    u.c = 0xAB;
    int v = u.i;
    int w = __VERIFIER_nondet_int();
    unsigned char low = *(unsigned char *)&w;
    return 0;
})",
                        11, {variable("y"), variable("v"), variable("low")}),
                ElementsAre("opaque", "opaque", "opaque"));
}

TEST_F(TermsTest, TypePunningBetweenKindsIsOpaque) {
    EXPECT_THAT(termsAt(R"(float __VERIFIER_nondet_float(void);
int __VERIFIER_nondet_int(void);
union U { float f; int i; unsigned u; };
int main(void) {
    union U v;
    v.f = __VERIFIER_nondet_float();
    int i = v.i;
    v.i = __VERIFIER_nondet_int();
    unsigned u = v.u;
    float f = v.f;
    return 0;
})",
                        11, {variable("i"), variable("u"), variable("f")}),
                ElementsAre("opaque", "(u32)x1", "opaque"));
}

TEST_F(TermsTest, LibraryFunctionsAreOpaque) {
    oracle_.answers = {-65};
    EXPECT_THAT(termsAt(R"(int __VERIFIER_nondet_int(void);
int abs(int);
unsigned long strlen(const char *);
char *strcpy(char *, const char *);
int sprintf(char *, const char *, ...);
void *memcpy(void *, const void *, unsigned long);
void *memset(void *, int, unsigned long);
int main(void) {
    int x = __VERIFIER_nondet_int();
    int a = abs(x);
    char s[4] = "ab";
    s[0] = x;
    unsigned long n = strlen(s);
    char t[4];
    strcpy(t, s);
    char t0 = t[0], t1 = t[1];
    char u[16];
    sprintf(u, "%d", x);
    char u0 = u[0];
    int c;
    memcpy(&c, &x, sizeof x);
    int z = x;
    memset(&z, 0, sizeof z);
    int k;
    memset(&k, x, sizeof k);
    return 0;
})",
                        26,
                        {variable("a"), variable("n"), variable("t0"), variable("t1"), variable("u0"), variable("c"),
                         variable("z"), variable("k")}),
                // Copying functions copy the terms of the bytes, as memcpy does.
                ElementsAre("opaque", "opaque", "(s8)x0", "-", "opaque", "x0", "-", "opaque"));
}

TEST_F(TermsTest, LibraryFunctionsThatWritePointerValuesMakeThemOpaque) {
    oracle_.answers = {1};
    EXPECT_THAT(
        termsAt(R"(int __VERIFIER_nondet_int(void);
int sprintf(char *, const char *, ...);
long strtol(const char *, char **, int);
char *strchr(const char *, int);
int main(void) {
    int i = __VERIFIER_nondet_int();
    char s[8] = "1a2b3";
    char buf[32];
    sprintf(buf, "%p", (void *)(s + i));
    char b = buf[3];
    sprintf(buf, "%s", s + i);
    char t = buf[0];
    char *end;
    strtol(s + i, &end, 10);
    long n = end - s;
    char *found = strchr(s + i, 'b');
    char *fixed;
    strtol(s, &fixed, 10);
    return 0;
})",
                19,
                {variable("b"), variable("t"), variable("end"), variable("n"), variable("found"), variable("fixed")}),
        // `%s` prints the bytes the pointer points to, which depend on no input.
        ElementsAre("opaque", "-", "opaque", "opaque", "opaque", "-"));
}

TEST_F(TermsTest, PointerArithmeticWithInputsIsOpaque) {
    oracle_.answers = {2};
    EXPECT_THAT(
        termsAt(R"(int __VERIFIER_nondet_int(void);
struct S { int a; int b; };
int main(void) {
    int a[4] = {1, 2, 3, 4};
    int i = __VERIFIER_nondet_int();
    int *p = a + i;
    int *q = &a[i];
    long d = q - a;
    int v = a[i];
    struct S *s = (struct S *)p;
    int *first = &s->a;
    int *second = &s->b;
    return 0;
})",
                13,
                {variable("p"), variable("q"), variable("d"), variable("v"), variable("first"), variable("second")}),
        // The address of an object is no dependency of its value (interp/README.md, "Terms").
        ElementsAre("opaque", "opaque", "opaque", "-", "opaque", "opaque"));
}

TEST_F(TermsTest, BitFieldsOfInputsAreOpaque) {
    oracle_.answers = {5, 1 << 3};
    EXPECT_THAT(termsAt(R"(int __VERIFIER_nondet_int(void);
struct B { unsigned a : 3, b : 4; };
int main(void) {
    struct B s;
    s.a = __VERIFIER_nondet_int();
    s.b = 2;
    unsigned a = s.a;
    unsigned b = s.b;
    struct B t;
    unsigned c = t.b;
    unsigned d = (s.b = 3);
    return 0;
})",
                        12, {variable("a"), variable("b"), variable("c"), variable("d")}),
                ElementsAre("opaque", "opaque", "opaque", "-"));
}

TEST_F(TermsTest, VariableLengthArraysOfInputSizeHaveOpaqueSizes) {
    oracle_.answers = {3};
    EXPECT_THAT(termsAt(R"(int __VERIFIER_nondet_int(void);
int main(void) {
    int n = __VERIFIER_nondet_int();
    int m[2][n];
    unsigned long size = sizeof m;
    int *row = m[1];
    return 0;
})",
                        7, {variable("size"), variable("row")}),
                ElementsAre("opaque", "opaque"));
}

// ---- Conditions ----

TEST_F(TermsTest, ConditionEventsCarryTheTermOfTheControllingExpression) {
    oracle_.answers = {7};
    std::vector<std::string> conditions;
    observer_.onCondition = [&](const ConditionEvent& event, EvaluationContext& context) {
        conditions.push_back(termText(context.terms(), event.term));
        if (event.construct == ConditionEvent::Construct::Switch) {
            EXPECT_EQ(event.switchValue.term(), event.term);
        }
        return Directive::Continue;
    };
    runChecked(R"(int __VERIFIER_nondet_int(void);
int main(void) {
    int x = __VERIFIER_nondet_int();
    char c = x;
    if (x > 5) {}
    while (x < 3) {}
    do {} while (!x);
    for (;;) break;
    for (int i = 0; i < 1; i++) {}
    switch (c) { case 7: break; }
    int r = x ? 1 : 2;
    return 0;
})");
    EXPECT_THAT(conditions, ElementsAre("(x0 > 5)", "(x0 < 3)", "!x0", "-", "-", "-", "(s32)(s8)x0", "x0"));
}

// ---- Limits ----

TEST_F(TermsTest, ResultsBeyondTheDepthLimitAreOpaque) {
    RunOptions options;
    options.terms.maxDepth = 3;
    EXPECT_THAT(termsAt(R"(int __VERIFIER_nondet_int(void);
int main(void) {
    int x = __VERIFIER_nondet_int();
    int y = x + 1 + 2 + 3;
    int z = x + 1 + 2 + 3 + 4;
    return 0;
})",
                        6, {variable("y"), variable("z")}, DataModel::LP64, options),
                ElementsAre("(((x0 + 1) + 2) + 3)", "opaque"));
}

TEST_F(TermsTest, WithoutTermsNothingIsRecorded) {
    RunOptions options;
    options.terms.maxTerms = 0;
    std::size_t terms = 1;
    observer_.onCall = [&](const CallEvent&, EvaluationContext& context) {
        terms = context.terms().size();
        return Directive::Continue;
    };
    EXPECT_THAT(termsAt(R"(int __VERIFIER_nondet_int(void);
int abs(int);
int main(void) {
    int x = __VERIFIER_nondet_int();
    int y = abs(x + 1);
    int *p = &x + x;
    return 0;
})",
                        7, {variable("x"), variable("y"), variable("p")}, DataModel::LP64, options),
                ElementsAre("-", "-", "-"));
    EXPECT_EQ(terms, 0u);
}

TEST_F(TermsTest, TheTermsOfConstraintsAreStrictAndThoseOfTheProgramNot) {
    std::optional<bool> isConstraintStrict;
    std::optional<bool> isProgramStrict;
    observer_.onStatement = [&](const StatementEvent& event, EvaluationContext& context) {
        const std::uint32_t line = sources_.lineColumn(event.stmt->location()).line;
        ast::Expr* expr = line == 4 ? binary(ast::BinaryOperator::Add, ref("x", context.function()), integer(1))
                                    : ref("y", context.function());
        if (line == 4 || line == 5) {
            const auto value = context.evaluate(analyzed(expr));
            if (value && value->term()) {
                (line == 4 ? isConstraintStrict : isProgramStrict) = context.terms().term(*value->term()).strict;
            }
        }
        return Directive::Continue;
    };
    runChecked(R"(int __VERIFIER_nondet_int(void);
int main(void) {
    int x = __VERIFIER_nondet_int();
    int y = x + 1;
    return y > 0;
})");
    EXPECT_EQ(isConstraintStrict, true);
    // The program computes x + 1 after the constraint was evaluated: its term is not strict.
    EXPECT_EQ(isProgramStrict, false);
}

// ---- The consistency check ----

TEST(TermMismatchTest, ATermThatGivesAnotherValueIsReportedWithItsOperation) {
    constexpr value::IntegerFormat s32{.bits = 32, .isSigned = true};
    constexpr value::IntegerFormat u32{.bits = 32, .isSigned = false};
    value::TermTable table(DataModel::LP64);
    value::Value x = value::Value::integer(static_cast<UInt128>(Int128{-2}), s32);
    x.setTerm(table.input(value::InputId{0}, x, s32));
    const std::optional<value::TermId> sum =
        table.integerBinary(value::IntegerOperator::Add, x, value::Value::integer(1, s32), s32);
    ASSERT_TRUE(sum.has_value());
    const value::Value minusOne = value::Value::integer(static_cast<UInt128>(Int128{-1}), s32);
    EXPECT_EQ(termMismatch(table, *sum, minusOne), std::nullopt);
    const std::optional<std::string> mismatch = termMismatch(table, *sum, value::Value::integer(7, s32));
    ASSERT_TRUE(mismatch.has_value());
    EXPECT_THAT(*mismatch, HasSubstr("the term of integer + gives 0xffffffffffffffffffffffffffffffff, but the run "
                                     "computed 0x7"));
    // The same bits in another format are another canonical value: a term recorded with the wrong format.
    EXPECT_TRUE(termMismatch(table, *sum, value::Value::integer(0xFFFFFFFF, u32)).has_value());
    EXPECT_TRUE(termMismatch(table, *x.term(), value::Value::ofDouble(-2.0)).has_value());
    // The opaque term stands for any value.
    EXPECT_EQ(termMismatch(table, *table.opaque(), value::Value::integer(7, s32)), std::nullopt);
}

TEST(TermMismatchTest, FloatingValuesAreComparedBitForBit) {
    value::TermTable table(DataModel::LP64);
    value::Value d = value::Value::ofDouble(0.0);
    d.setTerm(table.input(value::InputId{0}, d, value::FloatFormat::Double));
    const value::Value negated = value::floatNegate(d, value::FloatFormat::Double);
    const std::optional<value::TermId> term = table.floatNegate(d, value::FloatFormat::Double);
    ASSERT_TRUE(term.has_value());
    EXPECT_EQ(termMismatch(table, *term, negated), std::nullopt);
    EXPECT_TRUE(termMismatch(table, *term, value::Value::ofDouble(0.0)).has_value()); // -0.0 is not 0.0
}

TEST_F(TermsTest, TheCheckPassesForRunsThatEndAtUndefinedBehaviour) {
    oracle_.answers = {0};
    RunOptions options;
    options.checkTerms = true;
    const RunResult result = run(R"(int __VERIFIER_nondet_int(void);
int main(void) {
    int x = __VERIFIER_nondet_int();
    return 10 / x;
})",
                                 DataModel::LP64, options);
    EXPECT_EQ(result.termination, Termination::UndefinedBehavior);
    EXPECT_EQ(result.message, "division by zero");
}

} // namespace
} // namespace nitwit::interp::test
