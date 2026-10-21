// Values chosen for relations and the conversions they go through (values.hpp; validate/README.md, "Values").

#include "validate/values.hpp"

#include <cmath>
#include <limits>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

namespace nitwit::validate {
namespace {

using value::Comparison;

class ValuesTest : public ::testing::Test {
protected:
    ast::QualType type(ast::BuiltinKind kind) const { return context_.builtin(kind); }

    value::Value number(Int128 n, ast::BuiltinKind kind) { return valueOf(context_, n, type(kind)); }

    /// The value chosen for `x op constant` with both in `kind`, as text; "none" if there is none.
    std::string chosen(Comparison op, Int128 constant, ast::BuiltinKind kind) {
        return chosenFor({{.op = op, .constant = number(constant, kind), .type = type(kind)}}, kind);
    }

    std::string chosenFor(const std::vector<Bound>& bounds, ast::BuiltinKind kind) {
        const std::optional<value::Value> value = choose(context_, bounds, type(kind));
        return value ? toString(context_, *value, type(kind)) : "none";
    }

    TargetInfo target_ = TargetInfo::forDataModel(DataModel::ILP32);
    StringInterner interner_;
    ast::AstContext context_{target_, interner_};
};

TEST_F(ValuesTest, EqualityAndNonStrictRelationsChooseTheConstant) {
    EXPECT_EQ(chosen(Comparison::Equal, -7, ast::BuiltinKind::Int), "-7");
    EXPECT_EQ(chosen(Comparison::LessEqual, 5, ast::BuiltinKind::Int), "5");
    EXPECT_EQ(chosen(Comparison::GreaterEqual, 5, ast::BuiltinKind::Int), "5");
}

TEST_F(ValuesTest, StrictRelationsChooseTheNeighbour) {
    EXPECT_EQ(chosen(Comparison::Less, -3, ast::BuiltinKind::Int), "-4");
    EXPECT_EQ(chosen(Comparison::Greater, 5, ast::BuiltinKind::Int), "6");
    EXPECT_EQ(chosen(Comparison::Greater, -1, ast::BuiltinKind::UnsignedInt), "none"); // above UINT_MAX
}

TEST_F(ValuesTest, NotEqualChoosesZeroOrOne) {
    EXPECT_EQ(chosen(Comparison::NotEqual, 5, ast::BuiltinKind::Int), "0");
    EXPECT_EQ(chosen(Comparison::NotEqual, 0, ast::BuiltinKind::Int), "1");
}

TEST_F(ValuesTest, NoValueBeyondTheRangeOfTheType) {
    EXPECT_EQ(chosen(Comparison::Less, std::numeric_limits<int>::min(), ast::BuiltinKind::Int), "none");
    EXPECT_EQ(chosen(Comparison::Greater, std::numeric_limits<int>::max(), ast::BuiltinKind::Int), "none");
    EXPECT_EQ(chosen(Comparison::Less, 0, ast::BuiltinKind::UnsignedLongLong), "none");
}

TEST_F(ValuesTest, TheValueIsOfTheInputsTypeAndSatisfiesTheComparisonAfterConversion) {
    // unsigned char d; d >= 255U is compared in unsigned int.
    const Bound bound{.op = Comparison::GreaterEqual,
                      .constant = number(255, ast::BuiltinKind::UnsignedInt),
                      .type = type(ast::BuiltinKind::UnsignedInt)};
    EXPECT_EQ(chosenFor({bound}, ast::BuiltinKind::UnsignedChar), "255");
    // char c; c == 300 is compared in int and cannot hold.
    const Bound tooLarge{
        .op = Comparison::Equal, .constant = number(300, ast::BuiltinKind::Int), .type = type(ast::BuiltinKind::Int)};
    EXPECT_EQ(chosenFor({tooLarge}, ast::BuiltinKind::Char), "none");
}

TEST_F(ValuesTest, AValueSatisfiesAllBounds) {
    const ast::QualType intType = type(ast::BuiltinKind::Int);
    const Bound above{.op = Comparison::Greater, .constant = number(5, ast::BuiltinKind::Int), .type = intType};
    const Bound below{.op = Comparison::Less, .constant = number(10, ast::BuiltinKind::Int), .type = intType};
    const Bound notSix{.op = Comparison::NotEqual, .constant = number(6, ast::BuiltinKind::Int), .type = intType};
    EXPECT_EQ(chosenFor({above, below}, ast::BuiltinKind::Int), "6");
    EXPECT_EQ(chosenFor({notSix, above, below}, ast::BuiltinKind::Int), "9");
    const Bound atMost{.op = Comparison::LessEqual, .constant = number(4, ast::BuiltinKind::Int), .type = intType};
    EXPECT_EQ(chosenFor({above, atMost}, ast::BuiltinKind::Int), "none");
}

TEST_F(ValuesTest, FloatingRelationsChooseTheNextRepresentableValue) {
    const ast::QualType doubleType = type(ast::BuiltinKind::Double);
    const Bound below{.op = Comparison::Less, .constant = value::Value::ofDouble(1.0), .type = doubleType};
    const std::optional<value::Value> value = choose(context_, std::vector{below}, doubleType);
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(value->asDouble(), std::nextafter(1.0, 0.0));
    // A float input compared as double: the next double below 1 rounds to 1.0f, so the float neighbour is not found.
    EXPECT_EQ(chosenFor({below}, ast::BuiltinKind::Float), "none");
    const Bound equal{.op = Comparison::Equal, .constant = value::Value::ofDouble(0.5), .type = doubleType};
    EXPECT_EQ(chosenFor({equal}, ast::BuiltinKind::Float), "0.5");
}

TEST_F(ValuesTest, OnlyNotEqualHoldsForNaN) {
    const ast::QualType doubleType = type(ast::BuiltinKind::Double);
    const value::Value nan = value::Value::ofDouble(std::numeric_limits<double>::quiet_NaN());
    EXPECT_EQ(chosenFor({{.op = Comparison::Equal, .constant = nan, .type = doubleType}}, ast::BuiltinKind::Double),
              "none");
    EXPECT_EQ(chosenFor({{.op = Comparison::NotEqual, .constant = nan, .type = doubleType}}, ast::BuiltinKind::Double),
              "0");
}

TEST_F(ValuesTest, IntegerInputsCompareAsFloating) {
    // int x; x > 2.5 is compared in double.
    const Bound bound{
        .op = Comparison::Greater, .constant = value::Value::ofDouble(2.5), .type = type(ast::BuiltinKind::Double)};
    EXPECT_EQ(chosenFor({bound}, ast::BuiltinKind::Int), "none"); // 2.5000000000000004 truncates to 2
    const Bound equal{
        .op = Comparison::Equal, .constant = value::Value::ofDouble(3.0), .type = type(ast::BuiltinKind::Double)};
    EXPECT_EQ(chosenFor({equal}, ast::BuiltinKind::Int), "3");
}

TEST_F(ValuesTest, PointersAreNotChosen) {
    const ast::QualType pointer = context_.pointerTo(type(ast::BuiltinKind::Int));
    const Bound bound{
        .op = Comparison::Equal, .constant = number(0, ast::BuiltinKind::Int), .type = type(ast::BuiltinKind::Int)};
    EXPECT_EQ(choose(context_, std::vector{bound}, pointer), std::nullopt);
}

TEST_F(ValuesTest, ConversionsFollowC) {
    const auto converted = [&](const value::Value& v, ast::BuiltinKind from, ast::BuiltinKind to) {
        const std::optional<value::Value> result = convert(context_, v, type(from), type(to));
        return result ? toString(context_, *result, type(to)) : "none";
    };
    EXPECT_EQ(converted(number(300, ast::BuiltinKind::Int), ast::BuiltinKind::Int, ast::BuiltinKind::UnsignedChar),
              "44");
    EXPECT_EQ(converted(number(-1, ast::BuiltinKind::Int), ast::BuiltinKind::Int, ast::BuiltinKind::UnsignedInt),
              "4294967295");
    EXPECT_EQ(converted(number(5, ast::BuiltinKind::Int), ast::BuiltinKind::Int, ast::BuiltinKind::Bool), "1");
    EXPECT_EQ(converted(value::Value::ofDouble(-2.75), ast::BuiltinKind::Double, ast::BuiltinKind::Int), "-2");
    EXPECT_EQ(converted(value::Value::ofDouble(1e20), ast::BuiltinKind::Double, ast::BuiltinKind::Int), "none");
    EXPECT_EQ(converted(number(7, ast::BuiltinKind::Long), ast::BuiltinKind::Long, ast::BuiltinKind::Float), "7");
}

TEST_F(ValuesTest, NegatedAndMirroredRelations) {
    EXPECT_EQ(negated(Comparison::Less), Comparison::GreaterEqual);
    EXPECT_EQ(negated(Comparison::Equal), Comparison::NotEqual);
    EXPECT_EQ(negated(Comparison::GreaterEqual), Comparison::Less);
    EXPECT_EQ(mirrored(Comparison::Less), Comparison::Greater);
    EXPECT_EQ(mirrored(Comparison::GreaterEqual), Comparison::LessEqual);
    EXPECT_EQ(mirrored(Comparison::NotEqual), Comparison::NotEqual);
}

TEST_F(ValuesTest, ValuesAsText) {
    EXPECT_EQ(toString(context_, number(-5, ast::BuiltinKind::Int), type(ast::BuiltinKind::Int)), "-5");
    EXPECT_EQ(
        toString(context_, number(-1, ast::BuiltinKind::UnsignedLongLong), type(ast::BuiltinKind::UnsignedLongLong)),
        "18446744073709551615");
    EXPECT_EQ(toString(context_, value::Value::ofFloat(0.1F), type(ast::BuiltinKind::Float)), "0.1");
}

} // namespace
} // namespace nitwit::validate
