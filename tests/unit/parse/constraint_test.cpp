// Witness constraints (constraint.hpp): the scope rebuilt at a program point, `\result`, and the rejected forms.

#include "parse/constraint.hpp"

#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>

#include <gtest/gtest.h>

#include "ast/casting.hpp"
#include "parse_test_support.hpp"
#include "sema/expression.hpp"
#include "sema/sema.hpp"

namespace nitwit::parse {
namespace {

class ParseConstraintTest : public ParserTest {
protected:
    // Parses the program the constraints refer to. Locations in it are marked with comments like `/*A*/`.
    bool parseProgram(std::string text) {
        programText_ = text;
        unit_ = parse(std::move(text));
        programFile_ = file_;
        return unit_ != nullptr;
    }

    // Parses and analyzes the program, as the validator does before it parses constraints.
    bool program(std::string text) {
        return parseProgram(std::move(text)) && sema::analyze(*unit_, context_, diagnostics_);
    }

    // The location right after the comment `/*marker*/` in the program.
    [[nodiscard]] SourceLocation at(std::string_view marker) const {
        const std::string comment = std::format("/*{}*/", marker);
        const std::size_t offset = programText_.find(comment);
        EXPECT_NE(offset, std::string::npos) << "no marker " << marker;
        return {programFile_, static_cast<std::uint32_t>(offset + comment.size())};
    }

    // Parses `text` as a constraint at the marker in the function; without a function, at file scope.
    ast::Expr* constraint(std::string_view text, std::string_view function = "", std::string_view marker = "") {
        ConstraintScope scope{.unit = unit_, .function = nullptr, .location = {}};
        if (!function.empty()) {
            scope.function = find<ast::FunctionDecl>(unit_, function);
            scope.location = marker.empty() ? SourceLocation() : at(marker);
        }
        firstDiagnostic_ = diagnostics_.diagnostics().size();
        return parseConstraint(text, scope, context_, sources_, diagnostics_);
    }

    // Where the declaration that the name `name` binds to is, as "line:column"; the error if there is none.
    std::string bindingOf(std::string_view name, std::string_view function = "", std::string_view marker = "") {
        const ast::Expr* expr = constraint(name, function, marker);
        if (expr == nullptr) {
            return firstError();
        }
        const auto* ref = ast::dyn_cast<ast::DeclRefExpr>(expr);
        return ref != nullptr ? position(ref->decl()->location()) : "not a name";
    }

    // The dump of the parsed constraint, without positions.
    std::string constraintDump(std::string_view text, std::string_view function = "", std::string_view marker = "") {
        const ast::Expr* expr = constraint(text, function, marker);
        EXPECT_NE(expr, nullptr) << firstError();
        return expr != nullptr ? withoutPositions(ast::dump(expr, sources_)) : "";
    }

    // Whether the constraint parses and sema accepts it; `resultType` is the type of `\result`.
    bool analyzes(std::string_view text, std::string_view function = "", std::string_view marker = "",
                  ast::QualType resultType = {}) {
        ast::Expr* expr = constraint(text, function, marker);
        return expr != nullptr && sema::analyzeConstraint(expr, resultType, context_, diagnostics_) != nullptr;
    }

    // The error of the constraint `text`, which must fail.
    std::string constraintError(std::string_view text, std::string_view function = "", std::string_view marker = "") {
        EXPECT_EQ(constraint(text, function, marker), nullptr);
        return firstError();
    }

    [[nodiscard]] ast::QualType intType() const { return context_.builtin(ast::BuiltinKind::Int); }

