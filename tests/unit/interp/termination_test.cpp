// How runs end: return, exit, abort, assumptions, undefined behaviour (memory errors, division by zero, ...),
// unsupported constructs, the observer, and the limits.

#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>

#include <gtest/gtest.h>

#include "interpreter_test_support.hpp"
#include "support/large_stack.hpp"

namespace nitwit::interp::test {
namespace {

using ::testing::ElementsAre;
using ::testing::HasSubstr;

class TerminationTest : public InterpreterTest {};

TEST_F(TerminationTest, MainReturnsItsStatusAndFallingOffMainReturnsZero) {
    EXPECT_EQ(status("int main(void) { return 300; }"), 300);
    EXPECT_EQ(status("int main(void) { int x = 1; }"), 0);
}

TEST_F(TerminationTest, MainGetsArgcAndArgv) {
    EXPECT_EQ(outputOf(R"(int printf(const char *, ...);
int main(int argc, char **argv) { printf("%d %s %d\n", argc, argv[0], argv[1] == 0); return 0; })"),
              "1 prog 1\n");
}

TEST_F(TerminationTest, ExitEndsTheRunWithItsStatus) {
    const RunResult result = run(R"(void exit(int);
void f(void) { exit(3); }
int main(void) { f(); return 0; })");
    EXPECT_EQ(result.termination, Termination::Exit);
    EXPECT_EQ(result.exitStatus, 3);
}

TEST_F(TerminationTest, AbortAndAssertFailAbort) {
    EXPECT_EQ(run("void abort(void);\nint main(void) { abort(); }").termination, Termination::Abort);
    EXPECT_EQ(run(R"(void __assert_fail(const char *, const char *, unsigned, const char *);
int main(void) { __assert_fail("0", "test.c", 2, "main"); })")
                  .termination,
              Termination::Abort);
    EXPECT_EQ(run("void __VERIFIER_error(void);\nint main(void) { __VERIFIER_error(); }").termination,
              Termination::Abort);
}

TEST_F(TerminationTest, FailedAssumptionsEndTheRun) {
    const RunResult result = run(R"(void __VERIFIER_assume(int);
int main(void) { __VERIFIER_assume(1); __VERIFIER_assume(0); return 1; })");
    EXPECT_EQ(result.termination, Termination::AssumeFailed);
    EXPECT_EQ(position(result.location), "2:40");
}

TEST_F(TerminationTest, NullPointerDereferenceIsUndefined) {
    const RunResult result = run(R"(int main(void) {
    int *p = 0;
    return *p;
})");
    EXPECT_EQ(result.termination, Termination::UndefinedBehavior);
    EXPECT_THAT(result.message, HasSubstr("null pointer"));
    EXPECT_EQ(position(result.location), "3:12");
}

TEST_F(TerminationTest, OutOfBoundsAccessIsUndefined) {
    const RunResult result = run(R"(int main(void) {
    int a[4] = {0};
    int i = 4;
    a[i] = 1;
    return 0;
})");
    EXPECT_EQ(result.termination, Termination::UndefinedBehavior);
    EXPECT_THAT(result.message, HasSubstr("out of bounds"));
    EXPECT_THAT(result.message, HasSubstr("'a'"));
}

TEST_F(TerminationTest, UseAfterFreeIsUndefined) {
    const RunResult result = run(R"(void *malloc(unsigned long);
void free(void *);
int main(void) {
    int *p = malloc(sizeof(int));
    *p = 1;
    free(p);
    return *p;
})");
    EXPECT_EQ(result.termination, Termination::UndefinedBehavior);
    EXPECT_THAT(result.message, HasSubstr("lifetime has ended"));
}

TEST_F(TerminationTest, LocalsDieWhenTheirFunctionReturns) {
    const RunResult result = run(R"(int *dangling(void) { int x = 1; return &x; }
int main(void) { int *p = dangling(); return *p; })");
    EXPECT_EQ(result.termination, Termination::UndefinedBehavior);
    EXPECT_THAT(result.message, HasSubstr("lifetime has ended"));
}

TEST_F(TerminationTest, WritingAStringLiteralIsUndefined) {
    const RunResult result = run(R"(int main(void) { char *s = "abc"; s[0] = 'x'; return 0; })");
    EXPECT_EQ(result.termination, Termination::UndefinedBehavior);
    EXPECT_THAT(result.message, HasSubstr("read-only"));
}

TEST_F(TerminationTest, FreeingAnAllocaBlockIsUndefined) {
    const RunResult result = run(R"(void free(void *);
int main(void) { free(__builtin_alloca(4)); return 0; })");
    EXPECT_EQ(result.termination, Termination::UndefinedBehavior);
}

