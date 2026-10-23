#pragma once

// Small programs for the validation tests and scripts of their executions, as the interpreter would report them.

#include <string_view>

#include "validate_test_support.hpp"

namespace nitwit::validate::test {

/// Three inputs, two calls in line 5; reach_error() if a == 5.
inline constexpr std::string_view inputsProgram = R"(extern int __VERIFIER_nondet_int(void);
void reach_error(void);
int main(void) {
    int a = __VERIFIER_nondet_int();
    int b = __VERIFIER_nondet_int(); int c = __VERIFIER_nondet_int();
    if (a == 5) {
        reach_error();
    }
    return 0;
}
)";

/// `isTaken`: the outcome of the if in line 6; null to evaluate `a == 5`.
inline interp::RunResult runInputs(FakeExecution& x, std::optional<bool> isTaken = std::nullopt) {
    x.statement(4, 5);
    x.assign("a", x.nondet(4, 35));
    x.statement(5, 5);
    x.assign("b", x.nondet(5, 35));
    x.statement(5, 38);
    x.assign("c", x.nondet(5, 68));
    x.statement(6, 5);
    if (isTaken ? x.branch(6, 5, *isTaken) : x.branch(6, 5)) {
        x.statement(7, 9);
        x.call(7, 21);
    }
    x.statement(9, 5);
    return returned(0);
}

/// Two ifs in line 6; b is not an input.
inline constexpr std::string_view twoIfsProgram = R"(extern int __VERIFIER_nondet_int(void);
void reach_error(void);
int main(void) {
    int a = __VERIFIER_nondet_int();
    int b = 1;
    if (a > 0) b = 2; if (b > 0) reach_error();
    return 0;
}
)";

inline interp::RunResult runTwoIfs(FakeExecution& x) {
    x.statement(4, 5);
    x.assign("a", x.nondet(4, 35));
    x.statement(5, 5);
    x.assign("b", x.number("b", 1));
    x.statement(6, 5);
    if (x.branch(6, 5)) {
        x.statement(6, 16);
        x.assign("b", x.number("b", 2));
    }
    x.statement(6, 23);
    if (x.branch(6, 23)) {
        x.statement(6, 34);
        x.call(6, 46);
    }
    x.statement(7, 5);
    return returned(0);
}

/// A loop that runs three times.
inline constexpr std::string_view loopProgram = R"(extern int __VERIFIER_nondet_int(void);
void reach_error(void);
int main(void) {
    int i = 0;
    while (i < 3) {
        i = i + 1;
    }
    if (i == 3) reach_error();
    return 0;
}
)";

inline interp::RunResult runLoop(FakeExecution& x) {
    x.statement(4, 5);
    x.assign("i", x.number("i", 0));
    x.statement(5, 5);
    while (x.branch(5, 5)) {
        x.statement(6, 9);
        x.assign("i", x.number("i", x.integer("i") + 1));
    }
    x.statement(8, 5);
    if (x.branch(8, 5)) {
        x.statement(8, 17);
        x.call(8, 29);
    }
    x.statement(9, 5);
    return returned(0);
}

/// A switch on an input: case 1 returns 1, case 2 calls reach_error(), default returns 2.
inline constexpr std::string_view switchProgram = R"(extern int __VERIFIER_nondet_int(void);
void reach_error(void);
int main(void) {
    int a = __VERIFIER_nondet_int();
    switch (a) {
    case 1:
        return 1;
    case 2:
        reach_error();
        break;
    default:
        return 2;
    }
    return 0;
}
)";

inline interp::RunResult runSwitch(const ValidateTest& fixture, FakeExecution& x) {
    x.statement(4, 5);
    x.assign("a", x.nondet(4, 35));
    x.statement(5, 5);
    switch (fixture.lineOf(x.switchOn(5, 5))) {
    case 6:
        x.statement(6, 5);
        x.statement(7, 9);
        return returned(1);
    case 8:
        x.statement(8, 5);
        x.statement(9, 9);
        x.call(9, 21);
        x.statement(10, 9);
        break;
    default:
        x.statement(11, 5);
        x.statement(12, 9);
        return returned(2);
    }
    x.statement(14, 5);
    return returned(0);
}

} // namespace nitwit::validate::test
