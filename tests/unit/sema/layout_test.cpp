// Record layouts as GCC computes them for i386 and x86-64; tools/layout-probe checks the same rules against GCC.

#include <gtest/gtest.h>

#include "sema_test_support.hpp"

namespace nitwit::sema {
namespace {

using LayoutTest = SemaTest;

constexpr DataModel ilp32 = DataModel::ILP32;
constexpr DataModel lp64 = DataModel::LP64;

TEST_F(LayoutTest, MembersAreAlignedToTheirTypes) {
    EXPECT_EQ(layoutText("struct s { char c; int i; short h; };", "s"), "size 12 align 4 c@0 i@4 h@8");
    EXPECT_EQ(layoutText("struct s { char c; long l; };", "s", ilp32), "size 8 align 4 c@0 l@4");
    EXPECT_EQ(layoutText("struct s { char c; long l; };", "s", lp64), "size 16 align 8 c@0 l@8");
}

TEST_F(LayoutTest, EightByteScalarsAreAlignedToFourBytesOnI386) {
    EXPECT_EQ(layoutText("struct s { char c; double d; };", "s", ilp32), "size 12 align 4 c@0 d@4");
    EXPECT_EQ(layoutText("struct s { char c; long long d; };", "s", ilp32), "size 12 align 4 c@0 d@4");
    EXPECT_EQ(layoutText("struct s { char c; double d; };", "s", lp64), "size 16 align 8 c@0 d@8");
}

TEST_F(LayoutTest, LongDoubleTakesTwelveBytesOnI386AndSixteenOnX86_64) {
    EXPECT_EQ(layoutText("struct s { char c; long double d; };", "s", ilp32), "size 16 align 4 c@0 d@4");
    EXPECT_EQ(layoutText("struct s { char c; long double d; };", "s", lp64), "size 32 align 16 c@0 d@16");
}

TEST_F(LayoutTest, UnionMembersStartAtZero) {
    EXPECT_EQ(layoutText("union u { char c[5]; int i; };", "u"), "size 8 align 4 c@0 i@0");
}

TEST_F(LayoutTest, BitFieldsShareStorageUnits) {
    EXPECT_EQ(layoutText("struct s { unsigned a : 3; unsigned b : 5; unsigned c : 7; };", "s"),
              "size 4 align 4 a@0.0 b@0.3 c@1.0");
    EXPECT_EQ(layoutText("struct s { char c; int i : 9; };", "s"), "size 4 align 4 c@0 i@1.0");
}

TEST_F(LayoutTest, BitFieldsMoveOnlyWhenTheyWouldSpanTooManyUnitsOfTheirType) {
    // 25 bits after the first byte would span two int units.
    EXPECT_EQ(layoutText("struct s { char c; int i : 25; };", "s"), "size 8 align 4 c@0 i@4.0");
    EXPECT_EQ(layoutText("struct s { char c; int i : 24; };", "s"), "size 4 align 4 c@0 i@1.0");
    EXPECT_EQ(layoutText("struct s { short a : 9; short b : 9; };", "s"), "size 4 align 2 a@0.0 b@2.0");
}

TEST_F(LayoutTest, LongLongBitFieldsUseFourByteUnitsOnI386) {
    EXPECT_EQ(layoutText("struct s { char c; long long ll : 40; };", "s", ilp32), "size 8 align 4 c@0 ll@1.0");
    EXPECT_EQ(layoutText("struct s { char c; long long ll : 57; };", "s", ilp32), "size 12 align 4 c@0 ll@4.0");
    EXPECT_EQ(layoutText("struct s { char c; long long ll : 57; };", "s", lp64), "size 16 align 8 c@0 ll@8.0");
    EXPECT_EQ(layoutText("struct s { short a : 1; long long b : 63; };", "s", ilp32), "size 8 align 4 a@0.0 b@0.1");
    EXPECT_EQ(layoutText("struct s { short a : 1; long long b : 63; };", "s", lp64), "size 8 align 8 a@0.0 b@0.1");
}

TEST_F(LayoutTest, ZeroWidthBitFieldsAlignTheNextMemberButNotTheRecord) {
    EXPECT_EQ(layoutText("struct s { char a; int : 0; char b; };", "s"), "size 5 align 1 a@0 _@4.0 b@4");
    EXPECT_EQ(layoutText("struct s { char a; long long : 0; char b; };", "s", ilp32), "size 5 align 1 a@0 _@4.0 b@4");
    EXPECT_EQ(layoutText("struct s { char a; long long : 0; char b; };", "s", lp64), "size 9 align 1 a@0 _@8.0 b@8");
    EXPECT_EQ(layoutText("struct s { unsigned a : 1; unsigned : 0; unsigned b : 1; };", "s"),
              "size 8 align 4 a@0.0 _@4.0 b@4.0");
}

TEST_F(LayoutTest, UnnamedBitFieldsDoNotAlignTheRecord) {
    EXPECT_EQ(layoutText("struct s { char a; int : 3; char b; };", "s"), "size 3 align 1 a@0 _@1.0 b@2");
    EXPECT_EQ(layoutText("union u { char c; int : 3; };", "u"), "size 1 align 1 c@0 _@0.0");
}

TEST_F(LayoutTest, NamedBitFieldsAlignTheRecordToTheirType) {
    EXPECT_EQ(layoutText("union u { int a : 3; char c; };", "u"), "size 4 align 4 a@0.0 c@0");
    EXPECT_EQ(layoutText("struct s { _Bool a : 1; _Bool b : 1; char c; };", "s"), "size 2 align 1 a@0.0 b@0.1 c@1");
}

TEST_F(LayoutTest, PackedRecordsHaveNoPadding) {
    EXPECT_EQ(layoutText("struct __attribute__((packed)) s { char c; int i; };", "s"), "size 5 align 1 c@0 i@1");
    EXPECT_EQ(layoutText("struct s { char c; int i; } __attribute__((packed));", "s"), "size 5 align 1 c@0 i@1");
    EXPECT_EQ(layoutText("struct __attribute__((packed, aligned(4))) s { char c; int i; };", "s"),
              "size 8 align 4 c@0 i@1");
}

TEST_F(LayoutTest, PackedMembers) {
    EXPECT_EQ(layoutText("struct s { char c; int i __attribute__((packed)); char d; long long l; };", "s"),
              "size 16 align 8 c@0 i@1 d@5 l@8");
    // An `aligned` attribute on a member of a packed record is kept.
    EXPECT_EQ(layoutText("struct __attribute__((packed)) s { char c; int i __attribute__((aligned(4))); };", "s"),
              "size 8 align 4 c@0 i@4");
}

TEST_F(LayoutTest, PackedBitFieldsStraddleUnits) {
    EXPECT_EQ(layoutText("struct __attribute__((packed)) s { char c : 3; int i : 20; char d; };", "s"),
              "size 4 align 1 c@0.0 i@0.3 d@3");
    EXPECT_EQ(layoutText("struct __attribute__((packed)) s { char c; long long ll : 40; };", "s"),
              "size 6 align 1 c@0 ll@1.0");
}

TEST_F(LayoutTest, AlignedMembersAndRecords) {
    EXPECT_EQ(layoutText("struct s { char c; int i __attribute__((aligned(8))); };", "s"), "size 16 align 8 c@0 i@8");
    // `aligned` cannot lower the alignment of a member.
    EXPECT_EQ(layoutText("struct s { char c; int i __attribute__((aligned(2))); };", "s"), "size 8 align 4 c@0 i@4");
    EXPECT_EQ(layoutText("struct s { char c; double d __attribute__((aligned(8))); };", "s", ilp32),
              "size 16 align 8 c@0 d@8");
    EXPECT_EQ(layoutText("struct __attribute__((aligned(16))) s { char c; };", "s"), "size 16 align 16 c@0");
    EXPECT_EQ(layoutText("struct s { char c; int i __attribute__((aligned)); };", "s"), "size 32 align 16 c@0 i@16");
    EXPECT_EQ(layoutText("struct s { char c; _Alignas(16) char d; };", "s"), "size 32 align 16 c@0 d@16");
}

TEST_F(LayoutTest, TypedefAlignmentAppliesToMembers) {
    EXPECT_EQ(
        layoutText("typedef long long all __attribute__((aligned(8))); struct s { char c; all ll; };", "s", ilp32),
        "size 16 align 8 c@0 ll@8");
    EXPECT_EQ(layoutText("typedef int i2 __attribute__((aligned(2))); struct s { char c; i2 i; };", "s"),
              "size 6 align 2 c@0 i@2");
}

TEST_F(LayoutTest, PragmaPackCapsTheMemberAlignment) {
    EXPECT_EQ(layoutText("#pragma pack(2)\nstruct s { char c; int i; short h; long long l; };", "s"),
              "size 16 align 2 c@0 i@2 h@6 l@8");
    EXPECT_EQ(layoutText("#pragma pack(1)\nstruct s { char c; int i __attribute__((aligned(8))); };", "s"),
              "size 5 align 1 c@0 i@1");
    EXPECT_EQ(layoutText("#pragma pack(4)\nstruct s { char c; double d; };", "s", lp64), "size 12 align 4 c@0 d@4");
}

TEST_F(LayoutTest, PragmaPackLetsBitFieldsStraddle) {
    EXPECT_EQ(layoutText("#pragma pack(2)\nstruct s { char a : 4; int b : 30; };", "s"), "size 6 align 2 a@0.0 b@0.4");
    // A zero-width bit-field is not affected by #pragma pack.
    EXPECT_EQ(layoutText("#pragma pack(1)\nstruct s { char a; int : 0; char b; };", "s"),
              "size 5 align 1 a@0 _@4.0 b@4");
}

TEST_F(LayoutTest, FlexibleAndZeroLengthArrays) {
    EXPECT_EQ(layoutText("struct s { char c; int data[]; };", "s"), "size 4 align 4 c@0 data@4");
    EXPECT_EQ(layoutText("struct s { char c; long long data[0]; char d; };", "s", lp64),
              "size 16 align 8 c@0 data@8 d@8");
    EXPECT_EQ(layoutText("struct s {};", "s"), "size 0 align 1");
}

TEST_F(LayoutTest, AnonymousMembersAreLaidOutAsMembers) {
    EXPECT_EQ(layoutText("struct s { char c; struct { int a; char b; }; union { short h; double d; }; char t; };", "s"),
              "size 32 align 8 c@0 _@4 _@16 t@24");
}

TEST_F(LayoutTest, NestedRecordsUseTheirOwnLayout) {
    EXPECT_EQ(layoutText("struct in { char c; double d; }; struct s { char c; struct in i; char t; };", "s", ilp32),
              "size 20 align 4 c@0 i@4 t@16");
}

TEST_F(LayoutTest, ErrorsInMembers) {
    EXPECT_EQ(errorOf("struct s { struct s inner; };"), "1:21: field 'inner' has incomplete type");
    EXPECT_EQ(errorOf("struct s { int data[]; int n; };"), "1:16: flexible array member not at end of struct");
    EXPECT_EQ(errorOf("struct s { int f(void); };"), "1:16: field 'f' declared as a function");
    EXPECT_EQ(errorOf("struct s { char c : 9; };"), "1:21: width of 'c' exceeds its type");
    EXPECT_EQ(errorOf("struct s { _Bool b : 2; };"), "1:22: width of 'b' exceeds its type");
    EXPECT_EQ(errorOf("struct s { int c : -1; };"), "1:20: negative width in bit-field 'c'");
    EXPECT_EQ(errorOf("struct s { int c : 0; };"), "1:20: zero width for bit-field 'c'");
    EXPECT_EQ(errorOf("struct s { double d : 3; };"), "1:19: bit-field 'd' has invalid type");
    EXPECT_EQ(errorOf("int n; struct s { int c : n; };"), "1:27: bit-field 'c' width is not an integer constant");
    EXPECT_EQ(errorOf("struct s { int c __attribute__((aligned(3))); };"),
              "1:33: requested alignment is not a positive power of 2");
}

} // namespace
} // namespace nitwit::sema