TEST_F(TerminationTest, DivisionByZeroIsUndefined) {
    const RunResult result = run(R"(int main(void) {
    int zero = 0;
    return 1 / zero;
})");
    EXPECT_EQ(result.termination, Termination::UndefinedBehavior);
    EXPECT_EQ(result.message, "division by zero");
    EXPECT_EQ(position(result.location), "3:12");
}

TEST_F(TerminationTest, SignedOverflowIsReportedAndWraps) {
    EXPECT_EQ(status(R"(int main(void) {
    int x = 2147483647;
    x = x + 1;
    return x == -2147483647 - 1;
})"),
              1);
    EXPECT_THAT(observer_.only("ub"), ElementsAre("ub signed overflow 3:9"));
}

TEST_F(TerminationTest, ShiftsOutOfRangeAreReported) {
    EXPECT_EQ(status(R"(int main(void) {
    int n = 33, m = -1;
    int a = 1 << n;
    int b = m << 1;
    return a == 2 && b == -2;
})"),
              1);
    EXPECT_THAT(observer_.only("ub"), ElementsAre("ub shift count 3:13", "ub shift of negative 4:13"));
}

TEST_F(TerminationTest, TheObserverMayStopAtUndefinedBehavior) {
    observer_.stopOnUndefinedBehavior = true;
    const RunResult result = run("int main(void) { int x = 2147483647; return x + 1; }");
    EXPECT_EQ(result.termination, Termination::Stopped);
}

TEST_F(TerminationTest, CallingANullFunctionPointerIsUndefined) {
    const RunResult result = run(R"(int main(void) { int (*f)(void) = 0; return f(); })");
    EXPECT_EQ(result.termination, Termination::UndefinedBehavior);
    EXPECT_THAT(observer_.only("call"), ElementsAre("call <none>()"));
}

TEST_F(TerminationTest, CallingAnAddressThatIsNoFunctionIsUndefined) {
    const RunResult result = run(R"(int x;
int main(void) { int (*f)(void) = (int (*)(void))&x; return f(); })");
    EXPECT_EQ(result.termination, Termination::UndefinedBehavior);
    EXPECT_THAT(result.message, HasSubstr("no function"));
}

TEST_F(TerminationTest, CallingAWeakFunctionWithoutDefinitionIsUndefined) {
    const RunResult result = run("#pragma weak missing\nint missing(void);\nint main(void) { return missing(); }");
    EXPECT_EQ(result.termination, Termination::UndefinedBehavior);
}

TEST_F(TerminationTest, TheMissingReturnValueMayBeIgnored) {
    EXPECT_EQ(status("int f(int x) { if (x) return 1; }\nint main(void) { f(0); return 0; }"), 0);
}

TEST_F(TerminationTest, UsingTheMissingReturnValueIsUndefined) {
    const RunResult result = run(R"(int f(int x) { if (x) return 1; }
int main(void) { return f(0); })");
    EXPECT_EQ(result.termination, Termination::UndefinedBehavior);
    EXPECT_THAT(result.message, HasSubstr("without returning a value"));
}

TEST_F(TerminationTest, VaListsEndWithTheirFunction) {
    const RunResult result = run(R"(typedef __builtin_va_list va_list;
va_list saved;
void keep(int n, ...) { __builtin_va_start(saved, n); }
int main(void) { keep(1, 2); return __builtin_va_arg(saved, int); })");
    EXPECT_EQ(result.termination, Termination::UndefinedBehavior);
    EXPECT_THAT(result.message, HasSubstr("returned"));
}

TEST_F(TerminationTest, VaArgAfterTheLastArgumentIsUndefined) {
    const RunResult result = run(R"(typedef __builtin_va_list va_list;
int second(int n, ...) {
    va_list ap; __builtin_va_start(ap, n);
    int a = __builtin_va_arg(ap, int);
    int b = __builtin_va_arg(ap, int);
    __builtin_va_end(ap);
    return a + b;
}
int main(void) { return second(1, 2); })");
    EXPECT_EQ(result.termination, Termination::UndefinedBehavior);
    EXPECT_THAT(result.message, HasSubstr("after the last argument"));
}

TEST_F(TerminationTest, CallsWithoutPrototypeMustPassFittingArguments) {
    // C17 6.5.2.2p6: the promoted argument types must be compatible with the promoted parameter types.
    EXPECT_EQ(status(R"(int same();
int main(void) { return plus(5) + same(3); }
int plus(int x) { return x; }
int same(unsigned u) { return (int)u; })"),
              8);
    const RunResult result = run(R"(int f();
int main(void) { return f(1.5); }
int f(int x) { return x; })");
    EXPECT_EQ(result.termination, Termination::UndefinedBehavior);
    EXPECT_THAT(result.message, HasSubstr("argument 1"));
    EXPECT_EQ(run("int g();\nint main(void) { return g(); }\nint g(int x) { return x; }").termination,
              Termination::UndefinedBehavior);
}

