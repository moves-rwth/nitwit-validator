// Constraint compilation (constraints.hpp): the scope at the event, conjuncts, relations, `\result`, branching values.

#include "validate/constraints.hpp"

#include <string>

#include <gtest/gtest.h>

#include "ast/dump.hpp"
#include "validate_test_support.hpp"

namespace nitwit::validate::test {
namespace {

constexpr std::string_view source = R"(extern int __VERIFIER_nondet_int(void);
enum { LIMIT = 10 };
int g;
long f(int);
int main(void) {
    int x = __VERIFIER_nondet_int();
    long y = f(x);
    int z = 0;
    return 0;
}
)";

class ConstraintsTest : public ValidateTest {
protected:
    void SetUp() override {
        program(std::string(source));
        compiler_ = std::make_unique<ConstraintCompiler>(*input_, diagnostics_);
    }

    /// The location of `line:column` in the program.
    [[nodiscard]] SourceLocation at(std::uint32_t line, std::uint32_t column) const {
        return *sources_.location(file_, {line, column});
    }

    /// Compiles `text` at `line:column` (by default the return statement, where all locals are visible) as the
    /// constraint of the waypoint `index`; without an index, of a new waypoint, since compiled constraints are cached
    /// per waypoint.
    std::expected<const CompiledConstraint*, std::string> compile(std::string_view text, std::uint32_t line = 9,
                                                                  std::uint32_t column = 5,
                                                                  ast::QualType resultType = {},
                                                                  std::optional<std::uint32_t> index = std::nullopt) {
        const WaypointId id{.segment = 0, .index = index.value_or(nextIndex_++)};
        return compiler_->compile(id, text, functionNamed("main"), at(line, column), resultType);
    }

    /// "subject op constant" of each conjunct, with "-" for conjuncts that are no such relation.
    std::string relations(std::string_view text) {
        const auto compiled = compile(text);
        EXPECT_TRUE(compiled.has_value()) << compiled.error();
        if (!compiled) {
            return "";
        }
        static constexpr std::string_view ops[] = {"==", "!=", "<", "<=", ">", ">="};
        std::string described;
        for (const Conjunct& conjunct : (*compiled)->conjuncts) {
            if (!described.empty()) {
                described += "; ";
            }
            if (conjunct.subject == nullptr) {
                described += "-";
                continue;
            }
            described += std::format("{}:{} {}", sources_.lineColumn(conjunct.subject->location()).column,
                                     ops[static_cast<std::size_t>(conjunct.op)],
                                     sources_.lineColumn(conjunct.constant->location()).column);
        }
        return described;
    }

