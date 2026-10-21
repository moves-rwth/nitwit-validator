// Nondeterministic inputs: __VERIFIER_nondet_* calls, functions without definition and uninitialized reads go to the
// oracle; the origins of the values follow unmodified copies.

#include <gtest/gtest.h>

#include "interpreter_test_support.hpp"

namespace nitwit::interp::test {
namespace {

using ::testing::ElementsAre;

/// Builds an expression for the function whose frame is current.
using Builder = std::function<ast::Expr*(const ast::FunctionDecl* function)>;

class InputsTest : public InterpreterTest {
protected:
    /// The origins of the values of `expressions`, evaluated before the statement in line `line` of main, as text:
    /// the input id, or "-" for none.
    std::vector<std::string> originsAt(std::string program, std::uint32_t line, std::vector<Builder> expressions) {
        std::vector<std::string> origins;
        observer_.onStatement = [&](const StatementEvent& event, EvaluationContext& context) {
            if (sources_.lineColumn(event.stmt->location()).line != line) {
                return Directive::Continue;
            }
            for (const Builder& build : expressions) {
                const ast::Expr* expr = analyzed(build(context.function()));
                if (expr == nullptr) {
                    origins.emplace_back("invalid");
                    continue;
                }
                const auto value = context.evaluate(expr);
                if (!value) {
                    origins.push_back("error: " + value.error());
                } else if (value->origin()) {
                    origins.push_back(std::to_string(std::to_underlying(*value->origin())));
                } else {
                    origins.emplace_back("-");
                }
            }
            return Directive::Continue;
        };
        const RunResult result = run(std::move(program));
        EXPECT_EQ(result.termination, Termination::Returned) << result.message;
        return origins;
    }

    /// A reference to a variable of the current function.
    Builder variable(std::string name) {
        return [this, name](const ast::FunctionDecl* function) {
            return ref(name, function);
        };
    }
};

TEST_F(InputsTest, NondetCallsAskTheOracle) {
    oracle_.answers = {-5, 300};
    EXPECT_EQ(status(R"(int __VERIFIER_nondet_int(void);
unsigned char __VERIFIER_nondet_uchar(void);
int main(void) {
    int x = __VERIFIER_nondet_int();
    unsigned char c = __VERIFIER_nondet_uchar();
    return x == -5 && c == 44;
})"),
              1);
    ASSERT_EQ(oracle_.requests.size(), 2);
    const InputRequest& first = oracle_.requests[0];
    EXPECT_EQ(first.kind, InputRequest::Kind::NondetCall);
    EXPECT_EQ(std::to_underlying(first.id), 0);
    EXPECT_EQ(position(first.site->location()), "4:13");
    EXPECT_EQ(first.occurrence, 0);
    EXPECT_EQ(first.function->name().text(), "__VERIFIER_nondet_int");
    EXPECT_EQ(ast::typeToString(first.type), "int");
    EXPECT_EQ(std::to_underlying(oracle_.requests[1].id), 1);
    EXPECT_EQ(ast::typeToString(oracle_.requests[1].type), "unsigned char");
}

TEST_F(InputsTest, EachSiteCountsItsOccurrences) {
    oracle_.answers = {1, 2, 3};
    EXPECT_EQ(status(R"(int __VERIFIER_nondet_int(void);
int main(void) {
    int sum = 0;
    for (int i = 0; i < 3; i++)
        sum = sum * 10 + __VERIFIER_nondet_int();
    return sum;
})"),
              123);
    ASSERT_EQ(oracle_.requests.size(), 3);
    for (std::uint32_t i = 0; i < 3; ++i) {
        EXPECT_EQ(oracle_.requests[i].occurrence, i);
        EXPECT_EQ(oracle_.requests[i].site, oracle_.requests[0].site);
    }
}

TEST_F(InputsTest, FunctionsWithoutDefinitionReturnInputsAndIgnoreTheirArguments) {
    oracle_.answers = {7};
    EXPECT_EQ(status(R"(long unknown(int *);
void nothing(int *);
int main(void) {
    int x = 1;
    nothing(&x);
    return (int)unknown(&x) + x;
})"),
              8);
    ASSERT_EQ(oracle_.requests.size(), 1);
    EXPECT_EQ(oracle_.requests[0].kind, InputRequest::Kind::UndefinedFunction);
    EXPECT_EQ(ast::typeToString(oracle_.requests[0].type), "long");
    EXPECT_EQ(oracle_.requests[0].function->name().text(), "unknown");
}

TEST_F(InputsTest, UninitializedReadsAskOnceAndKeepTheAnswer) {
    oracle_.answers = {21};
    EXPECT_EQ(status(R"(int main(void) {
    int x;
    int y = x;
    return x + y;
})"),
              42);
    ASSERT_EQ(oracle_.requests.size(), 1);
    EXPECT_EQ(oracle_.requests[0].kind, InputRequest::Kind::UninitializedRead);
    EXPECT_EQ(position(oracle_.requests[0].site->location()), "3:13");
    EXPECT_EQ(ast::typeToString(oracle_.requests[0].type), "int");
}

TEST_F(InputsTest, ExpressionsInVoidContextDoNotRead) {
    EXPECT_EQ(status("int main(void) { int x; (void)x; x; return 0; }"), 0);
    EXPECT_TRUE(oracle_.requests.empty());
}

TEST_F(InputsTest, StaticObjectsAndCallocAreInitialized) {
    EXPECT_EQ(status(R"(void *calloc(unsigned long, unsigned long);
int g;
int main(void) { static int s; int *p = calloc(2, sizeof(int)); return g + s + p[1]; })"),
              0);
    EXPECT_TRUE(oracle_.requests.empty());
}

TEST_F(InputsTest, UninitializedHeapMemoryIsAnInput) {
    oracle_.answers = {9};
    EXPECT_EQ(status(R"(void *malloc(unsigned long);
int main(void) { int *p = malloc(2 * sizeof(int)); p[0] = 1; return p[0] + p[1]; })"),
              10);
    ASSERT_EQ(oracle_.requests.size(), 1);
    EXPECT_EQ(oracle_.requests[0].kind, InputRequest::Kind::UninitializedRead);
}

TEST_F(InputsTest, PartlyInitializedObjectsKeepTheWrittenBytes) {
    oracle_.answers = {0x11223344};
    EXPECT_EQ(status(R"(int main(void) {
    union { int i; unsigned char c; } u;
    u.c = 0xAB;
    return u.i == 0x112233AB;
})"),
              1);
    EXPECT_EQ(oracle_.requests.size(), 1);
}

