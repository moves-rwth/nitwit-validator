// The EvaluationContext: constraints evaluated in the current frame while the observer handles an event.

#include <gtest/gtest.h>

#include "interpreter_test_support.hpp"

namespace nitwit::interp::test {
namespace {

using ast::BinaryOperator;
using ast::UnaryOperator;
using ::testing::StartsWith;

/// Builds a constraint for the function whose frame is current.
using Builder = std::function<ast::Expr*(const ast::FunctionDecl* function)>;

class EvaluationContextTest : public InterpreterTest {
protected:
    /// The value of a constraint as text, or "error: <message>".
    std::string evaluate(EvaluationContext& context, const Builder& build, ast::QualType resultType = {}) {
        const ast::Expr* expr = analyzed(build(context.function()), resultType);
        if (expr == nullptr) {
            return "invalid";
        }
        const std::expected<value::Value, std::string> value = context.evaluate(expr);
        return value ? valueText(*value) : "error: " + value.error();
    }

    /// Evaluates a constraint before the statement in line `line`, the first time it is reached.
    std::string evaluateAt(std::string program, std::uint32_t line, const Builder& build) {
        std::string result = "not reached";
        observer_.onStatement = [&](const StatementEvent& event, EvaluationContext& context) {
            if (sources_.lineColumn(event.stmt->location()).line == line && result == "not reached") {
                result = evaluate(context, build);
            }
            return Directive::Continue;
        };
        (void)run(std::move(program));
        return result;
    }
};

constexpr const char* program = R"(int g = 7;
struct S { int a[3]; } s = {{1, 2, 3}};
int f(int n) {
    int local = n * 2;
    return local + g;
}
int main(void) {
    int x = 2147483647, *p = &x;
    int u;
    return f(3) > 0 ? 0 : 1;
})";

TEST_F(EvaluationContextTest, EvaluatesInTheCurrentFrame) {
    // local + n + g in f(3)
    EXPECT_EQ(evaluateAt(program, 5,
                         [&](const ast::FunctionDecl* f) {
                             return binary(BinaryOperator::Add,
                                           binary(BinaryOperator::Add, ref("local", f), ref("n", f)), ref("g", f));
                         }),
              "16");
    // *p == x && s.a[2] == 3 in main
    EXPECT_EQ(
        evaluateAt(program, 10,
                   [&](const ast::FunctionDecl* f) {
                       return binary(
                           BinaryOperator::LogicalAnd,
                           binary(BinaryOperator::Equal, unary(UnaryOperator::Dereference, ref("p", f)), ref("x", f)),
                           binary(BinaryOperator::Equal, subscript(member(ref("s", f), "a"), integer(2)), integer(3)));
                   }),
        "1");
}

TEST_F(EvaluationContextTest, ReportsTheCurrentFunction) {
    std::vector<std::string> functions;
    observer_.onStatement = [&](const StatementEvent&, EvaluationContext& context) {
        functions.emplace_back(context.function()->name().text());
        return Directive::Continue;
    };
    (void)run(program);
    EXPECT_THAT(functions, ::testing::ElementsAre("main", "main", "main", "f", "f"));
}

TEST_F(EvaluationContextTest, UndefinedBehaviourIsAnError) {
    EXPECT_EQ(
        evaluateAt(program, 10,
                   [&](const ast::FunctionDecl* f) { return binary(BinaryOperator::Add, ref("x", f), integer(1)); }),
        "error: signed integer overflow");
    EXPECT_EQ(evaluateAt(program, 10,
                         [&](const ast::FunctionDecl* f) {
                             return binary(BinaryOperator::Div, ref("x", f),
                                           binary(BinaryOperator::Sub, ref("x", f), ref("x", f)));
                         }),
              "error: division by zero");
    EXPECT_THAT(evaluateAt(program, 10,
                           [&](const ast::FunctionDecl* f) {
                               return unary(UnaryOperator::Dereference,
                                            binary(BinaryOperator::Add, ref("p", f), integer(100000)));
                           }),
                StartsWith("error: invalid read of 4 bytes"));
}

TEST_F(EvaluationContextTest, SideEffectsAreErrors) {
    EXPECT_EQ(
        evaluateAt(program, 10,
                   [&](const ast::FunctionDecl* f) { return binary(BinaryOperator::Assign, ref("x", f), integer(1)); }),
        "error: a constraint must not assign");
    EXPECT_EQ(evaluateAt(program, 10,
                         [&](const ast::FunctionDecl* f) { return unary(UnaryOperator::PostIncrement, ref("x", f)); }),
              "error: a constraint must not modify an object");
    EXPECT_EQ(evaluateAt(program, 10, [&](const ast::FunctionDecl* f) { return callOf(ref("f", f), integer(1)); }),
              "error: a constraint must not call a function");
}

TEST_F(EvaluationContextTest, UninitializedVariablesAskTheOracle) {
    oracle_.answers = {12};
    EXPECT_EQ(evaluateAt(program, 10, [&](const ast::FunctionDecl* f) { return ref("u", f); }), "12");
    ASSERT_EQ(oracle_.requests.size(), 1);
    EXPECT_EQ(oracle_.requests[0].kind, InputRequest::Kind::UninitializedRead);
}

TEST_F(EvaluationContextTest, ResultIsTheReturnValueOfTheObservedCall) {
    std::string value;
    observer_.onReturn = [&](const ReturnEvent& event, EvaluationContext& context) {
        value = evaluate(
            context, [&](const ast::FunctionDecl*) { return binary(BinaryOperator::Equal, result(), integer(13)); },
            event.call->type());
        return Directive::Continue;
    };
    (void)run(program);
    EXPECT_EQ(value, "1");
}

TEST_F(EvaluationContextTest, ResultOutsideAReturnIsAnError) {
    std::string value;
    observer_.onStatement = [&](const StatementEvent&, EvaluationContext& context) {
        value = evaluate(
            context, [&](const ast::FunctionDecl*) { return binary(BinaryOperator::Equal, result(), integer(13)); },
            context_->builtin(ast::BuiltinKind::Int));
        return Directive::Continue;
    };
    (void)run(program);
    EXPECT_EQ(value, "error: \\result outside the return of a call");
}

} // namespace
} // namespace nitwit::interp::test