TEST_F(TerminationTest, UnsupportedLibraryFunctionsEndTheRun) {
    const RunResult result = run(R"(int atexit(void (*)(void));
void bye(void) {}
int main(void) { return atexit(bye); })");
    EXPECT_EQ(result.termination, Termination::Unsupported);
    EXPECT_THAT(result.message, HasSubstr("atexit is not supported"));
}

TEST_F(TerminationTest, TheObserverMayStopTheRun) {
    observer_.onCondition = [](const ConditionEvent&, EvaluationContext&) {
        return Directive::Stop;
    };
    const RunResult result = run("int main(void) { int x = 1; if (x) return 1; return 2; }");
    EXPECT_EQ(result.termination, Termination::Stopped);
    EXPECT_THAT(observer_.only("C"), ElementsAre("C if 1:29 true"));
}

TEST_F(TerminationTest, TheStepLimitEndsEndlessLoops) {
    const RunResult result =
        run("int main(void) { while (1) {} }", DataModel::LP64, {.maxSteps = 1000, .maxCallDepth = 100});
    EXPECT_EQ(result.termination, Termination::LimitReached);
    EXPECT_EQ(result.steps, 1001);
}

TEST_F(TerminationTest, AnEndlessLoopWithoutEffectEndsTheRunAfterOneRound) {
    const RunOptions options{.maxSteps = 100'000, .endsEndlessLoops = true};
    // The location is that of the first instruction of the loop's round: 2:5, or the body of a do loop at 2:8.
    const std::pair<std::string_view, std::uint32_t> loops[] = {
        {"L: goto L;", 21},      {"while (1);", 21},     {"for (;;) {}", 21},
        {"do ; while (1);", 24}, {"while ((7)) {}", 21}, {"while (1) { continue; }", 21}};
    for (const auto& [loop, offset] : loops) {
        const RunResult result = run(std::format("int main(void) {{\n    {}\n}}", loop), DataModel::LP64, options);
        EXPECT_EQ(result.termination, Termination::EndlessLoop) << loop << ": " << result.message;
        EXPECT_EQ(result.location.offset, offset) << loop;
        EXPECT_LT(result.steps, 10u) << loop;
    }
}

TEST_F(TerminationTest, TheObserverSeesAWholeRoundOfAnEndlessLoop) {
    const RunResult result = run("int main(void) {\n    L: ;\n    goto L;\n}", DataModel::LP64,
                                 {.maxSteps = 100'000, .endsEndlessLoops = true});
    EXPECT_EQ(result.termination, Termination::EndlessLoop);
    EXPECT_THAT(observer_.only("S"), ElementsAre("S 2:5", "S 2:8", "S 3:5", "S 2:5", "S 2:8", "S 3:5"));
}

TEST_F(TerminationTest, LoopsWithEffectsOrConditionsAreNotEndless) {
    const RunOptions options{.maxSteps = 10'000, .endsEndlessLoops = true};
    for (const std::string_view loop : {"int x = 0; while (1) x++;", "int x = 0; while (x == 0);", "while (1) f();",
                                        "volatile int v = 0; while (1) v;", "L: ; int y; goto L;",
                                        "while (1) { if (0) ; }", "int x = 1; while (x) {}"}) {
        const RunResult result =
            run(std::format("void f(void) {{}}\nint main(void) {{\n    {}\n}}", loop), DataModel::LP64, options);
        EXPECT_EQ(result.termination, Termination::LimitReached) << loop << ": " << result.message;
    }
}

TEST_F(TerminationTest, AJumpAmongTheInstructionsOfALoopLeavesThem) {
    // The jump of `goto L` passes over the jump of `goto B`, which leaves the instructions between L and A: they are no
    // endless loop, although each of them is a statement event or a jump.
    const RunResult result = run(R"(int main(void) {
    int x = 0;
    goto L;
B:  if (++x == 100) return 3;
    goto A;
L:  ;
    goto B;
A:  goto L;
})",
                                 DataModel::LP64, {.maxSteps = 100'000, .endsEndlessLoops = true});
    EXPECT_EQ(result.termination, Termination::Returned) << result.message;
    EXPECT_EQ(result.exitStatus, 3);
}

TEST_F(TerminationTest, WithoutTheOptionAnEndlessLoopRunsLikeANativeRun) {
    const RunResult result = run("int main(void) { L: goto L; }", DataModel::LP64, {.maxSteps = 1000});
    EXPECT_EQ(result.termination, Termination::LimitReached);
}

TEST_F(TerminationTest, TheCallDepthLimitEndsEndlessRecursion) {
    observer_.recordStatements = false;
    const RunResult result = run("int f(int n) { return f(n + 1); }\nint main(void) { return f(0); }", DataModel::LP64,
                                 {.maxSteps = 0, .maxCallDepth = 50});
    EXPECT_EQ(result.termination, Termination::LimitReached);
    EXPECT_THAT(result.message, HasSubstr("call depth"));
}

TEST_F(TerminationTest, DeepRecursionStopsBeforeTheHostStackOverflows) {
    // Deep recursion with deep expressions around each call needs more host stack than the 1 GiB the driver gives,
    // before the default call depth limit is reached.
    std::string sum = "f(n - 1)";
    for (int i = 0; i < 30; ++i) {
        sum = "(" + sum + " + 1)";
    }
    const std::string program = std::format(
        "int f(int n) {{ if (n == 0) return 0; return {} - 30; }}\nint main(void) {{ return f(99990); }}", sum);
    observer_.recordStatements = false;
    RunResult result;
    runWithLargeStack(std::size_t{1} << 30, [&] { result = run(program); });
    if (result.termination == Termination::LimitReached) {
        EXPECT_THAT(result.message, HasSubstr("host stack"));
    } else {
        EXPECT_EQ(result.termination, Termination::Returned) << result.message;
    }
}

TEST_F(TerminationTest, TheHostStackLimitsRecursionOnSmallStacks) {
    observer_.recordStatements = false;
    RunResult result;
    // 8 MiB, of which 1 MiB stays in reserve, cannot hold 100,000 interpreted calls.
    runWithLargeStack(std::size_t{8} << 20, [&] {
        result = run("int f(int n) { return n == 0 ? 0 : f(n - 1) + 1; }\nint main(void) { return f(100000); }");
    });
    EXPECT_EQ(result.termination, Termination::LimitReached);
    EXPECT_THAT(result.message, HasSubstr("host stack"));
}

TEST_F(TerminationTest, CallsThroughPointersOfIncompatibleTypesAreUndefined) {
    const RunResult result = run(R"(struct S { int a, b; };
int g(int x) { return x; }
int main(void) {
    int (*pg)(struct S) = (int (*)(struct S))g;
    struct S s = {1, 2};
    return pg(s);
})");
    EXPECT_EQ(result.termination, Termination::UndefinedBehavior);
    EXPECT_THAT(result.message, HasSubstr("argument 1"));
    const RunResult returned = run(R"(struct S { int a, b; };
struct S make(void) { struct S s = {1, 2}; return s; }
int main(void) {
    int (*pm)(void) = (int (*)(void))make;
    return pm();
})");
    EXPECT_EQ(returned.termination, Termination::UndefinedBehavior);
    EXPECT_THAT(returned.message, HasSubstr("does not fit"));
}

TEST_F(TerminationTest, AssignmentsBetweenOverlappingObjectsAreUndefined) {
    const RunResult result = run(R"(struct S { int a, b, c; };
union U { struct S s; struct { int x; struct S t; } w; };
int main(void) {
    union U u = {{1, 2, 3}};
    u.w.t = u.s;
    return u.w.t.c;
})");
    EXPECT_EQ(result.termination, Termination::UndefinedBehavior);
    EXPECT_THAT(result.message, HasSubstr("overlapping"));
    EXPECT_EQ(status("struct S { int a, b; } s = {1, 2};\nint main(void) { s = s; return s.b; }"), 2);
}

TEST_F(TerminationTest, TheMemoryLimitCountsHeapBlocks) {
    const RunResult result = run(R"(void *malloc(unsigned long);
int main(void) { for (;;) malloc(1000); })",
                                 DataModel::LP64, {.maxSteps = 0, .maxCallDepth = 100, .memoryLimit = 100000});
    EXPECT_EQ(result.termination, Termination::OutOfMemory);
}

TEST_F(TerminationTest, TheMemoryLimitCountsLocals) {
    const RunResult result = run(R"(int main(void) { char big[200000]; big[0] = 1; return big[0]; })", DataModel::LP64,
                                 {.maxSteps = 0, .maxCallDepth = 100, .memoryLimit = 100000});
    EXPECT_EQ(result.termination, Termination::OutOfMemory);
}

TEST_F(TerminationTest, StepsCountStatementsAndCalls) {
    const RunResult result = run("int f(void) { return 1; }\nint main(void) { int x = f(); return x; }");
    EXPECT_EQ(result.termination, Termination::Returned);
    EXPECT_EQ(result.steps, 4); // two statements of main, the call, the return of f
}

} // namespace
} // namespace nitwit::interp::test
