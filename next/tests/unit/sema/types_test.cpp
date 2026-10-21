// The type queries of types.hpp: classification, formats, sizes and alignments in both data models.

#include <gtest/gtest.h>

#include "sema_test_support.hpp"

namespace nitwit::sema {
namespace {

class TypesTest : public SemaTest {
protected:
    // The type of the variable `x` that `declarations` declare.
    ast::QualType typeOfX(std::string declarations, DataModel model = DataModel::LP64) {
        const ast::TranslationUnit* unit = analyze(std::move(declarations), model);
        EXPECT_NE(unit, nullptr) << firstError();
        return unit != nullptr ? find<ast::VarDecl>(unit, "x")->type() : ast::QualType();
    }

    // `query(context, type)` for the type of `x` in `declarations`. Each analysis replaces the AstContext, so the
    // context must be taken after it.
    template <typename Query>
    auto ofX(Query query, std::string declarations, DataModel model = DataModel::LP64) {
        const ast::QualType type = typeOfX(std::move(declarations), model);
        return query(context(), type);
    }

    // "size/_Alignof/__alignof__" of the type T that `typedefDeclarator` declares, e.g. "double T[3]", in a data
    // model; __alignof__ comes from sema's constant evaluation.
    std::string sizeAndAlignment(std::string_view typedefDeclarator, DataModel model) {
        const std::string declarations = std::format("typedef {};", typedefDeclarator);
        const ast::QualType x = typeOfX(declarations + " T x;", model);
        if (x.isNull()) {
            return "";
        }
        const std::uint64_t size = sizeOf(context(), x);
        const std::uint64_t align = alignOf(context(), x);
        const Int128 preferred = constantOf(declarations, "__alignof__(T)", model);
        return std::format("{}/{}/{}", size, align, static_cast<long long>(preferred));
    }
};

TEST_F(TypesTest, ScalarSizesAndAlignmentsFollowTheDataModel) {
    struct Case {
        std::string_view typedefDeclarator;
        std::string_view ilp32;
        std::string_view lp64;
    };
    const Case cases[] = {
        {"char T", "1/1/1", "1/1/1"},
        {"_Bool T", "1/1/1", "1/1/1"},
        {"short T", "2/2/2", "2/2/2"},
        {"int T", "4/4/4", "4/4/4"},
        {"long T", "4/4/4", "8/8/8"},
        {"long long T", "8/4/8", "8/8/8"},
        {"float T", "4/4/4", "4/4/4"},
        {"double T", "8/4/8", "8/8/8"},
        {"long double T", "12/4/4", "16/16/16"},
        {"void *T", "4/4/4", "8/8/8"},
        {"_Complex float T", "8/4/4", "8/4/4"},
        {"_Complex double T", "16/4/8", "16/8/8"},
        {"_Complex long double T", "24/4/4", "32/16/16"},
        {"__builtin_va_list T", "4/4/4", "24/8/8"},
        {"double T[3]", "24/4/8", "24/8/8"},
    };
    for (const Case& c : cases) {
        EXPECT_EQ(sizeAndAlignment(c.typedefDeclarator, DataModel::ILP32), c.ilp32) << c.typedefDeclarator;
        EXPECT_EQ(sizeAndAlignment(c.typedefDeclarator, DataModel::LP64), c.lp64) << c.typedefDeclarator;
    }
}

TEST_F(TypesTest, Int128IsOnlyAvailableOnX86_64) {
    EXPECT_EQ(sizeAndAlignment("__int128 T", DataModel::LP64), "16/16/16");
    EXPECT_EQ(errorOf("__int128 x;", DataModel::ILP32), "1:10: '__int128' is not supported on this target");
}

TEST_F(TypesTest, IntegerFormats) {
    EXPECT_EQ(ofX(integerFormat, "long x;", DataModel::ILP32), (value::IntegerFormat{32, true}));
    EXPECT_EQ(ofX(integerFormat, "long x;"), (value::IntegerFormat{64, true}));
    EXPECT_EQ(ofX(integerFormat, "char x;"), (value::IntegerFormat{8, true}));
    EXPECT_EQ(ofX(integerFormat, "_Bool x;"), (value::IntegerFormat{8, false}));
    EXPECT_EQ(ofX(integerFormat, "int *x;", DataModel::ILP32), (value::IntegerFormat{32, false}));
    EXPECT_EQ(ofX(integerFormat, "enum e { A = -1 } x;"), (value::IntegerFormat{32, true}));
    EXPECT_EQ(ofX(integerFormat, "enum e { A = 1 } x;"), (value::IntegerFormat{32, false}));
    EXPECT_EQ(ofX(integerFormat, "typedef unsigned short u16; const u16 x;"), (value::IntegerFormat{16, false}));
}

TEST_F(TypesTest, FloatFormats) {
    EXPECT_EQ(ofX(floatFormat, "float x;"), value::FloatFormat::Float);
    EXPECT_EQ(ofX(floatFormat, "double x;"), value::FloatFormat::Double);
    EXPECT_EQ(ofX(floatFormat, "long double x;"), value::FloatFormat::LongDouble);
}

TEST_F(TypesTest, Classification) {
    const ast::QualType integer = typeOfX("typedef int T; T x;");
    EXPECT_TRUE(isInteger(context(), integer));
    EXPECT_TRUE(isSignedInteger(context(), integer));
    EXPECT_TRUE(isArithmetic(context(), integer));
    EXPECT_TRUE(isScalar(context(), integer));
    EXPECT_FALSE(isPointer(context(), integer));

    const ast::QualType pointer = typeOfX("char *x;");
    EXPECT_TRUE(isPointer(context(), pointer));
    EXPECT_TRUE(isScalar(context(), pointer));
    EXPECT_FALSE(isArithmetic(context(), pointer));
    EXPECT_EQ(ast::typeToString(pointee(context(), pointer)), "char");

    EXPECT_TRUE(ofX(isInteger, "enum { A } x;"));
    EXPECT_TRUE(ofX(isInteger, "_Bool x;"));
    EXPECT_FALSE(ofX(isSignedInteger, "unsigned x;"));
    EXPECT_TRUE(ofX(isFloating, "double x;"));
    EXPECT_TRUE(ofX(isRecord, "struct s { int a; } x;"));
    EXPECT_TRUE(ofX(isRecord, "union u { int a; } x;"));
    EXPECT_FALSE(ofX(isScalar, "int x[2];"));
    // Complex types are typed, but NITWIT does not compute with them.
    EXPECT_FALSE(ofX(isArithmetic, "_Complex double x;"));
    EXPECT_FALSE(ofX(isScalar, "__builtin_va_list x;"));
}

TEST_F(TypesTest, VoidAndFunctionTypesHaveSizeOneAsInGnuC) {
    EXPECT_EQ(constantOf("", "sizeof(void)"), 1);
    EXPECT_EQ(constantOf("int f(void);", "sizeof(f)"), 1);
    const ast::QualType voidPointer = typeOfX("void *x;");
    EXPECT_TRUE(isVoid(context(), pointee(context(), voidPointer)));
    EXPECT_EQ(sizeOf(context(), pointee(context(), voidPointer)), 1u);
}

TEST_F(TypesTest, TypedefAlignmentChangesTheAlignmentButNotTheSize) {
    const ast::QualType increased =
        typeOfX("typedef long long aligned_ll __attribute__((aligned(8))); aligned_ll x;", DataModel::ILP32);
    EXPECT_EQ(sizeOf(context(), increased), 8u);
    EXPECT_EQ(alignOf(context(), increased), 8u);
    const ast::QualType decreased = typeOfX("typedef double d2 __attribute__((aligned(2))); d2 x;");
    EXPECT_EQ(sizeOf(context(), decreased), 8u);
    EXPECT_EQ(alignOf(context(), decreased), 2u);
    const ast::QualType array = typeOfX("typedef int i16 __attribute__((aligned(16))); i16 x[2];");
    EXPECT_EQ(alignOf(context(), array), 16u);
}

TEST_F(TypesTest, RecordSizesComeFromTheirLayout) {
    const ast::QualType type = typeOfX("struct s { char c; double d; } x;", DataModel::ILP32);
    EXPECT_EQ(sizeOf(context(), type), 12u);
    EXPECT_EQ(alignOf(context(), type), 4u);
    const ast::RecordDecl* record = ast::cast<ast::RecordType>(context().canonical(type).type())->decl();
    EXPECT_EQ(layoutOf(record).size, 12u);
}

TEST_F(TypesTest, EnumerationsHaveTheSizeOfTheirIntegerType) {
    EXPECT_EQ(ofX(sizeOf, "enum e { A = 0x100000000 } x;", DataModel::ILP32), 8u);
    EXPECT_EQ(ofX(alignOf, "enum e { A = 0x100000000 } x;", DataModel::ILP32), 4u);
    EXPECT_EQ(ofX(sizeOf, "enum __attribute__((packed)) e { A = 200 } x;"), 1u);
}

} // namespace
} // namespace nitwit::sema
