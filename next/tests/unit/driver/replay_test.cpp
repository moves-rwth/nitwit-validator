// The replay mode's oracle and observer (tools/difftest/README.md, "Harness").

#include "driver/replay.hpp"

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <unistd.h>

#include <gtest/gtest.h>

#include "ast/context.hpp"
#include "support/interner.hpp"
#include "support/target.hpp"

namespace nitwit::driver {
namespace {

using ast::BuiltinKind;

class ReplayOracleTest : public ::testing::TestWithParam<DataModel> {
protected:
    void SetUp() override {
        target_ = std::make_unique<TargetInfo>(TargetInfo::forDataModel(GetParam()));
        context_ = std::make_unique<ast::AstContext>(*target_, interner_);
        directory_ = std::filesystem::path(::testing::TempDir()) / ("nitwit-replay-" + std::to_string(::getpid()));
        std::filesystem::create_directories(directory_);
        inputs_ = (directory_ / "inputs").string();
        log_ = (directory_ / "log").string();
        std::filesystem::remove(log_);
    }

    void TearDown() override { std::filesystem::remove_all(directory_); }

    void writeInputs(std::string_view text) const { std::ofstream(inputs_, std::ios::binary) << text; }

    [[nodiscard]] std::string log() const {
        std::ifstream file(log_);
        std::stringstream text;
        text << file.rdbuf();
        return text.str();
    }

    /// A nondet call of `name`, returning `type`.
    interp::InputRequest nondet(std::string_view name, ast::QualType type) {
        const ast::QualType functionType = context_->functionType(type, {}, false, true);
        auto* function = context_->create<ast::FunctionDecl>(interner_.intern(name), SourceLocation{}, functionType,
                                                             ast::StorageClass::Extern);
        return {.kind = interp::InputRequest::Kind::NondetCall,
                .id = value::InputId{0},
                .site = nullptr,
                .occurrence = 0,
                .type = type,
                .function = function};
    }

    ast::QualType builtin(BuiltinKind kind) const { return context_->builtin(kind); }

