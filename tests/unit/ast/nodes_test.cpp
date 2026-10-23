// The out-of-line members of the AST nodes.

#include <vector>

#include <gtest/gtest.h>

#include "ast/casting.hpp"
#include "ast/context.hpp"
#include "ast/decl.hpp"
#include "ast/expr.hpp"

namespace nitwit::ast {
namespace {

class NodesTest : public ::testing::Test {
protected:
    Symbol name(std::string_view text) { return interner_.intern(text); }

    FunctionDecl* function(std::string_view functionName) {
        return context_.create<FunctionDecl>(name(functionName), SourceLocation{},
                                             context_.functionType(context_.builtin(BuiltinKind::Int), {}, false, true),
                                             StorageClass::None);
    }

    DeclRefExpr* reference(ValueDecl* decl) { return context_.create<DeclRefExpr>(SourceRange{}, decl->name(), decl); }

    CallExpr* call(Expr* callee) {
        return context_.create<CallExpr>(SourceRange{}, callee, std::span<Expr*>(), SourceLocation{});
    }

    StringInterner interner_;
    TargetInfo target_ = TargetInfo::forDataModel(DataModel::LP64);
    AstContext context_{target_, interner_};
};

TEST_F(NodesTest, UnnamedMemberOfRecordTypeIsAnAnonymousRecord) {
    auto* inner = context_.create<RecordDecl>(Symbol(), SourceLocation{}, true);
    auto* anonymous = context_.create<FieldDecl>(Symbol(), SourceLocation{}, context_.recordType(inner), nullptr);
    auto* named = context_.create<FieldDecl>(name("u"), SourceLocation{}, context_.recordType(inner), nullptr);
    auto* bitField = context_.create<FieldDecl>(Symbol(), SourceLocation{}, context_.builtin(BuiltinKind::Int),
                                                context_.create<ImplicitZeroExpr>(SourceRange{}));
    EXPECT_TRUE(anonymous->isAnonymousRecord());
    EXPECT_FALSE(named->isAnonymousRecord());
    EXPECT_FALSE(bitField->isAnonymousRecord());
    EXPECT_TRUE(bitField->isBitField());
}

TEST_F(NodesTest, CompletingARecordSetsItsFields) {
    auto* record = context_.create<RecordDecl>(name("S"), SourceLocation{}, false);
    EXPECT_FALSE(record->isComplete());
    auto* field = context_.create<FieldDecl>(name("a"), SourceLocation{}, context_.builtin(BuiltinKind::Int), nullptr);
    const std::vector<FieldDecl*> fields = {field};
    record->complete(context_.copy<FieldDecl*>(fields));
    EXPECT_TRUE(record->isComplete());
    ASSERT_EQ(record->fields().size(), 1u);
    EXPECT_EQ(record->fields()[0], field);
}

TEST_F(NodesTest, DirectCalleeOfACallOfAFunctionName) {
    FunctionDecl* f = function("f");
    EXPECT_EQ(call(reference(f))->directCallee(), f);
}

TEST_F(NodesTest, DirectCalleeLooksThroughParenthesesAndDecay) {
    FunctionDecl* f = function("f");
    Expr* paren = context_.create<ParenExpr>(SourceRange{}, reference(f));
    Expr* decay = context_.create<ImplicitCastExpr>(CastKind::FunctionToPointerDecay, paren);
    EXPECT_EQ(call(context_.create<ParenExpr>(SourceRange{}, decay))->directCallee(), f);
}

TEST_F(NodesTest, CallThroughAPointerHasNoDirectCallee) {
    auto* pointer =
        context_.create<VarDecl>(name("p"), SourceLocation{}, context_.pointerTo(context_.builtin(BuiltinKind::Int)),
                                 StorageClass::None, true, false);
    Expr* load = context_.create<ImplicitCastExpr>(CastKind::LValueToRValue, reference(pointer));
    EXPECT_EQ(call(load)->directCallee(), nullptr);
    EXPECT_EQ(call(reference(pointer))->directCallee(), nullptr);
}

TEST_F(NodesTest, IgnoreParensStripsAllParentheses) {
    Expr* inner = context_.create<ImplicitZeroExpr>(SourceRange{});
    Expr* parens = context_.create<ParenExpr>(SourceRange{}, context_.create<ParenExpr>(SourceRange{}, inner));
    EXPECT_EQ(ignoreParens(parens), inner);
    const Expr* constParens = parens;
    EXPECT_EQ(ignoreParens(constParens), inner);
    EXPECT_EQ(ignoreParens(inner), inner);
}

} // namespace
} // namespace nitwit::ast
