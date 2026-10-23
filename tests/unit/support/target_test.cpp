#include "support/target.hpp"

#include <gtest/gtest.h>

namespace nitwit {
namespace {

// Expected values: GCC 13 with -m32 and -m64 on Linux (sizeof, _Alignof, __alignof__).

TEST(TargetTest, Ilp32Layouts) {
    const TargetInfo target = TargetInfo::forDataModel(DataModel::ILP32);
    EXPECT_EQ(target.pointerBits(), 32u);
    EXPECT_EQ(target.layout(ScalarType::Long).size, 4);
    EXPECT_EQ(target.layout(ScalarType::Pointer).size, 4);
    EXPECT_EQ(target.layout(ScalarType::LongLong).align, 4);
    EXPECT_EQ(target.layout(ScalarType::LongLong).preferredAlign, 8);
    EXPECT_EQ(target.layout(ScalarType::Double).align, 4);
    EXPECT_EQ(target.layout(ScalarType::LongDouble).size, 12);
    EXPECT_EQ(target.layout(ScalarType::LongDouble).align, 4);
    EXPECT_EQ(target.sizeType(), ScalarType::UnsignedInt);
    EXPECT_EQ(target.ptrdiffType(), ScalarType::Int);
    EXPECT_FALSE(target.hasInt128());
}

TEST(TargetTest, Lp64Layouts) {
    const TargetInfo target = TargetInfo::forDataModel(DataModel::LP64);
    EXPECT_EQ(target.pointerBits(), 64u);
    EXPECT_EQ(target.layout(ScalarType::Long).size, 8);
    EXPECT_EQ(target.layout(ScalarType::Pointer).align, 8);
    EXPECT_EQ(target.layout(ScalarType::Double).align, 8);
    EXPECT_EQ(target.layout(ScalarType::LongDouble).size, 16);
    EXPECT_EQ(target.layout(ScalarType::LongDouble).align, 16);
    EXPECT_EQ(target.layout(ScalarType::Int128).size, 16);
    EXPECT_EQ(target.sizeType(), ScalarType::UnsignedLong);
    EXPECT_TRUE(target.hasInt128());
}

TEST(TargetTest, PlainCharIsSigned) {
    const TargetInfo target = TargetInfo::forDataModel(DataModel::LP64);
    EXPECT_TRUE(target.isSigned(ScalarType::Char));
    EXPECT_FALSE(target.isSigned(ScalarType::UnsignedChar));
    EXPECT_FALSE(target.isSigned(ScalarType::Bool));
    EXPECT_FALSE(target.isSigned(ScalarType::Double));
}

} // namespace
} // namespace nitwit