TEST_F(InputsTest, UninitializedBitFieldsAreInputs) {
    oracle_.answers = {5};
    EXPECT_EQ(status(R"(int main(void) {
    struct { unsigned a : 3, b : 4; } s;
    return s.b;
})"),
              5);
    ASSERT_EQ(oracle_.requests.size(), 1);
    EXPECT_EQ(ast::typeToString(oracle_.requests[0].type), "unsigned int");
}

TEST_F(InputsTest, CopiesOfRecordsKeepTheirInitializationState) {
    oracle_.answers = {4};
    EXPECT_EQ(status(R"(struct P { int x, y; };
int main(void) {
    struct P a;
    a.x = 1;
    struct P b = a;
    return b.x + b.y;
})"),
              5);
    ASSERT_EQ(oracle_.requests.size(), 1);
    EXPECT_EQ(position(oracle_.requests[0].site->location()), "6:18");
}

TEST_F(InputsTest, NondetMemoryAsksForEveryByte) {
    oracle_.answers = {1, 2, 3, 4};
    EXPECT_EQ(status(R"(void __VERIFIER_nondet_memory(void *, unsigned long);
int main(void) {
    unsigned char bytes[4];
    __VERIFIER_nondet_memory(bytes, sizeof bytes);
    return bytes[0] + bytes[1] * 10 + bytes[3] * 100;
})"),
              421);
    ASSERT_EQ(oracle_.requests.size(), 4);
    for (const InputRequest& request : oracle_.requests) {
        EXPECT_EQ(request.kind, InputRequest::Kind::NondetCall);
        EXPECT_EQ(ast::typeToString(request.type), "unsigned char");
    }
}

TEST_F(InputsTest, FloatingInputsHaveTheirType) {
    oracle_.answers = {3};
    EXPECT_EQ(status(R"(double __VERIFIER_nondet_double(void);
int main(void) { double d = __VERIFIER_nondet_double(); return (int)(d * 2); })"),
              6);
    EXPECT_EQ(ast::typeToString(oracle_.requests[0].type), "double");
}

TEST_F(InputsTest, OriginsFollowUnmodifiedCopies) {
    EXPECT_THAT(originsAt(R"(int __VERIFIER_nondet_int(void);
int id(int v) { return v; }
int main(void) {
    int x = __VERIFIER_nondet_int();
    int y = x;
    long z = x;
    char c = x;
    int w = x + 0;
    int r = id(x);
    unsigned u = x;
    return 0;
})",
                          11,
                          {variable("x"), variable("y"), variable("z"), variable("c"), variable("w"), variable("r"),
                           variable("u"),
                           [&](const ast::FunctionDecl* f) { return castTo(ast::BuiltinKind::LongLong, ref("x", f)); },
                           [&](const ast::FunctionDecl* f) {
                               return castTo(ast::BuiltinKind::Double, ref("x", f));
                           }}),
                ElementsAre("0", "0", "0", "-", "-", "0", "-", "0", "0"));
}

TEST_F(InputsTest, OriginsMoveWithCopiedRecords) {
    EXPECT_THAT(originsAt(R"(int __VERIFIER_nondet_int(void);
struct P { int a; int b; };
int main(void) {
    struct P p;
    p.a = 1;
    p.b = __VERIFIER_nondet_int();
    struct P q = p;
    q.a = __VERIFIER_nondet_int();
    return 0;
})",
                          9,
                          {[&](const ast::FunctionDecl* f) { return member(ref("q", f), "b"); },
                           [&](const ast::FunctionDecl* f) { return member(ref("q", f), "a"); },
                           [&](const ast::FunctionDecl* f) {
                               return member(ref("p", f), "a");
                           }}),
                ElementsAre("0", "1", "-"));
}

TEST_F(InputsTest, UninitializedReadsHaveOrigins) {
    EXPECT_THAT(originsAt(R"(int main(void) {
    int x;
    int y = x;
    return 0;
})",
                          4, {variable("y")}),
                ElementsAre("0"));
}

} // namespace
} // namespace nitwit::interp::test
