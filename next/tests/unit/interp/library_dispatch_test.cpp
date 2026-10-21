#include <string_view>

#include <gtest/gtest.h>

#include "library_test_support.hpp"

namespace nitwit::interp::test {
namespace {

class DispatchTest : public LibraryTest {};

INSTANTIATE_TEST_SUITE_P(DataModels, DispatchTest, testing::Values(DataModel::ILP32, DataModel::LP64), modelName);

TEST(LibraryDispatchTest, FindsFunctionsByName) {
    for (const std::string_view name : {"malloc", "printf", "strlen", "__assert_fail", "rand", "__errno_location"}) {
        EXPECT_NE(findLibraryFunction(name), nullptr) << name;
    }
    EXPECT_EQ(findLibraryFunction("strlen"), findLibraryFunction("strlen"));
}

TEST(LibraryDispatchTest, KnowsNoOtherFunctions) {
    // The oracle answers calls of unknown functions; nondeterministic inputs and assume are the evaluator's.
    for (const std::string_view name : {"", "main", "reach_error", "__VERIFIER_nondet_int", "__VERIFIER_assume",
                                        "__builtin_", "__builtin_no_such_function", "sscanf"}) {
        EXPECT_EQ(findLibraryFunction(name), nullptr) << name;
    }
}

TEST(LibraryDispatchTest, BuiltinFormsOfLibraryFunctions) {
    EXPECT_EQ(findLibraryFunction("__builtin_memcpy"), findLibraryFunction("memcpy"));
    EXPECT_EQ(findLibraryFunction("__builtin_alloca"), findLibraryFunction("alloca"));
}

TEST_P(DispatchTest, UnsupportedFunctionsEndTheRun) {
    for (const std::string_view name : {"setjmp", "_setjmp", "longjmp", "atexit", "signal", "fopen", "qsort",
                                        "pthread_create", "pthread_mutex_lock", "thrd_create"}) {
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
    for (const std::string_view name : {"stdin", "stdout", "stderr", "errno", "__random_state", "_IO_2_1_stdout_"}) {
        EXPECT_TRUE(libraryObject(name, context.target()).has_value()) << name;
    }
    EXPECT_FALSE(libraryObject("environ", context.target()).has_value());
    EXPECT_FALSE(libraryObject("malloc", context.target()).has_value());
    const std::optional<LibraryObject> stdoutObject = libraryObject("stdout", context.target());
    EXPECT_EQ(stdoutObject->size, lp64() ? 8u : 4u);
    EXPECT_EQ(stdoutObject->alignment, lp64() ? 8u : 4u);
}

} // namespace
} // namespace nitwit::interp::test
