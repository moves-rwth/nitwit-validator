// GNU attributes and pragmas.

#include <gtest/gtest.h>

#include "ast/casting.hpp"
#include "parse_test_support.hpp"

namespace nitwit::parse {
namespace {

class GnuTest : public ParserTest {
protected:
    // The names of the attributes of `decl`.
    static std::vector<std::string> attributeNames(const ast::Decl* decl) {
        std::vector<std::string> names;
        for (const ast::Attribute& attribute : decl->attributes()) {
            names.emplace_back(attribute.name.text());
        }
        return names;
    }

    using Names = std::vector<std::string>;
};

TEST_F(GnuTest, AttributeNamesLoseTheirUnderscores) {
    const ast::TranslationUnit* unit = parse("int x __attribute__((__aligned__(8), unused, __const));");
    ASSERT_NE(unit, nullptr) << firstError();
    EXPECT_EQ(attributeNames(find<ast::VarDecl>(unit, "x")), (Names{"aligned", "unused", "const"}));
}

TEST_F(GnuTest, AttributesInSpecifiersApplyToEveryDeclarator) {
    const ast::TranslationUnit* unit = parse("__attribute__((unused)) static int a __attribute__((weak)), b;");
    ASSERT_NE(unit, nullptr) << firstError();
    EXPECT_EQ(attributeNames(find<ast::VarDecl>(unit, "a")), (Names{"unused", "weak"}));
    EXPECT_EQ(attributeNames(find<ast::VarDecl>(unit, "b")), (Names{"unused"}));
}

TEST_F(GnuTest, AttributesOnFunctionsAndParameters) {
    const ast::TranslationUnit* unit =
        parse("void fail(const char *m __attribute__((unused))) __attribute__((__noreturn__));\n"
              "int (__attribute__((cdecl)) *fp)(void);");
    ASSERT_NE(unit, nullptr) << firstError();
    const auto* function = find<ast::FunctionDecl>(unit, "fail");
    EXPECT_EQ(attributeNames(function), (Names{"noreturn"}));
    EXPECT_EQ(attributeNames(function->parameters()[0]), (Names{"unused"}));
    EXPECT_EQ(attributeNames(find<ast::VarDecl>(unit, "fp")), (Names{"cdecl"}));
}

TEST_F(GnuTest, AttributesOnRecordsApplyToTheRecord) {
    const ast::TranslationUnit* unit = parse("struct __attribute__((packed)) A { char c; int i; };\n"
                                             "struct B { char c; } __attribute__((aligned(16))) b;");
    ASSERT_NE(unit, nullptr) << firstError();
    EXPECT_EQ(attributeNames(find<ast::RecordDecl>(unit, "A")), (Names{"packed"}));
    EXPECT_EQ(attributeNames(find<ast::RecordDecl>(unit, "B")), (Names{"aligned"}));
    EXPECT_TRUE(find<ast::VarDecl>(unit, "b")->attributes().empty());
}

TEST_F(GnuTest, AttributesOnMembersEnumeratorsAndLabels) {
    const ast::TranslationUnit* unit = parse("struct S { int a __attribute__((packed)); };\n"
                                             "enum E { A __attribute__((deprecated)) = 1 };\n"
                                             "void f(void) { L: __attribute__((unused)); goto L; }");
    ASSERT_NE(unit, nullptr) << firstError();
    EXPECT_EQ(attributeNames(find<ast::RecordDecl>(unit, "S")->fields()[0]), (Names{"packed"}));
    EXPECT_EQ(attributeNames(find<ast::EnumDecl>(unit, "E")->constants()[0]), (Names{"deprecated"}));
}

TEST_F(GnuTest, EmptyAttributeLists) {
    EXPECT_NE(parse("int x __attribute__(()) __attribute__((,unused,));"), nullptr) << firstError();
}

TEST_F(GnuTest, PragmaPackAppliesToRecordsDefinedWhileItIsInEffect) {
    const ast::TranslationUnit* unit = parse("struct A { char c; };\n"
                                             "#pragma pack(2)\n"
                                             "struct B { char c; };\n"
                                             "#pragma pack()\n"
                                             "struct C { char c; };\n");
    ASSERT_NE(unit, nullptr) << firstError();
    EXPECT_EQ(find<ast::RecordDecl>(unit, "A")->maxFieldAlignment(), 0u);
    EXPECT_EQ(find<ast::RecordDecl>(unit, "B")->maxFieldAlignment(), 2u);
    EXPECT_EQ(find<ast::RecordDecl>(unit, "C")->maxFieldAlignment(), 0u);
}

TEST_F(GnuTest, PragmaPackPushAndPopNest) {
    const ast::TranslationUnit* unit = parse("#pragma pack(push, 4)\n"
                                             "struct A { char c; };\n"
                                             "#pragma pack(push)\n"
                                             "#pragma pack(1)\n"
                                             "struct B { char c; };\n"
                                             "#pragma pack(pop)\n"
                                             "struct C { char c; };\n"
                                             "#pragma pack(pop)\n"
                                             "struct D { char c; };\n"
                                             "#pragma pack(push, 8)\n"
                                             "#pragma pack(pop, 2)\n"
                                             "struct E { char c; };\n");
    ASSERT_NE(unit, nullptr) << firstError();
    EXPECT_EQ(find<ast::RecordDecl>(unit, "A")->maxFieldAlignment(), 4u);
    EXPECT_EQ(find<ast::RecordDecl>(unit, "B")->maxFieldAlignment(), 1u);
    EXPECT_EQ(find<ast::RecordDecl>(unit, "C")->maxFieldAlignment(), 4u);
    EXPECT_EQ(find<ast::RecordDecl>(unit, "D")->maxFieldAlignment(), 0u);
    EXPECT_EQ(find<ast::RecordDecl>(unit, "E")->maxFieldAlignment(), 2u);
    EXPECT_TRUE(messages().empty());
}

TEST_F(GnuTest, PragmaPackInsideAStructDefinitionAppliesWhereTheDefinitionEnds) {
    const ast::TranslationUnit* unit = parse("struct S {\n"
                                             "  char c;\n"
                                             "#pragma pack(1)\n"
                                             "  int i;\n"
                                             "};\n");
    ASSERT_NE(unit, nullptr) << firstError();
    EXPECT_EQ(find<ast::RecordDecl>(unit, "S")->maxFieldAlignment(), 1u);
    EXPECT_EQ(find<ast::RecordDecl>(unit, "S")->fields().size(), 2u);
}

TEST_F(GnuTest, PragmaPackIsShownInTheDump) {
    EXPECT_EQ(dumpOf("#pragma pack(push, 1)\nstruct S { int i; };\n#pragma pack(pop)\n"),
              "RecordDecl struct S max_field_align 1\n"
              "  FieldDecl i 'int'\n");
}

TEST_F(GnuTest, PragmaPackWithCommentsAndContinuedLines) {
    const ast::TranslationUnit* unit = parse("#pragma pack( /* two */ push, \\\n 2) // comment\nstruct S { int i; };");
    ASSERT_NE(unit, nullptr) << firstError();
    EXPECT_EQ(find<ast::RecordDecl>(unit, "S")->maxFieldAlignment(), 2u);
}

TEST_F(GnuTest, PopWithoutPushWarns) {
    ASSERT_NE(parse("#pragma pack(pop)\nint x;"), nullptr);
    EXPECT_EQ(messages(), std::vector<std::string>{
                              "test.c:1:1: warning: '#pragma pack(pop)' without a matching '#pragma pack(push)'"});
}

TEST_F(GnuTest, InvalidPackAlignmentIsIgnoredWithAWarning) {
    const ast::TranslationUnit* unit = parse("#pragma pack(3)\nstruct S { int i; };");
    ASSERT_NE(unit, nullptr);
    EXPECT_EQ(find<ast::RecordDecl>(unit, "S")->maxFieldAlignment(), 0u);
    EXPECT_TRUE(hasWarning("'#pragma pack' alignment must be 1, 2, 4, 8 or 16, not '3'"));
}

TEST_F(GnuTest, MalformedPackIsIgnoredWithAWarning) {
    ASSERT_NE(parse("#pragma pack 2\nint x;"), nullptr);
    EXPECT_TRUE(hasWarning("malformed '#pragma pack', ignored"));
}

TEST_F(GnuTest, PragmasBetweenStatementsAndInsideExpressions) {
    EXPECT_EQ(dumpOf("int f(void) {\n#pragma GCC diagnostic push\n  return 1 +\n#pragma unknown\n  2;\n}\n"),
              "FunctionDecl f 'int (void)'\n"
              "  CompoundStmt\n"
              "    ReturnStmt\n"
              "      BinaryExpr +\n"
              "        IntegerLiteral 1\n"
              "        IntegerLiteral 2\n");
    EXPECT_TRUE(messages().empty());
}

TEST_F(GnuTest, PragmaWeakMarksTheFunctionDeclaredBeforeOrAfterIt) {
    const ast::TranslationUnit* unit = parse("void before(void);\n"
                                             "#pragma weak before\n"
                                             "#pragma weak after\n"
                                             "void after(void);\n"
                                             "void after(void) {}\n"
                                             "void other(void);\n");
    ASSERT_NE(unit, nullptr) << firstError();
    EXPECT_TRUE(find<ast::FunctionDecl>(unit, "before")->isWeak());
    EXPECT_TRUE(find<ast::FunctionDecl>(unit, "after")->isWeak());
    EXPECT_TRUE(find<ast::FunctionDecl>(unit, "after")->previous()->isWeak());
    EXPECT_FALSE(find<ast::FunctionDecl>(unit, "other")->isWeak());
}

TEST_F(GnuTest, PragmaWeakIsShownInTheDump) {
    EXPECT_EQ(dumpOf("#pragma weak f\nvoid f(void);"), "FunctionDecl f 'void (void)' weak\n");
}

TEST_F(GnuTest, PragmaWeakAliasIsIgnoredWithAWarning) {
    const ast::TranslationUnit* unit = parse("void f(void);\n#pragma weak f = g\n");
    ASSERT_NE(unit, nullptr);
    EXPECT_FALSE(find<ast::FunctionDecl>(unit, "f")->isWeak());
    EXPECT_TRUE(hasWarning("'#pragma weak' with an alias is not supported, ignored"));
}

} // namespace
} // namespace nitwit::parse
