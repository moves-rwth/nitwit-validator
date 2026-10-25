// Guards of the program (guard.hpp; validate/README.md, "Guards").

#include "validate/guard.hpp"

#include <optional>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "validate_test_support.hpp"

namespace nitwit::validate::test {
namespace {

constexpr std::string_view ifsProgram = R"(extern void abort(void);
extern void exit(int);
extern void _Exit(int);
extern void __assert_fail(const char *, const char *, unsigned int, const char *);
void reach_error(void);
void other(void);
int main(void) {
    int a = 0;
    if (!a) abort();
    if (a) {} else exit(0);
    if (a) { { L: _Exit(1); } }
    if (a) ; else __assert_fail("a", "test.c", 12, "main");
    if (a) (abort());
    if (a) { abort(); a = 1; }
    if (a) abort(); else exit(0);
    if (a) { reach_error(); abort(); }
    if (a) other();
    return 0;
}
)";

class GuardTest : public ValidateTest {
protected:
    void SetUp() override { program(std::string(ifsProgram)); }

    /// The guard that the if in `line` is, with `errorFunction` as the error function.
    std::optional<IfGuard> guardIn(std::uint32_t line, std::string_view errorFunction = "reach_error") const {
        return guardOf(*ast::cast<ast::IfStmt>(statementAt(line, 5)), errorFunction);
    }

    /// The function that ends the run at the guard in `line`; empty if it is no guard.
    std::string endingIn(std::uint32_t line) const {
        const std::optional<IfGuard> guard = guardIn(line);
        return guard ? std::string(guard->ending->name().text()) : std::string();
    }
};

TEST_F(GuardTest, ABranchThatOnlyAbortsMakesAGuardThatContinuesWhenFalse) {
    const std::optional<IfGuard> guard = guardIn(9);
    ASSERT_TRUE(guard.has_value());
    EXPECT_FALSE(guard->continuingOutcome);
    EXPECT_EQ(guard->ending->name().text(), "abort");
}

TEST_F(GuardTest, AnElseBranchThatExitsMakesAGuardThatContinuesWhenTrue) {
    const std::optional<IfGuard> guard = guardIn(10);
    ASSERT_TRUE(guard.has_value());
    EXPECT_TRUE(guard->continuingOutcome);
    EXPECT_EQ(guard->ending->name().text(), "exit");
}

TEST_F(GuardTest, BlocksLabelsAndParenthesesAroundTheCallAreSkipped) {
    EXPECT_EQ(endingIn(11), "_Exit");
    EXPECT_EQ(endingIn(13), "abort");
}

TEST_F(GuardTest, TheAssertMacroMakesAGuard) {
    // glibc's assert in C: `if (expr) ; else __assert_fail(...);`.
    const std::optional<IfGuard> guard = guardIn(12);
    ASSERT_TRUE(guard.has_value());
    EXPECT_TRUE(guard->continuingOutcome);
    EXPECT_EQ(guard->ending->name().text(), "__assert_fail");
}

TEST_F(GuardTest, ABranchWithMoreThanTheCallMakesNoGuard) {
    EXPECT_EQ(endingIn(14), "");
    // The __VERIFIER_assert of sv-benchmarks.
    EXPECT_EQ(endingIn(16), "");
}

TEST_F(GuardTest, TwoBranchesThatEndTheRunMakeNoGuard) {
    EXPECT_EQ(endingIn(15), "");
}

TEST_F(GuardTest, OtherFunctionsMakeNoGuard) {
    EXPECT_EQ(endingIn(17), "");
}

TEST_F(GuardTest, TheErrorFunctionMakesNoGuard) {
    EXPECT_FALSE(guardIn(9, "abort").has_value());
}

/// The function `name` as the program declares or defines it last.
const ast::FunctionDecl* declaredFunction(const ast::TranslationUnit& unit, std::string_view name) {
    const ast::FunctionDecl* found = nullptr;
    for (const ast::Decl* decl : unit.decls) {
        if (const auto* function = ast::dyn_cast<ast::FunctionDecl>(decl);
            function != nullptr && function->name().text() == name) {
            found = function;
        }
    }
    return found;
}

TEST_F(GuardTest, OnlyAnUndefinedVerifierAssumeIsTheBuiltin) {
    program("extern void __VERIFIER_assume(int);\nvoid other(int);\n");
    EXPECT_TRUE(isBuiltinAssume(declaredFunction(unit(), "__VERIFIER_assume")));
    EXPECT_FALSE(isBuiltinAssume(declaredFunction(unit(), "other")));
    EXPECT_FALSE(isBuiltinAssume(nullptr));
    program("extern void abort(void);\nvoid __VERIFIER_assume(int c) { if (!c) abort(); }\n");
    EXPECT_FALSE(isBuiltinAssume(declaredFunction(unit(), "__VERIFIER_assume")));
}

} // namespace
} // namespace nitwit::validate::test
