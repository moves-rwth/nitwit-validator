// Error messages and their locations for malformed and unsupported input, and the nesting limit.

#include <cstddef>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "parse_test_support.hpp"
#include "support/large_stack.hpp"

namespace nitwit::parse {
namespace {

using ErrorTest = ParserTest;

TEST_F(ErrorTest, MissingSemicolon) {
    EXPECT_EQ(errorOf("int x\nint y;"), "test.c:2:1: error: expected ';', found 'int'");
}

TEST_F(ErrorTest, UnexpectedEndOfFile) {
    EXPECT_EQ(errorOf("void f(void) { if (1) {"), "test.c:1:24: error: expected '}', found end of file");
}

TEST_F(ErrorTest, MissingExpression) {
    EXPECT_EQ(errorOf("int x = ;"), "test.c:1:9: error: expected expression, found ';'");
}

TEST_F(ErrorTest, UndeclaredIdentifier) {
    EXPECT_EQ(errorOf("int f(void) { return y; }"), "test.c:1:22: error: use of undeclared identifier 'y'");
}

TEST_F(ErrorTest, UnknownTypeName) {
    EXPECT_EQ(errorOf("foo x;"), "test.c:1:1: error: unknown type name 'foo'");
    EXPECT_EQ(errorOf("void f(void) { bar y; }"), "test.c:1:16: error: unknown type name 'bar'");
    EXPECT_EQ(errorOf("void g(int a, baz b);"), "test.c:1:15: error: unknown type name 'baz'");
    EXPECT_EQ(errorOf("void h(qux q);"), "test.c:1:8: error: unknown type name 'qux'");
}

TEST_F(ErrorTest, TypeNameUsedAsExpression) {
    EXPECT_EQ(errorOf("typedef int T; int f(void) { return T; }"),
              "test.c:1:37: error: unexpected type name 'T', expected an expression");
}

TEST_F(ErrorTest, InvalidTypeSpecifierCombinations) {
    EXPECT_EQ(errorOf("long char c;"), "test.c:1:6: error: invalid combination of type specifiers");
    EXPECT_EQ(errorOf("typedef int T; T int x;"), "test.c:1:18: error: invalid combination of type specifiers");
    EXPECT_EQ(errorOf("signed unsigned u;"), "test.c:1:8: error: invalid combination of type specifiers");
}

TEST_F(ErrorTest, MultipleStorageClasses) {
    EXPECT_EQ(errorOf("static extern int x;"), "test.c:1:8: error: multiple storage classes in declaration specifiers");
}

TEST_F(ErrorTest, StorageClassOfAMember) {
    EXPECT_EQ(errorOf("struct S { static int x; };"), "test.c:1:12: error: 'static' is not allowed here");
}

TEST_F(ErrorTest, InlineVariable) {
    EXPECT_EQ(errorOf("inline int x;"), "test.c:1:12: error: variable 'x' declared 'inline'");
}

TEST_F(ErrorTest, IntegerLiteralTooLarge) {
    EXPECT_EQ(errorOf("unsigned __int128 x = 100000000000000000000;"),
              "test.c:1:23: error: integer constant '100000000000000000000' is too large");
}

TEST_F(ErrorTest, CaseOutsideASwitch) {
    EXPECT_EQ(errorOf("void f(void) { case 1: ; }"), "test.c:1:16: error: 'case' label not within a switch statement");
    EXPECT_EQ(errorOf("void f(void) { default: ; }"),
              "test.c:1:16: error: 'default' label not within a switch statement");
}

TEST_F(ErrorTest, MultipleDefaultLabels) {
    EXPECT_EQ(errorOf("void f(int a) { switch (a) { default: ; default: ; } }"),
              "test.c:1:41: error: multiple default labels in one switch");
}

TEST_F(ErrorTest, BreakAndContinueOutsideLoops) {
    EXPECT_EQ(errorOf("void f(void) { break; }"),
              "test.c:1:16: error: 'break' statement not in loop or switch statement");
    EXPECT_EQ(errorOf("void f(int a) { switch (a) { default: continue; } }"),
              "test.c:1:39: error: 'continue' statement not in loop statement");
}

TEST_F(ErrorTest, DuplicateLabel) {
    EXPECT_EQ(errorOf("void f(void) { L: ; L: ; }"), "test.c:1:21: error: redefinition of label 'L'");
}

TEST_F(ErrorTest, ComputedGotoIsNotSupported) {
    EXPECT_EQ(errorOf("void f(void *p) { goto *p; }"), "test.c:1:24: error: computed goto is not supported");
    EXPECT_EQ(errorOf("void f(void) { L: ; void *p = &&L; }"),
              "test.c:1:31: error: taking the address of a label is not supported");
}

TEST_F(ErrorTest, NestedFunctionsAreNotSupported) {
    EXPECT_EQ(errorOf("void f(void) { void g(void) { } }"), "test.c:1:29: error: nested functions are not supported");
}

TEST_F(ErrorTest, StatementExpressionOutsideAFunction) {
    EXPECT_EQ(errorOf("int x = ({ 1; });"),
              "test.c:1:9: error: a statement expression is only allowed inside a function");
}

TEST_F(ErrorTest, AutoTypeRequiresAnInitializer) {
    EXPECT_EQ(errorOf("void f(void) { __auto_type x; }"), "test.c:1:28: error: '__auto_type' requires an initializer");
}

TEST_F(ErrorTest, VoidMustBeTheOnlyParameter) {
    EXPECT_EQ(errorOf("int f(int, void);"), "test.c:1:7: error: 'void' must be the only parameter");
}

TEST_F(ErrorTest, KnrDeclarationOfAnUnlistedParameter) {
    EXPECT_EQ(errorOf("int f(a) int b; { return a; }"),
              "test.c:1:14: error: declaration for parameter 'b' but no such parameter");
}

TEST_F(ErrorTest, NestedRedefinitionOfATag) {
    EXPECT_EQ(errorOf("struct S { struct S { int a; } inner; };"),
              "test.c:1:39: error: nested redefinition of 'struct S'");
    EXPECT_EQ(errorOf("enum E { A = sizeof(enum E { B }) };"), "test.c:1:35: error: nested redefinition of 'enum E'");
}

TEST_F(ErrorTest, EmptyEnum) {
    EXPECT_EQ(errorOf("enum E {};"), "test.c:1:9: error: empty enum is invalid");
}

TEST_F(ErrorTest, ParsingStopsAtTheFirstError) {
    EXPECT_EQ(parse("int x = ; int y = ;"), nullptr);
    EXPECT_EQ(messages().size(), 1u);
}

TEST_F(ErrorTest, DeepNestingWithinTheLimitParses) {
    // The parser recurses per nesting level; the host needs a large stack for this, as the driver provides.
    const std::size_t depth = 9000;
    const std::string text = "int x = " + std::string(depth, '(') + "1" + std::string(depth, ')') + ";";
    const ast::TranslationUnit* unit = nullptr;
    runWithLargeStack(std::size_t{256} << 20, [&] { unit = parse(text); });
    EXPECT_NE(unit, nullptr) << firstError();
}

TEST_F(ErrorTest, NestingBeyondTheLimitIsAnError) {
    const std::size_t depth = 10001;
    const std::string text = "int x = " + std::string(depth, '(') + "1" + std::string(depth, ')') + ";";
    const ast::TranslationUnit* unit = nullptr;
    runWithLargeStack(std::size_t{256} << 20, [&] { unit = parse(text); });
    EXPECT_EQ(unit, nullptr);
    EXPECT_EQ(firstError(), "test.c:1:10009: error: nesting is deeper than 10000 levels");
}

TEST_F(ErrorTest, DeeplyNestedTypeNamesAreLimited) {
    const auto nested = [](std::string_view wrapper) {
        std::string text;
        for (int i = 0; i < 10005; ++i) {
            text += wrapper;
        }
        return text + "int" + std::string(10005, ')');
    };
    const std::string atomic = nested("_Atomic(");
    const std::string typeOf = nested("typeof(");
    runWithLargeStack(std::size_t{256} << 20, [&] {
        EXPECT_EQ(parse(atomic + " x;"), nullptr);
        EXPECT_EQ(parse(typeOf + " x;"), nullptr);
    });
}

TEST_F(ErrorTest, DeeplyNestedBlocksAndDeclaratorsAreLimited) {
    const std::size_t depth = 10005;
    const std::string blocks = "void f(void) " + std::string(depth, '{') + std::string(depth, '}');
    const std::string declarator = "int " + std::string(depth, '(') + "x" + std::string(depth, ')') + ";";
    runWithLargeStack(std::size_t{256} << 20, [&] {
        EXPECT_EQ(parse(blocks), nullptr);
        EXPECT_EQ(parse(declarator), nullptr);
    });
}

} // namespace
} // namespace nitwit::parse
