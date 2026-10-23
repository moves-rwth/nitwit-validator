// Witness constraints (expression.hpp): expressions analyzed after the translation unit, with `\result`.

#include <gtest/gtest.h>

#include "sema/expression.hpp"
#include "sema_test_support.hpp"

namespace nitwit::sema {
namespace {

class ConstraintTest : public SemaTest {
protected:
    void SetUp() override {
        unit_ = analyze("int x; struct s { int a; } v;");
        ASSERT_NE(unit_, nullptr) << firstError();
        range_ = SourceRange{find<ast::VarDecl>(unit_, "x")->location(), find<ast::VarDecl>(unit_, "x")->location()};
    }

    ast::Expr* reference(std::string_view name) {
        auto* decl = find<ast::VarDecl>(unit_, name);
        return context().create<ast::DeclRefExpr>(range_, decl->name(), decl);
    }

    ast::Expr* integer(std::uint64_t value) {
        return context().create<ast::IntegerLiteralExpr>(
            range_, lex::IntegerLiteral{.value = value, .isDecimal = true, .hasUnsignedSuffix = false, .longCount = 0});
    }

    ast::Expr* equal(ast::Expr* lhs, ast::Expr* rhs) {
        return context().create<ast::BinaryExpr>(range_, ast::BinaryOperator::Equal, lhs, rhs, range_.begin);
    }

    ast::Expr* result() { return context().create<ast::ResultExpr>(range_); }

    std::string dumpOf(const ast::Expr* expr) { return withoutPositions(ast::dump(expr, sources_)); }

    ast::TranslationUnit* unit_ = nullptr;
    SourceRange range_;
};

TEST_F(ConstraintTest, ConstraintsAreScalarRvalues) {
    const ast::Expr* constraint = analyzeConstraint(reference("x"), ast::QualType(), context(), diagnostics_);
    ASSERT_NE(constraint, nullptr) << firstError();
    EXPECT_EQ(dumpOf(constraint), "ImplicitCastExpr LValueToRValue 'int'\n"
                                  "  DeclRefExpr x (VarDecl at 1:5) 'int' lvalue\n");
}

TEST_F(ConstraintTest, ResultHasTheReturnType) {
    const ast::Expr* constraint = analyzeConstraint(equal(result(), integer(5)),
                                                    context().builtin(ast::BuiltinKind::Long), context(), diagnostics_);
    ASSERT_NE(constraint, nullptr) << firstError();
    EXPECT_EQ(dumpOf(constraint), "BinaryExpr == 'int'\n"
                                  "  ResultExpr 'long'\n"
                                  "  ImplicitCastExpr IntegralCast 'long'\n"
                                  "    IntegerLiteral 5 'int'\n");
}

TEST_F(ConstraintTest, ResultIsOnlyAllowedAtTheReturnOfACall) {
    EXPECT_EQ(analyzeConstraint(equal(result(), integer(5)), ast::QualType(), context(), diagnostics_), nullptr);
    EXPECT_EQ(firstError(), "1:5: '\\result' is only allowed in constraints at the return of a call");
}

TEST_F(ConstraintTest, NonScalarConstraintsAreErrors) {
    EXPECT_EQ(analyzeConstraint(reference("v"), ast::QualType(), context(), diagnostics_), nullptr);
    EXPECT_EQ(firstError(), "1:5: used struct or union type value where scalar is required in a constraint");
}

} // namespace
} // namespace nitwit::sema
