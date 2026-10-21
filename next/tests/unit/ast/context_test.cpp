#include "ast/context.hpp"

#include <vector>

#include <gtest/gtest.h>

#include "ast/casting.hpp"
#include "ast/decl.hpp"
#include "ast/dump.hpp"
#include "ast/expr.hpp"

namespace nitwit::ast {
namespace {

class AstContextTest : public ::testing::Test {
protected:
    QualType type(BuiltinKind kind) const { return context_.builtin(kind); }

    static Qualifiers constQualifier() {
        Qualifiers qualifiers;
        qualifiers.isConst = true;
        return qualifiers;
    }

    Expr* integer(std::uint64_t value) {
        return context_.create<IntegerLiteralExpr>(
            SourceRange{},
            lex::IntegerLiteral{.value = value, .isDecimal = true, .hasUnsignedSuffix = false, .longCount = 0});
    }

    TypedefDecl* typedefOf(QualType underlying) {
        return context_.create<TypedefDecl>(interner_.intern("T"), SourceLocation{}, underlying);
    }

    StringInterner interner_;
    TargetInfo target_ = TargetInfo::forDataModel(DataModel::ILP32);
    AstContext context_{target_, interner_};
};

TEST_F(AstContextTest, KeepsTargetAndInterner) {
    EXPECT_EQ(context_.target().dataModel(), DataModel::ILP32);
    EXPECT_EQ(&context_.interner(), &interner_);
}

TEST_F(AstContextTest, BuiltinTypesAreSingletons) {
    EXPECT_EQ(type(BuiltinKind::Int), type(BuiltinKind::Int));
    EXPECT_NE(type(BuiltinKind::Int), type(BuiltinKind::UnsignedInt));
    EXPECT_EQ(cast<BuiltinType>(type(BuiltinKind::VaList).type())->builtinKind(), BuiltinKind::VaList);
    EXPECT_TRUE(type(BuiltinKind::Char).qualifiers().empty());
}

TEST_F(AstContextTest, PointerTypesAreUniquedByPointee) {
    const QualType intPointer = context_.pointerTo(type(BuiltinKind::Int));
    EXPECT_EQ(intPointer, context_.pointerTo(type(BuiltinKind::Int)));
    EXPECT_NE(intPointer, context_.pointerTo(type(BuiltinKind::Int).withQualifiers(constQualifier())));
    EXPECT_EQ(cast<PointerType>(intPointer.type())->pointee(), type(BuiltinKind::Int));
}

TEST_F(AstContextTest, ConstantArraysAreUniquedBySize) {
    const QualType array = context_.constantArrayOf(type(BuiltinKind::Char), 4);
    EXPECT_EQ(array, context_.constantArrayOf(type(BuiltinKind::Char), 4));
    EXPECT_NE(array, context_.constantArrayOf(type(BuiltinKind::Char), 5));
    const auto* arrayType = cast<ArrayType>(array.type());
    EXPECT_EQ(arrayType->sizeKind(), ArrayType::SizeKind::Constant);
    EXPECT_EQ(arrayType->size(), 4u);
}

TEST_F(AstContextTest, ArraysAsWrittenAreNew) {
    Expr* size = integer(3);
    const QualType first = context_.arrayOf(type(BuiltinKind::Int), size);
    EXPECT_NE(first, context_.arrayOf(type(BuiltinKind::Int), size));
    const auto* array = cast<ArrayType>(first.type());
    EXPECT_EQ(array->sizeKind(), ArrayType::SizeKind::Unresolved);
    EXPECT_EQ(array->sizeExpression(), size);
}

TEST_F(AstContextTest, IncompleteArraysAreUniqued) {
    const QualType array = context_.arrayOf(type(BuiltinKind::Int), nullptr);
    EXPECT_EQ(array, context_.arrayOf(type(BuiltinKind::Int), nullptr));
    EXPECT_EQ(cast<ArrayType>(array.type())->sizeKind(), ArrayType::SizeKind::Incomplete);
}

TEST_F(AstContextTest, SemaResolvesArraySizes) {
    const auto* constant = cast<ArrayType>(context_.arrayOf(type(BuiltinKind::Int), integer(3)).type());
    constant->resolveConstantSize(3);
    EXPECT_EQ(constant->sizeKind(), ArrayType::SizeKind::Constant);
    EXPECT_EQ(constant->size(), 3u);
    const auto* variable = cast<ArrayType>(context_.arrayOf(type(BuiltinKind::Int), integer(3)).type());
    variable->resolveVariableSize();
    EXPECT_EQ(variable->sizeKind(), ArrayType::SizeKind::Variable);
}

TEST_F(AstContextTest, FunctionTypesAreUniquedBySignature) {
    const std::vector<QualType> parameters = {type(BuiltinKind::Int), context_.pointerTo(type(BuiltinKind::Char))};
    const QualType function = context_.functionType(type(BuiltinKind::Void), parameters, false, true);
    EXPECT_EQ(function, context_.functionType(type(BuiltinKind::Void), parameters, false, true));
    EXPECT_NE(function, context_.functionType(type(BuiltinKind::Void), parameters, true, true));
    EXPECT_NE(context_.functionType(type(BuiltinKind::Int), {}, false, true),
              context_.functionType(type(BuiltinKind::Int), {}, false, false));
    const auto* functionType = cast<FunctionType>(function.type());
    ASSERT_EQ(functionType->parameters().size(), 2u);
    EXPECT_EQ(functionType->parameters()[1], parameters[1]);
}

TEST_F(AstContextTest, DeclarationTypesAreUniquedByDeclaration) {
    auto* record = context_.create<RecordDecl>(interner_.intern("S"), SourceLocation{}, false);
    auto* other = context_.create<RecordDecl>(interner_.intern("S"), SourceLocation{}, false);
    EXPECT_EQ(context_.recordType(record), context_.recordType(record));
    EXPECT_NE(context_.recordType(record), context_.recordType(other));
    auto* enumDecl = context_.create<EnumDecl>(interner_.intern("E"), SourceLocation{});
    EXPECT_EQ(cast<EnumType>(context_.enumType(enumDecl).type())->decl(), enumDecl);
    TypedefDecl* typedefDecl = typedefOf(type(BuiltinKind::Int));
    EXPECT_EQ(context_.typedefType(typedefDecl), context_.typedefType(typedefDecl));
}

TEST_F(AstContextTest, ComplexTypesAreUniqued) {
    EXPECT_EQ(context_.complexOf(type(BuiltinKind::Double)), context_.complexOf(type(BuiltinKind::Double)));
    EXPECT_NE(context_.complexOf(type(BuiltinKind::Double)), context_.complexOf(type(BuiltinKind::Float)));
}

TEST_F(AstContextTest, TypeofTypesAreNewUntilResolved) {
    Expr* expression = integer(1);
    const QualType typeOf = context_.typeofExpression(expression, false);
    EXPECT_NE(typeOf, context_.typeofExpression(expression, false));
    const auto* typeofType = cast<TypeofType>(typeOf.type());
    EXPECT_TRUE(typeofType->resolved().isNull());
    typeofType->resolve(type(BuiltinKind::Int));
    EXPECT_EQ(typeofType->resolved(), type(BuiltinKind::Int));
}

TEST_F(AstContextTest, CanonicalTypeStripsTypedefsAndMergesQualifiers) {
    const QualType named = context_.typedefType(typedefOf(context_.pointerTo(type(BuiltinKind::Int))));
    const QualType canonical = context_.canonical(named.withQualifiers(constQualifier()));
    EXPECT_EQ(canonical, context_.pointerTo(type(BuiltinKind::Int)).withQualifiers(constQualifier()));
}

TEST_F(AstContextTest, CanonicalTypeLooksThroughNestedSugar) {
    const QualType named = context_.typedefType(typedefOf(type(BuiltinKind::Long)));
    const QualType pointer = context_.pointerTo(context_.pointerTo(named));
    EXPECT_EQ(context_.canonical(pointer), context_.pointerTo(context_.pointerTo(type(BuiltinKind::Long))));
}

TEST_F(AstContextTest, CanonicalTypeOfAResolvedArrayIsTheConstantArray) {
    const QualType array = context_.arrayOf(context_.typedefType(typedefOf(type(BuiltinKind::Int))), integer(2));
    cast<ArrayType>(array.type())->resolveConstantSize(2);
    EXPECT_EQ(context_.canonical(array), context_.constantArrayOf(type(BuiltinKind::Int), 2));
}

TEST_F(AstContextTest, QualifiersOfAnArrayMoveToItsElements) {
    const QualType array = context_.constantArrayOf(type(BuiltinKind::Int), 2);
    const QualType canonical =
        context_.canonical(context_.typedefType(typedefOf(array)).withQualifiers(constQualifier()));
    EXPECT_EQ(canonical, context_.constantArrayOf(type(BuiltinKind::Int).withQualifiers(constQualifier()), 2));
    EXPECT_TRUE(canonical.qualifiers().empty());
}

TEST_F(AstContextTest, CanonicalIncompleteArrayIsUniqued) {
    const QualType named = context_.typedefType(typedefOf(type(BuiltinKind::Char)));
    EXPECT_EQ(context_.canonical(context_.arrayOf(named, nullptr)), context_.arrayOf(type(BuiltinKind::Char), nullptr));
}

TEST_F(AstContextTest, VariableLengthArrayIsItsOwnCanonicalType) {
    const QualType array = context_.arrayOf(type(BuiltinKind::Int), integer(2));
    cast<ArrayType>(array.type())->resolveVariableSize();
    EXPECT_EQ(context_.canonical(array), array);
}

TEST_F(AstContextTest, CanonicalFunctionTypeIgnoresParameterAndReturnQualifiers) {
    const std::vector<QualType> constParameter = {type(BuiltinKind::Int).withQualifiers(constQualifier())};
    const std::vector<QualType> plainParameter = {type(BuiltinKind::Int)};
    const QualType withQualifiers =
        context_.functionType(type(BuiltinKind::Int).withQualifiers(constQualifier()), constParameter, false, true);
    EXPECT_EQ(context_.canonical(withQualifiers),
              context_.functionType(type(BuiltinKind::Int), plainParameter, false, true));
}

TEST_F(AstContextTest, CanonicalTypeOfTypeofIsItsResolvedType) {
    const QualType typeOf = context_.typeofExpression(integer(1), false);
    cast<TypeofType>(typeOf.type())->resolve(context_.typedefType(typedefOf(type(BuiltinKind::Short))));
    EXPECT_EQ(context_.canonical(typeOf.withQualifiers(constQualifier())),
              type(BuiltinKind::Short).withQualifiers(constQualifier()));
}

TEST_F(AstContextTest, CanonicalComplexType) {
    const QualType named = context_.typedefType(typedefOf(type(BuiltinKind::Float)));
    EXPECT_EQ(context_.canonical(context_.complexOf(named)), context_.complexOf(type(BuiltinKind::Float)));
}

TEST_F(AstContextTest, CopyPutsElementsInTheArena) {
    const std::vector<int> values = {1, 2, 3};
    const std::span<int> copy = context_.copy<int>(values);
    ASSERT_EQ(copy.size(), 3u);
    EXPECT_NE(copy.data(), values.data());
    EXPECT_EQ(copy[2], 3);
    EXPECT_TRUE(context_.copy<int>(std::vector<int>{}).empty());
}

} // namespace
} // namespace nitwit::ast
