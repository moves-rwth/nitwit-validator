// Non-local jumps (interp/README.md, "Non-local jumps"): what setjmp returns, the frames and objects a longjmp ends,
// the events of a jump, copies of a jmp_buf, the uses of setjmp the interpreter resumes, and the jumps that end the run
// (undefined, or not supported).

#include <string>

#include <gtest/gtest.h>

#include "interpreter_test_support.hpp"

namespace nitwit::interp::test {
namespace {

using ::testing::ElementsAre;
using ::testing::HasSubstr;

/// glibc's declarations, as preprocessed programs have them, in one line.
constexpr std::string_view prelude =
    "typedef long __jmp_buf[8]; struct __jmp_buf_tag { __jmp_buf __jmpbuf; int __mask_was_saved; unsigned long "
    "__saved_mask[16]; }; typedef struct __jmp_buf_tag jmp_buf[1]; int _setjmp(struct __jmp_buf_tag __env[1]); "
    "int __sigsetjmp(struct __jmp_buf_tag __env[1], int __savemask); void longjmp(struct __jmp_buf_tag __env[1], int "
    "__val); void _longjmp(struct __jmp_buf_tag __env[1], int __val); void siglongjmp(struct __jmp_buf_tag __env[1], "
    "int __val); void __longjmp_chk(struct __jmp_buf_tag __env[1], int __val);\n";

class JumpsTest : public DataModelTest {
protected:
    /// Runs `text` after the prelude, which is line 1.
    RunResult runJumps(std::string_view text) { return run(std::string(prelude) + std::string(text), GetParam()); }
    int statusOf(std::string_view text) { return status(std::string(prelude) + std::string(text), GetParam()); }
};

INSTANTIATE_TEST_SUITE_P(DataModels, JumpsTest, ::testing::Values(DataModel::ILP32, DataModel::LP64), dataModelName);

TEST_P(JumpsTest, SetjmpReturnsZeroAndThenTheValueOfEachLongjmp) {
    EXPECT_EQ(statusOf(R"(jmp_buf env;
int trace = 0;
int main(void) {
    int value = _setjmp(env);
    trace = trace * 10 + value;
    if (value < 3) longjmp(env, value + 1);
    return trace;
})"),
              123);
}

TEST_P(JumpsTest, LongjmpWithZeroMakesSetjmpReturnOne) {
    // C11 7.13.2.1p4.
    EXPECT_EQ(statusOf(R"(jmp_buf env;
int main(void) {
    volatile int round = 0;
    int value = _setjmp(env);
    if (round++ == 0) longjmp(env, 0);
    return value;
})"),
              1);
}

TEST_P(JumpsTest, EveryNameOfSetjmpAndLongjmpWorks) {
    EXPECT_EQ(statusOf(R"(jmp_buf env;
int sum = 0;
int main(void) {
    volatile int round = 0;
    int value = _setjmp(env);
    sum = sum + value;
    if (round == 0) { round = 1; longjmp(env, 1); }
    if (round == 1) { round = 2; _longjmp(env, 10); }
    if (round == 2) { round = 3; siglongjmp(env, 100); }
    if (round == 3) { round = 4; __longjmp_chk(env, 1000); }
    value = __sigsetjmp(env, 1);
    sum = sum + value;
    if (round == 4) { round = 5; longjmp(env, 10000); }
    return sum;
})"),
              11111);
}

TEST_P(JumpsTest, LongjmpEndsTheFramesBetween) {
    observer_.recordStatements = false;
    const RunResult result = runJumps(R"(jmp_buf env;
int *local;
void down(int n) {
    int here = n;
    if (n == 0) { local = &here; longjmp(env, 2); }
    down(n - 1);
}
int main(void) {
    if (_setjmp(env)) return *local;
    down(4);
    return 0;
})");
    EXPECT_EQ(result.termination, Termination::UndefinedBehavior);
    EXPECT_THAT(result.message, HasSubstr("lifetime has ended"));
    // The functions left by longjmp do not return; the setjmp call returns again.
    EXPECT_THAT(observer_.only("return"), ElementsAre("return _setjmp 0", "return _setjmp 2"));
    EXPECT_EQ(observer_.only("call down").size(), 5u);
}

TEST_P(JumpsTest, ALongjmpResumesAFrameBetweenMainAndItself) {
    observer_.recordStatements = false;
    EXPECT_EQ(statusOf(R"(jmp_buf env;
int down(int n) {
    if (n == 3) {
        if (_setjmp(env)) return 100 + n;
    }
    if (n == 6) longjmp(env, 1);
    return down(n + 1);
}
int main(void) { return down(0); })"),
              103);
    // down(4) to down(6) end without returning; down(0) to down(3) return.
    EXPECT_EQ(observer_.only("return down").size(), 4u);
    EXPECT_EQ(observer_.only("call down").size(), 7u);
}

TEST_P(JumpsTest, ALongjmpLeavesAStatementExpression) {
    EXPECT_EQ(statusOf(R"(jmp_buf env;
int main(void) {
    volatile int round = 0;
    int value = _setjmp(env);
    if (round++ == 0) {
        int x = ({ longjmp(env, 4); 0; });
        return x;
    }
    return value;
})"),
              4);
}

TEST_P(JumpsTest, TheJumpReportsTheReturnOfSetjmpAndTheConditionAgain) {
    runJumps(R"(jmp_buf env;
void f(void) { longjmp(env, 2); }
int main(void) {
    if (_setjmp(env)) return 1;
    f();
    return 0;
})");
    // The statement of the setjmp call does not start again: the run resumes in its condition.
    EXPECT_THAT(observer_.only("S"), ElementsAre("S 5:5", "S 6:5", "S 3:16", "S 5:23"));
    EXPECT_THAT(observer_.only("C"), ElementsAre("C if 5:5 false", "C if 5:5 true"));
    EXPECT_THAT(observer_.only("return"), ElementsAre("return _setjmp 0", "return _setjmp 2"));
    EXPECT_EQ(observer_.only("call longjmp").size(), 1u);
}

TEST_P(JumpsTest, ModifiedLocalsKeepTheValuesTheyHadAtTheLongjmp) {
    // C11 7.13.2.1p3 makes x indeterminate; the interpreter keeps the value it had, as GCC and Clang do at -O0.
    EXPECT_EQ(statusOf(R"(jmp_buf env;
int main(void) {
    int x = 1;
    if (_setjmp(env)) return x;
    x = 2;
    longjmp(env, 1);
})"),
              2);
}

TEST_P(JumpsTest, CopiesOfAJmpBufJumpLikeTheOriginal) {
    EXPECT_EQ(statusOf(R"(void *memcpy(void *, const void *, unsigned long);
void *malloc(unsigned long);
struct holder { jmp_buf env; } a, b;
int main(void) {
    struct __jmp_buf_tag *heap = malloc(sizeof(jmp_buf));
    volatile int round = 0;
    int value = _setjmp(a.env);
    if (round == 0) { round = 1; b = a; longjmp(b.env, 5); }
    if (round == 1) { round = 2; memcpy(heap, a.env, sizeof(jmp_buf)); longjmp(heap, value + 1); }
    return value;
})"),
              6);
}

TEST_P(JumpsTest, ASetjmpInALoopResumesAtItsLastCall) {
    EXPECT_EQ(statusOf(R"(jmp_buf env;
int main(void) {
    volatile int i;
    for (i = 0; i < 1000; ++i) {
        if (_setjmp(env)) return i;
    }
    longjmp(env, 1);
})"),
              1000);
}

TEST_P(JumpsTest, SetjmpResumesInTheUsesOfC11AndAsAssignedValue) {
    // C11 7.13.1.1p4, and an assignment or initializer, which GCC and Clang support too.
    EXPECT_EQ(statusOf(R"(jmp_buf env;
struct s { int member; int array[2]; } s;
int trace = 0;
void again(int step) { trace = trace * 10 + step; longjmp(env, step); }
int main(void) {
    volatile int step = 0;
    if (!_setjmp(env)) again(1);
    if (0 != _setjmp(env)) { } else again(2);
    switch (_setjmp(env)) { case 0: again(3); default: break; }
    while (_setjmp(env) < 4) again(4);
    (void)_setjmp(env);
    if (++step == 1) again(5);
    s.member = _setjmp(env);
    if (s.member == 0) again(6);
    s.array[1] = (_setjmp(env));
    if (s.array[1] == 0) again(7);
    int x = _setjmp(env);
    if (x == 0) again(8);
    return trace;
})"),
              12345678);
}

TEST_P(JumpsTest, TheJumpEndsTheStackObjectsAllocatedAfterSetjmp) {
    // A variable length array declared after the setjmp call, whose scope the jump leaves, and an alloca block.
    const RunResult array = runJumps(R"(jmp_buf env;
int *volatile element;
int main(void) {
    int n = 4;
    if (_setjmp(env)) return *element;
    { int a[n]; a[0] = 1; element = a; longjmp(env, 1); }
})");
    EXPECT_EQ(array.termination, Termination::UndefinedBehavior);
    EXPECT_THAT(array.message, HasSubstr("lifetime has ended (object 'a')"));
    const RunResult block = runJumps(R"(void *alloca(unsigned long);
jmp_buf env;
int *volatile block;
int main(void) {
    if (_setjmp(env)) return *block;
    block = alloca(sizeof(int));
    *block = 2;
    longjmp(env, 1);
})");
    EXPECT_EQ(block.termination, Termination::UndefinedBehavior);
    EXPECT_THAT(block.message, HasSubstr("lifetime has ended (object 'alloca')"));
}

TEST_P(JumpsTest, LongjmpToAFunctionThatReturnedIsUndefined) {
    // C11 7.13.2.1p2; the wrapper is busybox's, whose `_setjmp` returns the value of setjmp.
    const RunResult result = runJumps(R"(int setjmp(struct __jmp_buf_tag env[1]);
int _setjmp(struct __jmp_buf_tag env[1]) { return setjmp(env); }
jmp_buf env;
int main(void) {
    if (_setjmp(env)) return 1;
    longjmp(env, 1);
})");
    EXPECT_EQ(result.termination, Termination::UndefinedBehavior);
    EXPECT_THAT(result.message, HasSubstr("whose function has returned"));
}

TEST_P(JumpsTest, LongjmpWithoutSetjmpIsUndefined) {
    for (const std::string_view program : {
             "jmp_buf env;\nint main(void) { longjmp(env, 1); }",
             "int main(void) { jmp_buf env; longjmp(env, 1); }",
             "void *memset(void *, int, unsigned long);\n"
             "int main(void) { jmp_buf env; memset(env, 0x7f, sizeof env); longjmp(env, 1); }",
         }) {
        const RunResult result = runJumps(program);
        EXPECT_EQ(result.termination, Termination::UndefinedBehavior) << program;
        EXPECT_THAT(result.message, HasSubstr("that setjmp did not save")) << program;
    }
    EXPECT_TRUE(oracle_.requests.empty()); // the bytes of the jmp_buf are no input
}

TEST_P(JumpsTest, SetjmpInAnotherExpressionDoesNotResume) {
    // Evaluating the expression again would evaluate `1 +` again too: no longjmp can resume there.
    EXPECT_EQ(statusOf("jmp_buf env;\nint main(void) { int x = 1 + _setjmp(env); return x; }"), 1);
    for (const std::string_view program : {
             "jmp_buf env;\nint main(void) { int x = 1 + _setjmp(env); if (x == 1) longjmp(env, 1); }",
             "jmp_buf env;\nint f(void);\nint main(void) { if (f() == _setjmp(env)) longjmp(env, 1); }\nint f(void) "
             "{ return 0; }",
             "jmp_buf env;\nint *p;\nint main(void) { int x; p = &x; *p = _setjmp(env); if (x == 0) longjmp(env, 1); }",
         }) {
        const RunResult result = runJumps(program);
        EXPECT_EQ(result.termination, Termination::Unsupported) << program;
        EXPECT_THAT(result.message, HasSubstr("cannot resume")) << program;
    }
}

TEST_P(JumpsTest, SetjmpThroughAPointerDoesNotResume) {
    const RunResult result = runJumps(R"(jmp_buf env;
int main(void) {
    int (*save)(struct __jmp_buf_tag *) = _setjmp;
    if (save(env)) return 1;
    longjmp(env, 1);
})");
    EXPECT_EQ(result.termination, Termination::Unsupported);
    EXPECT_THAT(result.message, HasSubstr("through a function pointer"));
}

TEST_P(JumpsTest, SetjmpInAStatementExpressionDoesNotResume) {
    const RunResult result = runJumps(R"(jmp_buf env;
int main(void) {
    int x = ({ _setjmp(env); });
    if (x == 0) longjmp(env, 1);
    return x;
})");
    EXPECT_EQ(result.termination, Termination::Unsupported);
    EXPECT_THAT(result.message, HasSubstr("in a statement expression"));
}

TEST_P(JumpsTest, SetjmpAfterAVariablyModifiedTypeDoesNotResume) {
    // C11 7.13.2.1p2: undefined if the scope of the array was left in the interim.
    const RunResult result = runJumps(R"(jmp_buf env;
int main(void) {
    int n = 3;
    { int a[n]; if (_setjmp(env)) return 1; }
    longjmp(env, 1);
})");
    EXPECT_EQ(result.termination, Termination::Unsupported);
    EXPECT_THAT(result.message, HasSubstr("variably modified type"));
}

TEST_P(JumpsTest, SetjmpInTheInitializerOfAStaticObjectIsUnsupported) {
    // C17 6.7.9p4 requires a constant initializer; the run evaluates such initializers before main, outside any frame.
    for (const std::string_view program : {
             "jmp_buf env;\nint x = _setjmp(env);\nint main(void) { return x; }",
             "jmp_buf env;\nint main(void) { static int s = _setjmp(env); return s; }",
         }) {
        const RunResult result = runJumps(program);
        EXPECT_EQ(result.termination, Termination::Unsupported) << program;
        EXPECT_THAT(result.message, HasSubstr("in the initializer of a static object")) << program;
    }
}

TEST_P(JumpsTest, GccsJumpBuiltinsAreUnsupported) {
    const RunResult result = runJumps(R"(int __builtin_setjmp(void *);
void *buffer[5];
int main(void) { return __builtin_setjmp(buffer); })");
    EXPECT_EQ(result.termination, Termination::Unsupported);
    EXPECT_THAT(result.message, HasSubstr("__builtin_setjmp is not supported"));
}

} // namespace
} // namespace nitwit::interp::test
