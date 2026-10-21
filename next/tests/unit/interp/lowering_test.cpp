// The lowering of statements, seen through the events of a run: statements, conditions, jumps, calls.

#include <gtest/gtest.h>

#include "interpreter_test_support.hpp"

namespace nitwit::interp::test {
namespace {

using ::testing::ElementsAre;

class LoweringTest : public InterpreterTest {};

TEST_F(LoweringTest, IfReportsItsConditionAtTheKeyword) {
    EXPECT_EQ(status(R"(int main(void) {
    int x = 1;
    if (x)
        x = 2;
    else
        x = 3;
    return x;
})"),
              2);
    EXPECT_THAT(observer_.events, ElementsAre("S 2:5", "S 3:5", "C if 3:5 true", "S 4:9", "S 7:5"));
}

TEST_F(LoweringTest, ElseBranchRunsWhenTheConditionIsFalse) {
    EXPECT_EQ(status(R"(int main(void) {
    int x = 0;
    if (x)
        x = 2;
    else
        x = 3;
    return x;
})"),
              3);
    EXPECT_THAT(observer_.events, ElementsAre("S 2:5", "S 3:5", "C if 3:5 false", "S 6:9", "S 7:5"));
}

TEST_F(LoweringTest, WhileReportsEveryEvaluationOfItsCondition) {
    EXPECT_EQ(status(R"(int main(void) {
    int i = 0;
    while (i < 2) {
        i++;
    }
    return i;
})"),
              2);
    EXPECT_THAT(observer_.events, ElementsAre("S 2:5", "S 3:5", "C while 3:5 true", "S 4:9", "C while 3:5 true",
                                              "S 4:9", "C while 3:5 false", "S 6:5"));
}

TEST_F(LoweringTest, DoWhileReportsItsConditionAtTheWhileKeyword) {
    EXPECT_EQ(status(R"(int main(void) {
    int i = 0;
    do
        i++;
    while (i < 2);
    return i;
})"),
              2);
    EXPECT_THAT(observer_.events,
                ElementsAre("S 2:5", "S 3:5", "S 4:9", "C do 5:5 true", "S 4:9", "C do 5:5 false", "S 6:5"));
}

TEST_F(LoweringTest, ForWithoutConditionReportsTrueAndHonoursBreakAndContinue) {
    EXPECT_EQ(status(R"(int main(void) {
    int n = 0;
    for (int i = 0;; i++) {
        if (i == 1)
            continue;
        if (i == 3)
            break;
        n += 10;
    }
    return n;
})"),
              20);
    EXPECT_THAT(observer_.events,
                ElementsAre("S 2:5", "S 3:5", "C for 3:5 true",                                              // i = 0
                            "S 4:9", "C if 4:9 false", "S 6:9", "C if 6:9 false", "S 8:9", "C for 3:5 true", // i = 1
                            "S 4:9", "C if 4:9 true", "S 5:13", "C for 3:5 true",                            // i = 2
                            "S 4:9", "C if 4:9 false", "S 6:9", "C if 6:9 false", "S 8:9", "C for 3:5 true", // i = 3
                            "S 4:9", "C if 4:9 false", "S 6:9", "C if 6:9 true", "S 7:13", "S 10:5"));
}

TEST_F(LoweringTest, SwitchReportsTheValueAndTheLabelAndFallsThrough) {
    EXPECT_EQ(status(R"(int f(int x) {
    switch (x) {
    case 1:
        return 10;
    case 2:
    case 3:
        x = 30;
        break;
    default:
        return 0;
    }
    return x;
}
int main(void) { return f(1) + f(3) + f(7); })"),
              40);
    EXPECT_THAT(observer_.only("C"), ElementsAre("C switch 2:5 1 3:5", "C switch 2:5 3 6:5", "C switch 2:5 7 9:5"));
    // f(3): the case label, then its statement, then the statements after it.
    const auto& events = observer_.events;
    const auto second = std::ranges::find(events, std::string("C switch 2:5 3 6:5"));
    ASSERT_NE(second, events.end());
    EXPECT_THAT(std::vector(second + 1, second + 5), ElementsAre("S 6:5", "S 7:9", "S 8:9", "S 12:5"));
}

TEST_F(LoweringTest, SwitchWithoutMatchAndDefaultSkipsTheBody) {
    EXPECT_EQ(status(R"(int main(void) {
    int x = 5;
    switch (x) {
    case 1:
        x = 1;
    }
    return x;
})"),
              5);
    EXPECT_THAT(observer_.events, ElementsAre("S 2:5", "S 3:5", "C switch 3:5 5 none", "S 7:5"));
}

TEST_F(LoweringTest, SwitchCaseRangesAndNestedBlocks) {
    EXPECT_EQ(status(R"(int f(int x) {
    switch (x) {
        { case 1 ... 5: return 1; }
    case 'a':
        return 2;
    }
    return 3;
}
int main(void) { return f(3) * 100 + f('a') * 10 + f(6); })"),
              123);
}

TEST_F(LoweringTest, GotoJumpsToTheLabeledStatement) {
    EXPECT_EQ(status(R"(int main(void) {
    int i = 0;
again:
    i++;
    if (i < 2)
        goto again;
    return i;
})"),
              2);
    EXPECT_THAT(observer_.events, ElementsAre("S 2:5", "S 3:1", "S 4:5", "S 5:5", "C if 5:5 true", "S 6:9", "S 3:1",
                                              "S 4:5", "S 5:5", "C if 5:5 false", "S 7:5"));
}

TEST_F(LoweringTest, GotoIntoAnotherBlockAndOutOfLoops) {
    EXPECT_EQ(status(R"(int main(void) {
    int n = 0;
    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 10; j++) {
            if (i * j == 6)
                goto done;
            n++;
        }
    }
done:
    return n;
})"),
              16);
}