    StringInterner interner_;
    std::unique_ptr<TargetInfo> target_;
    std::unique_ptr<ast::AstContext> context_;
    std::filesystem::path directory_;
    std::string inputs_;
    std::string log_;
};

std::string dataModelName(const ::testing::TestParamInfo<DataModel>& parameter) {
    return parameter.param == DataModel::ILP32 ? "ILP32" : "LP64";
}

INSTANTIATE_TEST_SUITE_P(DataModels, ReplayOracleTest, ::testing::Values(DataModel::ILP32, DataModel::LP64),
                         dataModelName);

TEST_P(ReplayOracleTest, IntegersAreConvertedToTheTypeAndLogged) {
    writeInputs("300\n-1\n2\n200\n");
    ReplayOracle oracle(*context_, inputs_, log_);
    EXPECT_EQ(oracle.provide(nondet("__VERIFIER_nondet_uchar", builtin(BuiltinKind::UnsignedChar))).bits(), 44);
    EXPECT_EQ(oracle.provide(nondet("__VERIFIER_nondet_uint", builtin(BuiltinKind::UnsignedInt))).bits(), 0xFFFFFFFFu);
    EXPECT_EQ(oracle.provide(nondet("__VERIFIER_nondet_bool", builtin(BuiltinKind::Bool))).bits(), 1);
    EXPECT_EQ(oracle.provide(nondet("__VERIFIER_nondet_char", builtin(BuiltinKind::Char))).signedBits(), -56);
    EXPECT_EQ(log(), "__VERIFIER_nondet_uchar 44\n__VERIFIER_nondet_uint 4294967295\n__VERIFIER_nondet_bool 1\n"
                     "__VERIFIER_nondet_char -56\n");
}

TEST_P(ReplayOracleTest, TheHarnessTypeDecidesTheConversionAndTheLog) {
    // The harness's __VERIFIER_nondet_char returns a (signed) char, whatever the program declares.
    writeInputs("200\n200\n");
    ReplayOracle oracle(*context_, inputs_, log_);
    EXPECT_EQ(oracle.provide(nondet("__VERIFIER_nondet_char", builtin(BuiltinKind::UnsignedChar))).bits(), 200);
    EXPECT_EQ(oracle.provide(nondet("__VERIFIER_nondet_char", builtin(BuiltinKind::Int))).signedBits(), -56);
    EXPECT_EQ(log(), "__VERIFIER_nondet_char -56\n__VERIFIER_nondet_char -56\n");
}

TEST_P(ReplayOracleTest, ValuesTheTypeCannotHoldWrap) {
    // Read modulo 2^64 without __int128, else modulo 2^128: 2^64 is 0 or not for _Bool.
    writeInputs("18446744073709551616\n");
    ReplayOracle oracle(*context_, inputs_, log_);
    EXPECT_EQ(oracle.provide(nondet("__VERIFIER_nondet_bool", builtin(BuiltinKind::Bool))).bits(),
              GetParam() == DataModel::LP64 ? 1 : 0);
}

TEST_P(ReplayOracleTest, PointersAreLoggedAsUnsignedAddresses) {
    writeInputs("-1\n");
    ReplayOracle oracle(*context_, inputs_, log_);
    (void)oracle.provide(nondet("__VERIFIER_nondet_pointer", context_->pointerTo(builtin(BuiltinKind::Void))));
    EXPECT_EQ(log(), GetParam() == DataModel::LP64 ? "__VERIFIER_nondet_pointer 18446744073709551615\n"
                                                   : "__VERIFIER_nondet_pointer 4294967295\n");
}

TEST_P(ReplayOracleTest, FloatingLinesAreDecimalHexadecimalOrSpecial) {
    writeInputs("0x1.8p+1\n-inf\n7\n");
    ReplayOracle oracle(*context_, inputs_, log_);
    EXPECT_EQ(oracle.provide(nondet("__VERIFIER_nondet_double", builtin(BuiltinKind::Double))).asDouble(), 3.0);
    (void)oracle.provide(nondet("__VERIFIER_nondet_float", builtin(BuiltinKind::Float)));
    EXPECT_EQ(oracle.provide(nondet("__VERIFIER_nondet_float", builtin(BuiltinKind::Float))).asFloat(), 7.0F);
    EXPECT_EQ(log(), "__VERIFIER_nondet_double 0x1.8p+1\n__VERIFIER_nondet_float -inf\n"
                     "__VERIFIER_nondet_float 0x1.cp+2\n");
}

TEST_P(ReplayOracleTest, ExhaustedInputsGiveZeroMarkedDefault) {
    writeInputs("5\r\n");
    ReplayOracle oracle(*context_, inputs_, log_);
    EXPECT_EQ(oracle.provide(nondet("__VERIFIER_nondet_int", builtin(BuiltinKind::Int))).bits(), 5);
    EXPECT_EQ(oracle.provide(nondet("__VERIFIER_nondet_int", builtin(BuiltinKind::Int))).bits(), 0);
    EXPECT_EQ(log(), "__VERIFIER_nondet_int 5\n__VERIFIER_nondet_int 0 default\n");
}

TEST_P(ReplayOracleTest, OtherRequestsGetZeroWithoutInputLines) {
    writeInputs("5\n");
    ReplayOracle oracle(*context_, inputs_, log_);
    interp::InputRequest uninitialized = nondet("f", builtin(BuiltinKind::Long));
    uninitialized.kind = interp::InputRequest::Kind::UninitializedRead;
    EXPECT_EQ(oracle.provide(uninitialized).bits(), 0);
    interp::InputRequest unknown = nondet("g", builtin(BuiltinKind::Double));
    unknown.kind = interp::InputRequest::Kind::UndefinedFunction;
    EXPECT_EQ(oracle.provide(unknown).asDouble(), 0.0);
    EXPECT_EQ(oracle.provide(nondet("__VERIFIER_nondet_int", builtin(BuiltinKind::Int))).bits(), 5);
    EXPECT_EQ(log(), "__VERIFIER_nondet_int 5\n");
}

TEST_P(ReplayOracleTest, MalformedLinesEndTheRunWithCode99) {
    writeInputs("12x\n");
    EXPECT_EXIT(
        {
            ReplayOracle oracle(*context_, inputs_, log_);
            (void)oracle.provide(nondet("__VERIFIER_nondet_int", builtin(BuiltinKind::Int)));
        },
        ::testing::ExitedWithCode(exitBadInput),
        "NITWIT-DIFFTEST: bad input: __VERIFIER_nondet_int, line 1 of NITWIT_INPUTS: expected a decimal integer");
}

TEST_P(ReplayOracleTest, LongLinesEndTheRunWithCode99) {
    writeInputs(std::string(256, '1') + "\n");
    EXPECT_EXIT(
        {
            ReplayOracle oracle(*context_, inputs_, log_);
            (void)oracle.provide(nondet("__VERIFIER_nondet_int", builtin(BuiltinKind::Int)));
        },
        ::testing::ExitedWithCode(exitBadInput), "line too long");
}

TEST_P(ReplayOracleTest, UnreadableInputsEndTheRunWithCode99) {
    EXPECT_EXIT(
        {
            ReplayOracle oracle(*context_, (directory_ / "missing").string(), log_);
            (void)oracle.provide(nondet("__VERIFIER_nondet_int", builtin(BuiltinKind::Int)));
        },
        ::testing::ExitedWithCode(exitBadInput), "cannot open the file");
}

TEST(ReplayObserverTest, StopsAtErrorFunctionsWithoutDefinition) {
    const TargetInfo target = TargetInfo::forDataModel(DataModel::LP64);
    StringInterner interner;
    ast::AstContext context(target, interner);
    const ast::QualType type = context.functionType(context.builtin(BuiltinKind::Void), {}, false, true);
    auto* declared = context.create<ast::FunctionDecl>(interner.intern("reach_error"), SourceLocation{}, type,
                                                       ast::StorageClass::None);
    auto* defined = context.create<ast::FunctionDecl>(interner.intern("reach_error"), SourceLocation{}, type,
                                                      ast::StorageClass::None);
    defined->setBody(context.create<ast::CompoundStmt>(SourceRange{}, std::span<ast::Stmt* const>()));

    ReplayObserver observer;
    struct NoContext final : interp::EvaluationContext {
        std::expected<value::Value, std::string> evaluate(const ast::Expr*) override { return value::Value(); }
        [[nodiscard]] const ast::FunctionDecl* function() const override { return nullptr; }
        [[nodiscard]] const value::TermTable& terms() const override { return table; }
        value::TermTable table{DataModel::ILP32};
    } evaluation;
    EXPECT_EQ(observer.afterArguments({.call = nullptr, .callee = defined, .arguments = {}}, evaluation),
              interp::Directive::Continue);
    EXPECT_FALSE(observer.errorFunction().has_value());
    EXPECT_EQ(observer.afterArguments({.call = nullptr, .callee = declared, .arguments = {}}, evaluation),
              interp::Directive::Stop);
    EXPECT_EQ(observer.errorFunction(), "reach_error");

    // __builtin_trap ends a native run by SIGILL; the observer remembers the call.
    auto* trap = context.create<ast::FunctionDecl>(interner.intern("__builtin_trap"), SourceLocation{}, type,
                                                   ast::StorageClass::Extern);
    EXPECT_FALSE(observer.hasTrapped());
    EXPECT_EQ(observer.afterArguments({.call = nullptr, .callee = trap, .arguments = {}}, evaluation),
              interp::Directive::Continue);
    EXPECT_TRUE(observer.hasTrapped());

    // An inline definition is no external definition (C17 6.7.4p7); natively, the harness's function is called.
    ReplayObserver inlineObserver;
    defined->setInline();
    EXPECT_EQ(inlineObserver.afterArguments({.call = nullptr, .callee = defined, .arguments = {}}, evaluation),
              interp::Directive::Stop);
}

} // namespace
} // namespace nitwit::driver