    ast::TranslationUnit* unit_ = nullptr;
    std::string programText_;
    FileId programFile_ = FileId::None;
};

// ---- Scopes ----

// Blocks, a declaration with three declarators and a for loop; markers before the declared names and at the locations.
constexpr std::string_view blocksProgram = "int /*g*/x;\n"
                                           "void f(int /*p*/p) {\n"
                                           "    int /*a*/a = 1;\n"
                                           "    /*A*/if (a) {\n"
                                           "        int /*x1*/x = 2;\n"
                                           "        /*B*/a = x;\n"
                                           "    } else {\n"
                                           "        int /*e*/e = 3;\n"
                                           "        /*C*/a = e;\n"
                                           "    }\n"
                                           "    /*D*/int /*b*/b = 4, /*c*/c = /*E*/b, /*d*/d = 5;\n"
                                           "    for (int /*i*/i = 0; i < 3; i++) {\n"
                                           "        /*F*/a += i;\n"
                                           "    }\n"
                                           "    /*G*/;\n"
                                           "}\n";

TEST_F(ParseConstraintTest, ParametersAreVisibleInTheBody) {
    ASSERT_TRUE(program(std::string(blocksProgram))) << firstError();
    EXPECT_EQ(bindingOf("p", "f", "A"), position(at("p")));
    EXPECT_EQ(bindingOf("p", "f", "F"), position(at("p")));
}

TEST_F(ParseConstraintTest, ParametersAreVisibleOutsideTheBody) {
    ASSERT_TRUE(program("void /*F*/f(int /*p*/p) {\n  int l = p;\n}\n")) << firstError();
    EXPECT_EQ(bindingOf("p", "f", "F"), position(at("p")));
    EXPECT_EQ(bindingOf("p", "f"), position(at("p")));
    EXPECT_EQ(bindingOf("l", "f", "F"), "<constraint>:1:1: error: use of undeclared identifier 'l'");
}

TEST_F(ParseConstraintTest, InnerDeclarationsShadowOuterOnes) {
    ASSERT_TRUE(program(std::string(blocksProgram))) << firstError();
    EXPECT_EQ(bindingOf("x", "f", "A"), position(at("g")));
    EXPECT_EQ(bindingOf("x", "f", "B"), position(at("x1")));
    EXPECT_EQ(bindingOf("a", "f", "B"), position(at("a")));
}

TEST_F(ParseConstraintTest, SiblingBlocksAreNotVisible) {
    ASSERT_TRUE(program(std::string(blocksProgram))) << firstError();
    EXPECT_EQ(bindingOf("e", "f", "C"), position(at("e")));
    EXPECT_EQ(bindingOf("x", "f", "C"), position(at("g")));
    EXPECT_EQ(bindingOf("e", "f", "G"), "<constraint>:1:1: error: use of undeclared identifier 'e'");
}

TEST_F(ParseConstraintTest, DeclarationsAtOrAfterTheLocationAreNotVisible) {
    ASSERT_TRUE(program(std::string(blocksProgram))) << firstError();
    EXPECT_EQ(bindingOf("b", "f", "A"), "<constraint>:1:1: error: use of undeclared identifier 'b'");
    EXPECT_EQ(bindingOf("b", "f", "D"), "<constraint>:1:1: error: use of undeclared identifier 'b'");
}

TEST_F(ParseConstraintTest, ADeclaratorIsInScopeAfterItsCompletion) {
    // C17 6.2.1p7: c is in scope in its own initializer, d is not yet.
    ASSERT_TRUE(program(std::string(blocksProgram))) << firstError();
    EXPECT_EQ(bindingOf("b", "f", "E"), position(at("b")));
    EXPECT_EQ(bindingOf("c", "f", "E"), position(at("c")));
    EXPECT_EQ(bindingOf("d", "f", "E"), "<constraint>:1:1: error: use of undeclared identifier 'd'");
    EXPECT_EQ(bindingOf("d", "f", "G"), position(at("d")));
}

TEST_F(ParseConstraintTest, ForDeclarationIsScopedToTheLoop) {
    ASSERT_TRUE(program(std::string(blocksProgram))) << firstError();
    EXPECT_EQ(bindingOf("i", "f", "F"), position(at("i")));
    EXPECT_EQ(bindingOf("i", "f", "G"), "<constraint>:1:1: error: use of undeclared identifier 'i'");
}

TEST_F(ParseConstraintTest, BlocksInsideSwitchLoopsAndLabelsAreEntered) {
    ASSERT_TRUE(program("void g(int n) {\n"
                        "    switch (n) {\n"
                        "    case 1: {\n"
                        "        int /*s*/s = 1;\n"
                        "        /*S*/n = s;\n"
                        "        break;\n"
                        "    }\n"
                        "    case 2:\n"
                        "        int /*v*/v = 2;\n"
                        "        /*V*/n = v;\n"
                        "    }\n"
                        "    while (n) { int /*w*/w = 0; /*W*/n = w; }\n"
                        "    do { int /*d*/d = 0; /*DO*/n = d; } while (n);\n"
                        "    lab: { int /*l*/l = 0; /*L*/n = l; }\n"
                        "    goto lab;\n"
                        "}\n"))
        << firstError();
    EXPECT_EQ(bindingOf("s", "g", "S"), position(at("s")));
    EXPECT_EQ(bindingOf("v", "g", "V"), position(at("v")));
    EXPECT_EQ(bindingOf("s", "g", "V"), "<constraint>:1:1: error: use of undeclared identifier 's'");
    EXPECT_EQ(bindingOf("w", "g", "W"), position(at("w")));
    EXPECT_EQ(bindingOf("d", "g", "DO"), position(at("d")));
    EXPECT_EQ(bindingOf("l", "g", "L"), position(at("l")));
}

TEST_F(ParseConstraintTest, DeepElseIfChainsAreFollowed) {
    // The scopes are found in a loop: else-if chains have no depth limit. The chain is too deep for sema's recursion
    // on this stack, so the program is only parsed.
    std::string text = "void z(int n) {\n    if (n == 0) {}\n";
    for (int i = 1; i < 20000; ++i) {
        text += std::format("    else if (n == {}) {{}}\n", i);
    }
    text += "    else { int /*deep*/deep = 0; /*Z*/n = deep; }\n}\n";
    ASSERT_TRUE(parseProgram(std::move(text))) << firstError();
    EXPECT_EQ(bindingOf("deep", "z", "Z"), position(at("deep")));
}

TEST_F(ParseConstraintTest, WithoutAFunctionOnlyFileScopeIsVisible) {
    ASSERT_TRUE(program(std::string(blocksProgram))) << firstError();
    EXPECT_EQ(bindingOf("x"), position(at("g")));
    EXPECT_EQ(bindingOf("f"), "2:6");
    EXPECT_EQ(bindingOf("a"), "<constraint>:1:1: error: use of undeclared identifier 'a'");
}

TEST_F(ParseConstraintTest, GlobalsDeclaredAfterTheLocationAreVisible) {
    ASSERT_TRUE(program("void f(void) {\n  /*A*/;\n}\nint /*late*/late;\n")) << firstError();
    EXPECT_EQ(bindingOf("late", "f", "A"), position(at("late")));
}

TEST_F(ParseConstraintTest, TypedefsAndCastsInScope) {
    ASSERT_TRUE(program("typedef unsigned char u8;\n"
                        "int x;\n"
                        "void f(void) {\n"
                        "    typedef long L;\n"
                        "    /*A*/;\n"
                        "}\n"))
        << firstError();
    EXPECT_EQ(constraintDump("(u8)x == 1", "f", "A"), "BinaryExpr ==\n"
                                                      "  CastExpr (u8)\n"
                                                      "    DeclRefExpr x (VarDecl at 2:5)\n"
                                                      "  IntegerLiteral 1\n");
    EXPECT_TRUE(analyzes("(L)x == 0L && sizeof(L) == 8", "f", "A")) << firstError();
    EXPECT_TRUE(analyzes("(unsigned char)x == 255", "f", "A")) << firstError();
    EXPECT_EQ(constraintError("(L)x == 0"), "<constraint>:1:2: error: use of undeclared identifier 'L'");
}

TEST_F(ParseConstraintTest, LocalVariableHidesATypedef) {
    ASSERT_TRUE(program("typedef int T;\nvoid f(void) {\n    int T = 1;\n    /*A*/;\n}\n")) << firstError();
    EXPECT_NE(ast::dyn_cast<ast::BinaryExpr>(constraint("(T) - 1", "f", "A")), nullptr) << firstError();
    EXPECT_NE(ast::dyn_cast<ast::CastExpr>(constraint("(T) - 1")), nullptr) << firstError();
}

TEST_F(ParseConstraintTest, MemberAccess) {
    ASSERT_TRUE(program("struct S { int a; struct S *next; };\n"
                        "void f(struct S *p) {\n"
                        "    struct S s = { 0, 0 };\n"
                        "    /*A*/;\n"
                        "}\n"))
        << firstError();
    EXPECT_TRUE(analyzes("s.a == 1 && p->next->a > 0 && (*p).a != s.a", "f", "A")) << firstError();
}

TEST_F(ParseConstraintTest, BlockScopeTagHidesTheFileScopeTag) {
    ASSERT_TRUE(program("struct S { int a; };\n"
                        "void f(void) {\n"
                        "    struct S { int b; } s;\n"
                        "    /*A*/;\n"
                        "}\n"))
        << firstError();
    EXPECT_TRUE(analyzes("((struct S *)&s)->b == 0", "f", "A")) << firstError();
    EXPECT_FALSE(analyzes("((struct S *)&s)->a == 0", "f", "A"));
    EXPECT_TRUE(analyzes("((struct S *)0)->a == 0")) << firstError();
}

TEST_F(ParseConstraintTest, EnumerationConstantsInScope) {
    ASSERT_TRUE(program("enum { ONE = 1 };\nvoid f(void) {\n    enum { TWO = 2 };\n    /*A*/;\n}\n")) << firstError();
    EXPECT_TRUE(analyzes("ONE + TWO == 3", "f", "A")) << firstError();
    EXPECT_EQ(bindingOf("TWO"), "<constraint>:1:1: error: use of undeclared identifier 'TWO'");
}

// ---- The program is unchanged ----

TEST_F(ParseConstraintTest, TagDefinedInAConstraintLeavesTheProgramsTagAlone) {
    ASSERT_TRUE(program("void f(void) {\n    struct S;\n    struct S *p = 0;\n    /*A*/;\n}\n")) << firstError();
    const auto* declaration = ast::cast<ast::DeclarationStmt>(bodyOf(unit_, "f")[0]);
    const auto* tag = ast::cast<ast::RecordDecl>(declaration->decls()[0]);
    EXPECT_NE(constraint("((struct S { int a; } *)p)->a == 0", "f", "A"), nullptr) << firstError();
    EXPECT_FALSE(tag->isComplete());
}

TEST_F(ParseConstraintTest, ScopesDoNotLeakIntoLaterConstraints) {
    ASSERT_TRUE(program(std::string(blocksProgram))) << firstError();
    EXPECT_EQ(bindingOf("x", "f", "B"), position(at("x1")));
    EXPECT_EQ(bindingOf("x"), position(at("g")));
    EXPECT_EQ(bindingOf("a"), "<constraint>:1:1: error: use of undeclared identifier 'a'");
}

TEST_F(ParseConstraintTest, UndeclaredFunctionsAreNotDeclaredImplicitly) {
    ASSERT_TRUE(program("int x;")) << firstError();
    EXPECT_EQ(constraintError("g(x) == 0"), "<constraint>:1:1: error: use of undeclared identifier 'g'");
    EXPECT_EQ(diagnostics().size(), 1u); // no warning about an implicit declaration
}

// ---- \result ----

TEST_F(ParseConstraintTest, ResultIsAnExpression) {
    ASSERT_TRUE(program("int x;")) << firstError();
    EXPECT_EQ(constraintDump("\\result == 5"), "BinaryExpr ==\n"
                                               "  ResultExpr\n"
                                               "  IntegerLiteral 5\n");
    const auto* compare = ast::dyn_cast<ast::BinaryExpr>(constraint("x == \\result"));
    ASSERT_NE(compare, nullptr) << firstError();
    EXPECT_EQ(position(compare->rhs()->range().begin), "1:6");
    EXPECT_EQ(position(compare->rhs()->range().end), "1:13");
}

TEST_F(ParseConstraintTest, ResultConstraintsAnalyzeWithAResultType) {
    ASSERT_TRUE(program("void f(void) {\n  /*A*/;\n}\n")) << firstError();
    EXPECT_TRUE(analyzes("\\result == 5", "f", "A", intType())) << firstError();
    EXPECT_TRUE(analyzes("\\result >= 0 && \\result < 10", "f", "A", intType())) << firstError();
    EXPECT_TRUE(analyzes("(\\result == 0)", "", "", intType())) << firstError();
    EXPECT_FALSE(analyzes("\\result == 5", "f", "A"));
}

TEST_F(ParseConstraintTest, OtherBackslashesAreErrors) {
    ASSERT_TRUE(program("int x;")) << firstError();
    EXPECT_EQ(constraintError("\\ result == 1"), "<constraint>:1:1: error: stray '\\' in constraint");
    EXPECT_EQ(constraintError("x == \\"), "<constraint>:1:6: error: stray '\\' in constraint");
    EXPECT_EQ(constraintError("\\old(x) == 1"),
              "<constraint>:1:1: error: '\\old' is not supported in a constraint, only '\\result'");
    EXPECT_EQ(constraintError("\\resultx == 1"),
              "<constraint>:1:1: error: '\\resultx' is not supported in a constraint, only '\\result'");
    EXPECT_EQ(diagnostics().size(), 1u);
}

// ---- Forms that CPAchecker and others write ----

TEST_F(ParseConstraintTest, ProducerForms) {
    ASSERT_TRUE(program("int x; unsigned u; long long y; double z;")) << firstError();
    EXPECT_EQ(constraintDump("1"), "IntegerLiteral 1\n");
    EXPECT_EQ(constraintDump("(x == 5)"), "ParenExpr\n"
                                          "  BinaryExpr ==\n"
                                          "    DeclRefExpr x (VarDecl at 1:5)\n"
                                          "    IntegerLiteral 5\n");
    for (const std::string_view text : {"1", "(x == 5)", "u == 5U", "(long long)x == -1LL", "(z == -2.0)",
                                        "(y == 9223372036854775807LL)", "(x == 1) && (u == 2U)", "x != 0 || !u"}) {
        EXPECT_TRUE(analyzes(text)) << text << ": " << firstError();
    }
}

// ---- Errors ----

TEST_F(ParseConstraintTest, SyntaxErrors) {
    ASSERT_TRUE(program("int x;")) << firstError();
    EXPECT_EQ(constraintError("x =="), "<constraint>:1:5: error: expected expression, found end of file");
    EXPECT_EQ(constraintError(""), "<constraint>:1:1: error: expected expression, found end of file");
    EXPECT_EQ(constraintError("(x == 1"), "<constraint>:1:8: error: expected ')', found end of file");
}

TEST_F(ParseConstraintTest, UnknownIdentifiersAreErrors) {
    ASSERT_TRUE(program("int x;")) << firstError();
    EXPECT_EQ(constraintError("y == 1"), "<constraint>:1:1: error: use of undeclared identifier 'y'");
}

TEST_F(ParseConstraintTest, TypeNamesAreNotExpressions) {
    ASSERT_TRUE(program("typedef int T;")) << firstError();
    EXPECT_EQ(constraintError("T == 1"), "<constraint>:1:1: error: unexpected type name 'T', expected an expression");
}

TEST_F(ParseConstraintTest, TrailingTokensAreErrors) {
    ASSERT_TRUE(program("int x;")) << firstError();
    EXPECT_EQ(constraintError("x == 5 x"), "<constraint>:1:8: error: expected end of constraint, found identifier 'x'");
    EXPECT_EQ(constraintError("x == 1, x == 2"), "<constraint>:1:7: error: expected end of constraint, found ','");
    EXPECT_EQ(constraintError("x == 1;"), "<constraint>:1:7: error: expected end of constraint, found ';'");
}

TEST_F(ParseConstraintTest, CommaInParenthesesIsAnExpression) {
    ASSERT_TRUE(program("int x;")) << firstError();
    EXPECT_TRUE(analyzes("(x, x == 1)")) << firstError();
}

TEST_F(ParseConstraintTest, AssignmentsAreErrors) {
    ASSERT_TRUE(program("int x;")) << firstError();
    EXPECT_EQ(constraintError("x = 5"), "<constraint>:1:3: error: a constraint cannot contain an assignment");
    EXPECT_EQ(constraintError("x == (x += 1)"), "<constraint>:1:9: error: a constraint cannot contain an assignment");
}

TEST_F(ParseConstraintTest, IncrementsAndDecrementsAreErrors) {
    ASSERT_TRUE(program("int x;")) << firstError();
    EXPECT_EQ(constraintError("x++ == 1"),
              "<constraint>:1:2: error: a constraint cannot contain an increment or decrement");
    EXPECT_EQ(constraintError("--x == 1"),
              "<constraint>:1:1: error: a constraint cannot contain an increment or decrement");
}

TEST_F(ParseConstraintTest, CallsAreErrors) {
    ASSERT_TRUE(program("int x; int f(int a);")) << firstError();
    EXPECT_EQ(constraintError("x == f(1)"), "<constraint>:1:6: error: a constraint cannot contain a function call");
    EXPECT_TRUE(analyzes("f != 0")) << firstError();
}

TEST_F(ParseConstraintTest, StatementExpressionsAndVaArgAreErrors) {
    ASSERT_TRUE(program("int x; __builtin_va_list ap;")) << firstError();
    EXPECT_EQ(constraintError("({ x; }) == 1"),
              "<constraint>:1:1: error: a constraint cannot contain a statement expression");
    EXPECT_EQ(constraintError("__builtin_va_arg(ap, int) == 1"),
              "<constraint>:1:1: error: a constraint cannot contain '__builtin_va_arg'");
}

TEST_F(ParseConstraintTest, LexerErrorsAreReported) {
    ASSERT_TRUE(program("int x;")) << firstError();
    EXPECT_EQ(constraintError("x == @"), "<constraint>:1:6: error: stray '@' in program");
    EXPECT_EQ(constraintError("#define X 1"),
              "<constraint>:1:1: error: a constraint cannot contain preprocessing directives");
}

TEST_F(ParseConstraintTest, NestingIsLimited) {
    // Constraints are parsed on the caller's stack, so the limit is far below the program's.
    ASSERT_TRUE(program("int x;")) << firstError();
    const auto nested = [](std::size_t depth) {
        return std::string(depth, '(') + "x" + std::string(depth, ')');
    };
    EXPECT_NE(constraint(nested(255)), nullptr) << firstError();
    EXPECT_EQ(constraintError(nested(256)), "<constraint>:1:257: error: nesting is deeper than 256 levels");
}

} // namespace
} // namespace nitwit::parse
