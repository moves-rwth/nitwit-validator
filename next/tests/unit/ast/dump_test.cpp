#include "ast/dump.hpp"

#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "ast/casting.hpp"
#include "ast/context.hpp"

namespace nitwit::ast {
namespace {

class DumpTest : public ::testing::Test {
protected:
    QualType type(BuiltinKind kind) const { return context_.builtin(kind); }

    static Qualifiers qualifiers(bool isConst, bool isVolatile = false, bool isRestrict = false) {
        Qualifiers result;
        result.isConst = isConst;
        result.isVolatile = isVolatile;
        result.isRestrict = isRestrict;
        return result;
    }

    QualType function(QualType result, std::vector<QualType> parameters, bool isVariadic = false) {
        return context_.functionType(result, parameters, isVariadic, true);
    }

    // A location in the file "test.c" given by `text`.
    SourceLocation at(std::uint32_t line, std::uint32_t column) const {
        return *sources_.location(file_, {line, column});
    }

    IntegerLiteralExpr* integer(std::uint64_t value, SourceLocation location = {}) {
        return context_.create<IntegerLiteralExpr>(
            SourceRange{location, location},
            lex::IntegerLiteral{.value = value, .isDecimal = true, .hasUnsignedSuffix = false, .longCount = 0});
    }

