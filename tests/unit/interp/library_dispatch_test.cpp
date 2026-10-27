#include <set>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "library_test_support.hpp"
#include "sema/sema.hpp"

namespace nitwit::interp::test {
namespace {

class DispatchTest : public LibraryTest {};

INSTANTIATE_TEST_SUITE_P(DataModels, DispatchTest, testing::Values(DataModel::ILP32, DataModel::LP64), modelName);

TEST(LibraryDispatchTest, FindsFunctionsByName) {
    for (const std::string_view name : {"malloc", "printf", "strlen", "__assert_fail", "__ctype_b_loc", "sinl",
                                        "__builtin_clz", "__isnanf", "rand", "__errno_location"}) {
        EXPECT_NE(findLibraryFunction(name), nullptr) << name;
    }
    EXPECT_EQ(findLibraryFunction("strlen"), findLibraryFunction("strlen"));
}

TEST(LibraryDispatchTest, KnowsNoOtherFunctions) {
    // The oracle answers calls of unknown functions; nondeterministic inputs, assume, setjmp and longjmp are the
    // evaluator's.
    for (const std::string_view name : {"", "main", "reach_error", "__VERIFIER_nondet_int", "__VERIFIER_assume",
                                        "__builtin_", "__builtin_no_such_function", "getchar", "setjmp", "_setjmp",
                                        "__sigsetjmp", "longjmp", "siglongjmp", "__longjmp_chk"}) {
        EXPECT_EQ(findLibraryFunction(name), nullptr) << name;
    }
}

TEST(LibraryDispatchTest, BuiltinFormsOfLibraryFunctions) {
    EXPECT_EQ(findLibraryFunction("__builtin_memcpy"), findLibraryFunction("memcpy"));
    EXPECT_EQ(findLibraryFunction("__builtin_fabsf"), findLibraryFunction("fabsf"));
    EXPECT_EQ(findLibraryFunction("__builtin_scanf"), findLibraryFunction("scanf"));
    // Where GCC's builtin differs from glibc's function, it has its own implementation.
    EXPECT_NE(findLibraryFunction("__builtin_isnan"), findLibraryFunction("isnan"));
    EXPECT_NE(findLibraryFunction("__builtin_isinff"), findLibraryFunction("isinff"));
    EXPECT_NE(findLibraryFunction("__builtin_fmin"), findLibraryFunction("fmin"));
    EXPECT_NE(findLibraryFunction("__builtin_isdigit"), findLibraryFunction("isdigit"));
}

/// The functions sema knows that the evaluator implements itself.
const std::set<std::string, std::less<>> evaluatorFunctions = {
    "alloca",
    "__builtin_alloca",
    "__builtin_va_start",
    "__builtin_va_end",
    "__builtin_va_copy",
    "__builtin_add_overflow",
    "__builtin_sub_overflow",
    "__builtin_mul_overflow",
};

// No library function that sema types may silently become an input of the oracle.
TEST(LibraryDispatchTest, EveryFunctionSemaKnowsIsImplementedOrUnsupported) {
    const std::vector<std::string> names = sema::builtinFunctionNames();
    EXPECT_GT(names.size(), 300u);
    for (const std::string& name : names) {
        // The atomic builtins work on the program's memory, which the evaluator owns.
        if (evaluatorFunctions.contains(name) || name.starts_with("__atomic_") || name.starts_with("__sync_")) {
            EXPECT_EQ(findLibraryFunction(name), nullptr) << name;
        } else {
            EXPECT_NE(findLibraryFunction(name), nullptr) << name;
        }
    }
}

TEST_P(DispatchTest, UnsupportedFunctionsEndTheRun) {
    for (const std::string_view name :
         {"__builtin_setjmp", "__builtin_longjmp", "atexit", "signal", "fopen", "qsort", "scanf", "sscanf",
          "__isoc99_sscanf", "__builtin___memcpy_chk", "__builtin_return_address", "pthread_create",
          "pthread_mutex_lock", "thrd_create"}) {
        EXPECT_TRUE(endedWith(call(name, {}), LibraryResult::Kind::Unsupported, name)) << name;
    }
}

TEST_P(DispatchTest, MissingArgumentsAreUndefined) {
    EXPECT_TRUE(isUndefined(call("strlen", {}), "strlen is called with too few arguments"));
    EXPECT_TRUE(isUndefined(call("memcpy", {pointer(0), pointer(0)}), "too few arguments"));
}

TEST_P(DispatchTest, ArgumentsOfTheWrongKindAreUndefined) {
    EXPECT_TRUE(isUndefined(call("strlen", {value::Value::ofDouble(1)}), "argument 1 of strlen is not an integer"));
    EXPECT_TRUE(isUndefined(call("malloc", {value::Value::ofBytes({})}), "not an integer"));
}

TEST_P(DispatchTest, ExtraArgumentsAreIgnored) {
    EXPECT_EQ(callInteger("strlen", {pointer(string("ab")), intValue(5)}), 2);
}

TEST_P(DispatchTest, LibraryObjects) {
    for (const std::string_view name : {"stdin", "stdout", "stderr", "errno", "__random_state", "__ctype_b",
                                        "__ctype_tolower", "__ctype_toupper", "_IO_2_1_stdout_"}) {
        EXPECT_TRUE(libraryObject(name, context.target()).has_value()) << name;
    }
    EXPECT_FALSE(libraryObject("environ", context.target()).has_value());
    EXPECT_FALSE(libraryObject("malloc", context.target()).has_value());
    const std::optional<LibraryObject> stdoutObject = libraryObject("stdout", context.target());
    EXPECT_EQ(stdoutObject->size, lp64() ? 8u : 4u);
    EXPECT_EQ(stdoutObject->alignment, lp64() ? 8u : 4u);
    EXPECT_FALSE(stdoutObject->readOnly);
    EXPECT_TRUE(libraryObject("_nl_C_LC_CTYPE_class", context.target())->readOnly);
}

} // namespace
} // namespace nitwit::interp::test