TEST_F(LoweringTest, ConditionalOperatorReportsItsQuestionMark) {
    EXPECT_EQ(status(R"(int main(void) {
    int x = 5;
    return x > 3 ? 1 : 2;
})"),
              1);
    EXPECT_THAT(observer_.only("C"), ElementsAre("C ?: 3:18 true"));
}

TEST_F(LoweringTest, BlocksReportNoStatementEvent) {
    EXPECT_EQ(status(R"(int main(void) {
    { { int x = 1; } }
    ;
    return 0;
})"),
              0);
    EXPECT_THAT(observer_.events, ElementsAre("S 2:9", "S 3:5", "S 4:5"));
}

TEST_F(LoweringTest, CallsReportArgumentsAndResults) {
    oracle_.answers = {42};
    EXPECT_EQ(status(R"(int add(int a, int b) { return a + b; }
int __VERIFIER_nondet_int(void);
void nothing(void) {}
int main(void) { nothing(); return add(1, __VERIFIER_nondet_int()); })"),
              43);
    EXPECT_THAT(observer_.only("call"),
                ElementsAre("call nothing()", "call __VERIFIER_nondet_int()", "call add(1, 42)"));
    EXPECT_THAT(observer_.only("return"),
                ElementsAre("return nothing none", "return __VERIFIER_nondet_int 42", "return add 43"));
}

TEST_F(LoweringTest, LibraryCallsAreReportedToo) {
    run(R"(int printf(const char *, ...);
int main(void) { return printf("%d\n", 12); })");
    EXPECT_EQ(output(), "12\n");
    EXPECT_THAT(observer_.only("return"), ElementsAre("return printf 3"));
}

TEST_F(LoweringTest, StatementExpressionsYieldTheirLastExpression) {
    EXPECT_EQ(status(R"(int main(void) {
    int y = ({ int t = 3; t + 1; });
    return y;
})"),
              4);
    EXPECT_THAT(observer_.events, ElementsAre("S 2:5", "S 2:16", "S 2:27", "S 3:5"));
}

TEST_F(LoweringTest, TheValueOfAStatementExpressionMayBeLabeled) {
    EXPECT_EQ(status(R"(int main(void) {
    int i = 0;
    return ({ again: i++; if (i < 5) goto again; done: i * 2; });
})"),
              10);
}

TEST_F(LoweringTest, BreakLeavesAStatementExpression) {
    EXPECT_EQ(status(R"(int main(void) {
    int n = 0;
    while (1) {
        n += ({ if (n == 2) break; 1; });
    }
    return n;
})"),
              2);
}

TEST_F(LoweringTest, ReturnInAStatementExpressionEndsTheFunction) {
    EXPECT_EQ(status(R"(int f(int x) {
    int y = ({ if (x) return 7; 3; });
    return y;
}
int main(void) { return f(1) * 10 + f(0); })"),
              73);
}

TEST_F(LoweringTest, VariableLengthArraysEndWithTheirScope) {
    // 10,000 iterations of a 4 KiB array would exceed the memory limit if the arrays lived on.
    const RunResult result = run(R"(int main(void) {
    int n = 1024, sum = 0;
    for (int i = 0; i < 10000; i++) {
        int a[n];
        a[i % n] = i;
        sum += a[i % n] & 1;
    }
    return sum == 5000;
})",
                                 DataModel::LP64, {.maxSteps = 0, .maxCallDepth = 100, .memoryLimit = 1 << 20});
    EXPECT_EQ(result.termination, Termination::Returned) << result.message;
    EXPECT_EQ(result.exitStatus, 1);
}

TEST_F(LoweringTest, JumpingBackBeforeAVariableLengthArrayEndsIt) {
    const RunResult result = run(R"(int main(void) {
    int n = 1024, i = 0;
again:;
    int a[n];
    a[0] = i;
    if (++i < 10000)
        goto again;
    return a[0] == 9999;
})",
                                 DataModel::LP64, {.maxSteps = 0, .maxCallDepth = 100, .memoryLimit = 1 << 20});
    EXPECT_EQ(result.termination, Termination::Returned) << result.message;
    EXPECT_EQ(result.exitStatus, 1);
}

TEST_F(LoweringTest, ProgramsWithoutMainAreRejected) {
    target_ = std::make_unique<TargetInfo>(TargetInfo::forDataModel(DataModel::LP64));
    context_ = std::make_unique<ast::AstContext>(*target_, interner_);
    const FileId file = sources_.addFile("test.c", "int f(void) { return 0; }\nint main(void);\n");
    lex::LexResult lexed = lex::lexFile(sources_, file, interner_, diagnostics_);
    ast::TranslationUnit* unit = parse::parseTranslationUnit(lexed.tokens, *context_, sources_, diagnostics_);
    ASSERT_NE(unit, nullptr);
    ASSERT_TRUE(sema::analyze(*unit, *context_, diagnostics_));
    const auto program = buildProgram(*unit, *context_);
    ASSERT_FALSE(program.has_value());
    EXPECT_EQ(program.error().message, "the program has no definition of 'main'");
}

TEST_F(LoweringTest, InlineAssemblyIsUnsupportedWhenReached) {
    const RunResult result = run(R"(int main(void) {
    __asm__ __volatile__("" ::: "memory");
    if (0)
        __asm__("nop");
    __asm__("nop");
    return 0;
})");
    EXPECT_EQ(result.termination, Termination::Unsupported);
    EXPECT_EQ(position(result.location), "5:5");
}

} // namespace
} // namespace nitwit::interp::test