    std::unique_ptr<ConstraintCompiler> compiler_;
    std::uint32_t nextIndex_ = 100;
};

TEST_F(ConstraintsTest, SplitsTopLevelConjunctions) {
    const auto compiled = compile("(x == 1) && (y > 2 && z) && (x || z)");
    ASSERT_TRUE(compiled.has_value()) << compiled.error();
    EXPECT_EQ((*compiled)->conjuncts.size(), 4u);
}

TEST_F(ConstraintsTest, RecognizesRelationsWithAConstantOnEitherSide) {
    // Columns in the constraint text: the subject, then the constant.
    EXPECT_EQ(relations("x == 1"), "1:== 6");
    EXPECT_EQ(relations("-3 > x"), "6:< 1");
    EXPECT_EQ(relations("x <= LIMIT + 1 && 2 != y"), "1:<= 6; 24:!= 19");
    EXPECT_EQ(relations("x >= (int)2.5"), "1:>= 6");
}

TEST_F(ConstraintsTest, OtherConjunctsAreNoRelations) {
    EXPECT_EQ(relations("x == z"), "-");
    EXPECT_EQ(relations("x"), "-");
    EXPECT_EQ(relations("x + 1 == 2"), "1:== 10");
    EXPECT_EQ(relations("1 == 2"), "-");
    EXPECT_EQ(relations("g == x"), "-");
}

TEST_F(ConstraintsTest, ResultHasTheTypeOfTheCall) {
    const auto compiled = compile("\\result == 5", 7, 17, context().builtin(ast::BuiltinKind::Long));
    ASSERT_TRUE(compiled.has_value()) << compiled.error();
    EXPECT_EQ(ast::typeToString((*compiled)->conjuncts[0].subject->type()), "long");
}

TEST_F(ConstraintsTest, NamesBindAsAtTheEventLocation) {
    // z is declared in line 8; before the end of its declarator, the constraint cannot name it.
    EXPECT_TRUE(compile("z == 0", 9, 5).has_value());
    const auto beforeDeclaration = compile("z == 0", 8, 5);
    ASSERT_FALSE(beforeDeclaration.has_value());
    EXPECT_EQ(beforeDeclaration.error(), "<constraint>:1:1: error: use of undeclared identifier 'z'");
}

TEST_F(ConstraintsTest, ErrorsAreReportedToTheDiagnostics) {
    const auto compiled = compile("x ==");
    ASSERT_FALSE(compiled.has_value());
    EXPECT_TRUE(compiled.error().starts_with("<constraint>:1:5: error:")) << compiled.error();
    EXPECT_EQ(firstError(), compiled.error());
}

TEST_F(ConstraintsTest, ResultIsOnlyAllowedAtCalls) {
    const auto compiled = compile("\\result == 5");
    ASSERT_FALSE(compiled.has_value());
    EXPECT_EQ(compiled.error(),
              "<constraint>:1:1: error: '\\result' is only allowed in constraints at the return of a call");
}

TEST_F(ConstraintsTest, CompiledOncePerWaypointAndLocation) {
    const auto first = compile("x == 1", 9, 5, {}, 0);
    const auto again = compile("x == 1", 9, 5, {}, 0);
    const auto elsewhere = compile("x == 1", 8, 5, {}, 0);
    ASSERT_TRUE(first && again && elsewhere);
    EXPECT_EQ(*first, *again);
    EXPECT_NE(*first, *elsewhere);
}

class BranchValuesTest : public ConstraintsTest {
protected:
    std::string expectation(std::string_view text) {
        const auto branch =
            compiler_->branch({.segment = 0, .index = nextIndex_++}, text, functionNamed("main"), at(9, 5));
        if (!branch) {
            return branch.error();
        }
        switch (branch->kind) {
        case BranchExpectation::Kind::True:
            return "true";
        case BranchExpectation::Kind::False:
            return "false";
        case BranchExpectation::Kind::Default:
            return "default";
        case BranchExpectation::Kind::Case:
            return std::format("case {}", static_cast<long long>(branch->caseValue));
        }
        return "?";
    }
};

TEST_F(BranchValuesTest, OutcomesAndDefaultIgnoreCaseAndBlanks) {
    EXPECT_EQ(expectation("true"), "true");
    EXPECT_EQ(expectation(" FALSE\n"), "false");
    EXPECT_EQ(expectation("Default"), "default");
}

TEST_F(BranchValuesTest, CaseValuesAreIntegerConstantExpressions) {
    EXPECT_EQ(expectation("3"), "case 3");
    EXPECT_EQ(expectation("-1"), "case -1");
    EXPECT_EQ(expectation("'a'"), "case 97");
    EXPECT_EQ(expectation("LIMIT * 2"), "case 20");
}

TEST_F(BranchValuesTest, OtherValuesAreErrors) {
    EXPECT_EQ(expectation("x"), "the branching constraint 'x' is neither true, false, default nor an integer constant");
    EXPECT_EQ(expectation("1.5"),
              "the branching constraint '1.5' is neither true, false, default nor an integer constant");
    EXPECT_TRUE(expectation("maybe").starts_with("<constraint>:1:1: error:"));
}

} // namespace
} // namespace nitwit::validate::test
