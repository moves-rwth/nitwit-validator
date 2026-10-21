#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "library_test_support.hpp"

namespace nitwit::interp::test {
namespace {

using value::InputId;

class BuiltinsTest : public LibraryTest {
protected:
    /// Calls an overflow builtin; returns whether it reported overflow, and stores the result in `result`.
    bool overflow(const char* function, Int128 a, Int128 b, ScalarType type, Int128& result) {
        const Address object = buffer(8);
        const Int128 overflowed = callInteger(function, {integer(a, type), integer(b, type), pointer(object)});
        result = readInteger(object, type);
        return overflowed != 0;
    }
};

INSTANTIATE_TEST_SUITE_P(DataModels, BuiltinsTest, testing::Values(DataModel::ILP32, DataModel::LP64), modelName);

TEST_P(BuiltinsTest, ExpectReturnsItsArgumentWithItsOrigin) {
    TypedValue argument = integer(5, ScalarType::Long);
    argument.value.setOrigin(InputId{2});
    const LibraryResult result = call("__builtin_expect", {argument, integer(0, ScalarType::Long)});
    EXPECT_EQ(returned(result), 5);
    EXPECT_EQ(result.value.origin(), InputId{2});
    EXPECT_EQ(callInteger("__builtin_expect_with_probability",
                          {integer(-1, ScalarType::Long), integer(1, ScalarType::Long), value::Value::ofDouble(0.9)}),
              -1);
    EXPECT_EQ(callPointer("__builtin_assume_aligned", {pointer(0x1000), size(16)}), 0x1000u);
}

TEST_P(BuiltinsTest, UnreachableIsUndefinedAndTrapAborts) {
    EXPECT_TRUE(isUndefined(call("__builtin_unreachable", {}), "__builtin_unreachable was reached"));
    EXPECT_TRUE(endedWith(call("__builtin_trap", {}), LibraryResult::Kind::Abort));
    EXPECT_TRUE(endedWith(call("__builtin_abort", {}), LibraryResult::Kind::Abort));
}

TEST_P(BuiltinsTest, ConstantPIsZeroForValues) {
    EXPECT_EQ(callInteger("__builtin_constant_p", {intValue(1)}), 0);
    EXPECT_EQ(call("__builtin_prefetch", {pointer(0)}).kind, LibraryResult::Kind::Return);
}

// The library sees only pointers whose object GCC does not know; calls on known objects are folded before.
TEST_P(BuiltinsTest, ObjectSizeOfAnUnknownObject) {
    const Int128 unknown = lp64() ? Int128{UINT64_MAX} : Int128{UINT32_MAX};
    for (const char* function : {"__builtin_object_size", "__builtin_dynamic_object_size"}) {
        EXPECT_EQ(callInteger(function, {pointer(0x1000), intValue(0)}), unknown) << function;
        EXPECT_EQ(callInteger(function, {pointer(0x1000), intValue(1)}), unknown) << function;
        EXPECT_EQ(callInteger(function, {pointer(0x1000), intValue(2)}), 0) << function;
        EXPECT_EQ(callInteger(function, {pointer(0x1000), intValue(3)}), 0) << function;
    }
}

TEST_P(BuiltinsTest, ByteSwaps) {
    EXPECT_EQ(callInteger("__builtin_bswap16", {integer(0x1234, ScalarType::UnsignedShort)}), 0x3412);
    EXPECT_EQ(callInteger("__builtin_bswap32", {integer(0x12345678, ScalarType::UnsignedInt)}), 0x78563412);
    EXPECT_EQ(callInteger("__builtin_bswap64", {integer(0x0102030405060708, ScalarType::UnsignedLongLong)}),
              0x0807060504030201);
    const UInt128 big = (UInt128{0x0102030405060708} << 64) | 0x090A0B0C0D0E0F10;
    const UInt128 swapped = (UInt128{0x100F0E0D0C0B0A09} << 64) | 0x0807060504030201;
    EXPECT_EQ(call("__builtin_bswap128", {integer(static_cast<Int128>(big), ScalarType::UnsignedInt128)}).value.bits(),
              swapped);
    EXPECT_EQ(callInteger("htons", {integer(0x1234, ScalarType::UnsignedShort)}), 0x3412);
    EXPECT_EQ(callInteger("ntohl", {integer(0x12345678, ScalarType::UnsignedInt)}), 0x78563412);
}

TEST_P(BuiltinsTest, CountLeadingAndTrailingZeros) {
    EXPECT_EQ(callInteger("__builtin_clz", {integer(1, ScalarType::UnsignedInt)}), 31);
    EXPECT_EQ(callInteger("__builtin_clz", {integer(0x80000000, ScalarType::UnsignedInt)}), 0);
    EXPECT_EQ(callInteger("__builtin_clzl", {integer(1, ScalarType::UnsignedLong)}), lp64() ? 63 : 31);
    EXPECT_EQ(callInteger("__builtin_clzll", {integer(1, ScalarType::UnsignedLongLong)}), 63);
    EXPECT_EQ(callInteger("__builtin_ctz", {integer(8, ScalarType::UnsignedInt)}), 3);
    EXPECT_EQ(callInteger("__builtin_ctzl", {integer(0x80000000, ScalarType::UnsignedLong)}), 31);
    EXPECT_EQ(callInteger("__builtin_ctzll", {integer(Int128{1} << 63, ScalarType::UnsignedLongLong)}), 63);
    // The argument converts to the parameter type: -1 as unsigned int has no leading zeros.
    EXPECT_EQ(callInteger("__builtin_clz", {intValue(-1)}), 0);
}

TEST_P(BuiltinsTest, CountingZerosOfZeroIsUndefined) {
    EXPECT_TRUE(isUndefined(call("__builtin_clz", {integer(0, ScalarType::UnsignedInt)}), "__builtin_clz of 0"));
    EXPECT_TRUE(isUndefined(call("__builtin_ctzll", {integer(0, ScalarType::UnsignedLongLong)}), "of 0"));
}

TEST_P(BuiltinsTest, PopcountParityFfsAndClrsb) {
    EXPECT_EQ(callInteger("__builtin_popcount", {integer(0xF0F0, ScalarType::UnsignedInt)}), 8);
    EXPECT_EQ(callInteger("__builtin_popcountl", {integer(-1, ScalarType::UnsignedLong)}), lp64() ? 64 : 32);
    EXPECT_EQ(callInteger("__builtin_popcountll", {integer(-1, ScalarType::UnsignedLongLong)}), 64);
    EXPECT_EQ(callInteger("__builtin_parity", {integer(7, ScalarType::UnsignedInt)}), 1);
    EXPECT_EQ(callInteger("__builtin_parityll", {integer(3, ScalarType::UnsignedLongLong)}), 0);
    EXPECT_EQ(callInteger("ffs", {intValue(0)}), 0);
    EXPECT_EQ(callInteger("__builtin_ffs", {intValue(12)}), 3);
    EXPECT_EQ(callInteger("ffs", {intValue(INT32_MIN)}), 32);
    EXPECT_EQ(callInteger("ffsll", {integer(Int128{1} << 40, ScalarType::LongLong)}), 41);
    EXPECT_EQ(callInteger("__builtin_clrsb", {intValue(0)}), 31);
    EXPECT_EQ(callInteger("__builtin_clrsb", {intValue(-1)}), 31);
    EXPECT_EQ(callInteger("__builtin_clrsb", {intValue(1)}), 30);
    EXPECT_EQ(callInteger("__builtin_clrsbll", {integer(-2, ScalarType::LongLong)}), 62);
}

TEST_P(BuiltinsTest, SignedOverflowBuiltins) {
    Int128 result = 0;
    EXPECT_FALSE(overflow("__builtin_sadd_overflow", 1, 2, ScalarType::Int, result));
    EXPECT_EQ(result, 3);
    EXPECT_TRUE(overflow("__builtin_sadd_overflow", INT32_MAX, 1, ScalarType::Int, result));
    EXPECT_EQ(result, INT32_MIN);
    EXPECT_TRUE(overflow("__builtin_ssub_overflow", INT32_MIN, 1, ScalarType::Int, result));
    EXPECT_EQ(result, INT32_MAX);
    EXPECT_TRUE(overflow("__builtin_smul_overflow", INT32_MIN, -1, ScalarType::Int, result));
    EXPECT_EQ(result, INT32_MIN);
    EXPECT_EQ(overflow("__builtin_saddl_overflow", INT32_MAX, 1, ScalarType::Long, result), !lp64());
    EXPECT_TRUE(overflow("__builtin_smulll_overflow", INT64_MAX, 2, ScalarType::LongLong, result));
    EXPECT_EQ(result, -2);
    EXPECT_FALSE(overflow("__builtin_smulll_overflow", INT64_MIN, 1, ScalarType::LongLong, result));
}

TEST_P(BuiltinsTest, UnsignedOverflowBuiltins) {
    Int128 result = 0;
    EXPECT_TRUE(overflow("__builtin_uadd_overflow", UINT32_MAX, 2, ScalarType::UnsignedInt, result));
    EXPECT_EQ(result, 1);
    EXPECT_TRUE(overflow("__builtin_usub_overflow", 1, 2, ScalarType::UnsignedInt, result));
    EXPECT_EQ(result, UINT32_MAX);
    EXPECT_FALSE(overflow("__builtin_usub_overflow", 2, 2, ScalarType::UnsignedInt, result));
    EXPECT_EQ(result, 0);
    EXPECT_EQ(overflow("__builtin_umull_overflow", UINT32_MAX, 2, ScalarType::UnsignedLong, result), !lp64());
    EXPECT_EQ(overflow("__builtin_uaddl_overflow", UINT32_MAX, 1, ScalarType::UnsignedLong, result), !lp64());
    EXPECT_TRUE(overflow("__builtin_umulll_overflow", UINT64_MAX, UINT64_MAX, ScalarType::UnsignedLongLong, result));
    EXPECT_EQ(result, 1);
}

TEST_P(BuiltinsTest, OverflowBuiltinsWriteThroughTheirPointer) {
    EXPECT_TRUE(isUndefined(call("__builtin_sadd_overflow", {intValue(1), intValue(1), pointer(0)}), "null pointer"));
    const LibraryResult result = call("__builtin_uadd_overflow", {intValue(1), intValue(1), pointer(buffer(4))});
    EXPECT_EQ(result.value.bits(), value::Value::integer(0, {.bits = 8, .isSigned = false}).bits()); // a _Bool
}

} // namespace
} // namespace nitwit::interp::test