    SourceManager sources_;
    FileId file_ = sources_.addFile("test.c", "\n\nint main(void) {\n    return 0;\n}\n");
    StringInterner interner_;
    TargetInfo target_ = TargetInfo::forDataModel(DataModel::LP64);
    AstContext context_{target_, interner_};
};

TEST_F(DumpTest, BuiltinTypes) {
    EXPECT_EQ(typeToString(type(BuiltinKind::UnsignedLongLong)), "unsigned long long");
    EXPECT_EQ(typeToString(type(BuiltinKind::SignedChar)), "signed char");
    EXPECT_EQ(typeToString(type(BuiltinKind::LongDouble)), "long double");
    EXPECT_EQ(typeToString(type(BuiltinKind::VaList)), "__builtin_va_list");
    EXPECT_EQ(typeToString(type(BuiltinKind::Int).withQualifiers(qualifiers(true, true))), "const volatile int");
}

TEST_F(DumpTest, PointerTypes) {
    EXPECT_EQ(typeToString(context_.pointerTo(type(BuiltinKind::Char).withQualifiers(qualifiers(true)))),
              "const char *");
    EXPECT_EQ(typeToString(context_.pointerTo(type(BuiltinKind::Char)).withQualifiers(qualifiers(true))),
              "char *const");
    EXPECT_EQ(typeToString(context_.pointerTo(
                  context_.pointerTo(type(BuiltinKind::Int)).withQualifiers(qualifiers(false, false, true)))),
              "int *restrict *");
}

TEST_F(DumpTest, ArrayTypes) {
    const QualType array = context_.constantArrayOf(type(BuiltinKind::Int), 3);
    EXPECT_EQ(typeToString(array), "int [3]");
    EXPECT_EQ(typeToString(context_.constantArrayOf(array, 2)), "int [2][3]");
    EXPECT_EQ(typeToString(context_.pointerTo(array)), "int (*)[3]");
    EXPECT_EQ(typeToString(context_.constantArrayOf(context_.pointerTo(type(BuiltinKind::Int)), 3)), "int *[3]");
    EXPECT_EQ(typeToString(context_.arrayOf(type(BuiltinKind::Char), nullptr)), "char []");
    EXPECT_EQ(typeToString(context_.arrayOf(type(BuiltinKind::Char), integer(7))), "char [7]");
}

TEST_F(DumpTest, FunctionTypes) {
    const QualType intType = type(BuiltinKind::Int);
    EXPECT_EQ(typeToString(function(intType, {})), "int (void)");
    EXPECT_EQ(typeToString(context_.functionType(intType, {}, false, false)), "int ()");
    EXPECT_EQ(typeToString(function(intType, {intType}, true)), "int (int, ...)");
    EXPECT_EQ(typeToString(context_.pointerTo(function(intType, {intType}, true))), "int (*)(int, ...)");
    const QualType returnsPointer = function(context_.pointerTo(function(type(BuiltinKind::Void), {})), {intType});
    EXPECT_EQ(typeToString(returnsPointer), "void (*(int))(void)");
}

TEST_F(DumpTest, NamedTypes) {
    auto* record = context_.create<RecordDecl>(interner_.intern("S"), SourceLocation{}, false);
    auto* anonymous = context_.create<RecordDecl>(Symbol(), SourceLocation{}, true);
    auto* enumDecl = context_.create<EnumDecl>(interner_.intern("E"), SourceLocation{});
    auto* typedefDecl =
        context_.create<TypedefDecl>(interner_.intern("size_t"), SourceLocation{}, type(BuiltinKind::UnsignedLong));
    EXPECT_EQ(typeToString(context_.constantArrayOf(context_.recordType(record), 3)), "struct S [3]");
    EXPECT_EQ(typeToString(context_.recordType(anonymous)), "union <anonymous>");
    EXPECT_EQ(typeToString(context_.enumType(enumDecl)), "enum E");
    EXPECT_EQ(typeToString(context_.typedefType(typedefDecl)), "size_t");
    EXPECT_EQ(typeToString(context_.complexOf(type(BuiltinKind::Double))), "_Complex double");
}

TEST_F(DumpTest, TypeofTypes) {
    auto* var = context_.create<VarDecl>(interner_.intern("x"), SourceLocation{}, type(BuiltinKind::Int),
                                         StorageClass::None, true, false);
    Expr* reference = context_.create<DeclRefExpr>(SourceRange{}, var->name(), var);
    EXPECT_EQ(typeToString(context_.typeofExpression(reference, false)), "typeof(x)");
    EXPECT_EQ(typeToString(context_.typeofExpression(reference, true)), "__auto_type");
}

TEST_F(DumpTest, NodesWithTypesAsInTheHeaderExample) {
    IntegerLiteralExpr* zero = integer(0, at(4, 12));
    zero->setType(type(BuiltinKind::Int), ValueCategory::RValue);
    Stmt* const returnStmt[] = {context_.create<ReturnStmt>(SourceRange{at(4, 5), at(4, 14)}, zero)};
    auto* body = context_.create<CompoundStmt>(SourceRange{at(3, 16), at(5, 2)}, context_.copy<Stmt*>(returnStmt));
    auto* main = context_.create<FunctionDecl>(interner_.intern("main"), at(3, 5), function(type(BuiltinKind::Int), {}),
                                               StorageClass::None);
    main->setBody(body);
    EXPECT_EQ(dump(main, sources_), "FunctionDecl main 'int (void)' <3:5>\n"
                                    "  CompoundStmt <3:16>\n"
                                    "    ReturnStmt <4:5>\n"
                                    "      IntegerLiteral 0 'int' <4:12>\n");
}

TEST_F(DumpTest, LValuesAndImplicitCasts) {
    auto* var = context_.create<VarDecl>(interner_.intern("x"), at(3, 5), type(BuiltinKind::Int), StorageClass::None,
                                         true, false);
    auto* reference = context_.create<DeclRefExpr>(SourceRange{at(4, 5), at(4, 6)}, var->name(), var);
    reference->setType(type(BuiltinKind::Int), ValueCategory::LValue);
    auto* load = context_.create<ImplicitCastExpr>(CastKind::LValueToRValue, reference);
    load->setType(type(BuiltinKind::Int), ValueCategory::RValue);
    EXPECT_EQ(dump(load, sources_), "ImplicitCastExpr LValueToRValue 'int' <4:5>\n"
                                    "  DeclRefExpr x (VarDecl at 3:5) 'int' lvalue <4:5>\n");
}

TEST_F(DumpTest, RecordLayoutAfterSema) {
    auto* record = context_.create<RecordDecl>(interner_.intern("S"), at(3, 1), false);
    auto* a = context_.create<FieldDecl>(interner_.intern("a"), at(3, 5), type(BuiltinKind::Char), nullptr);
    auto* b = context_.create<FieldDecl>(interner_.intern("b"), at(3, 9), type(BuiltinKind::Int), integer(3));
    b->bitWidthExpression()->setType(type(BuiltinKind::Int), ValueCategory::RValue);
    b->setBitWidth(3);
    FieldDecl* const fields[] = {a, b};
    a->setParent(record, 0);
    b->setParent(record, 1);
    record->complete(context_.copy<FieldDecl*>(fields));
    const std::uint64_t offsets[] = {0, 1};
    const std::uint8_t bits[] = {0, 2};
    const RecordLayout layout{.size = 4,
                              .alignment = 4,
                              .fieldOffsets = context_.copy<std::uint64_t>(offsets),
                              .bitOffsets = context_.copy<std::uint8_t>(bits)};
    record->setLayout(&layout);
    record->setMaxFieldAlignment(2);
    EXPECT_EQ(dump(record, sources_), "RecordDecl struct S max_field_align 2 size 4 align 4 <3:1>\n"
                                      "  FieldDecl a 'char' offset 0 <3:5>\n"
                                      "  FieldDecl b 'int' width 3 offset 1 bit 2 <3:9>\n"
                                      "    IntegerLiteral 3 'int' <invalid>\n");
}

TEST_F(DumpTest, IncompleteRecord) {
    auto* record = context_.create<RecordDecl>(interner_.intern("S"), at(3, 1), true);
    EXPECT_EQ(dump(record, sources_), "RecordDecl union S incomplete <3:1>\n");
}

TEST_F(DumpTest, EnumerationValuesAfterSema) {
    auto* enumDecl = context_.create<EnumDecl>(interner_.intern("E"), at(3, 1));
    auto* constant = context_.create<EnumConstantDecl>(interner_.intern("A"), at(3, 10), nullptr);
    constant->setType(type(BuiltinKind::Int));
    constant->setValue(-5);
    EnumConstantDecl* const constants[] = {constant};
    enumDecl->complete(context_.copy<EnumConstantDecl*>(constants));
    enumDecl->setIntegerType(type(BuiltinKind::Int));
    EXPECT_EQ(dump(enumDecl, sources_), "EnumDecl enum E 'int' <3:1>\n"
                                        "  EnumConstantDecl A 'int' value -5 <3:10>\n");
}

TEST_F(DumpTest, InvalidLocations) {
    EXPECT_EQ(dump(integer(7), sources_), "IntegerLiteral 7 <invalid>\n");
}

} // namespace
} // namespace nitwit::ast
