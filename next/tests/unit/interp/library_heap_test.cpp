#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "library_test_support.hpp"

namespace nitwit::interp::test {
namespace {

using value::InputId;

class HeapTest : public LibraryTest {
protected:
    Address malloc(std::uint64_t bytes) { return callPointer("malloc", {size(static_cast<Int128>(bytes))}); }
};

INSTANTIATE_TEST_SUITE_P(DataModels, HeapTest, testing::Values(DataModel::ILP32, DataModel::LP64), modelName);

TEST_P(HeapTest, MallocReturnsAnUninitializedAlignedHeapBlock) {
    const Address block = malloc(24);
    const std::optional<AllocationInfo> info = memory().find(block);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->base, block);
    EXPECT_EQ(info->size, 24u);
    EXPECT_EQ(info->region, Region::Heap);
    EXPECT_EQ(block % 16, 0u);
    EXPECT_FALSE(memory().isInitialized(block, 1));
}

TEST_P(HeapTest, MallocOfZeroReturnsAUniqueNonNullPointer) {
    const Address first = malloc(0);
    const Address second = malloc(0);
    EXPECT_NE(first, 0u);
    EXPECT_NE(first, second);
    EXPECT_EQ(call("free", {pointer(first)}).kind, LibraryResult::Kind::Return);
}

TEST_P(HeapTest, CallocReturnsZeroedMemory) {
    const Address block = callPointer("calloc", {size(3), size(5)});
    EXPECT_EQ(memory().find(block)->size, 15u);
    EXPECT_TRUE(memory().isInitialized(block, 15));
    EXPECT_EQ(readBytes(block, 15), std::vector<std::byte>(15));
}

TEST_P(HeapTest, CallocWhoseSizeOverflowsIsOutOfMemory) {
    const Int128 half = lp64() ? Int128{1} << 32 : Int128{1} << 16;
    EXPECT_TRUE(endedWith(call("calloc", {size(half), size(half)}), LibraryResult::Kind::OutOfMemory, "overflows"));
}

TEST_P(HeapTest, AllocationBeyondTheLimitIsOutOfMemory) {
    context.heapLimit = 100;
    malloc(60);
    EXPECT_TRUE(endedWith(call("malloc", {size(60)}), LibraryResult::Kind::OutOfMemory, "cannot allocate 60 bytes"));
    EXPECT_TRUE(endedWith(call("alloca", {size(60)}), LibraryResult::Kind::OutOfMemory));
    EXPECT_TRUE(endedWith(call("strdup", {pointer(string(std::string(70, 'x')))}), LibraryResult::Kind::OutOfMemory));
}

TEST_P(HeapTest, FreeEndsTheLifetime) {
    const Address block = malloc(8);
    EXPECT_EQ(call("free", {pointer(block)}).kind, LibraryResult::Kind::Return);
    EXPECT_FALSE(memory().find(block)->isAlive);
}

TEST_P(HeapTest, FreeOfNullDoesNothing) {
    EXPECT_EQ(call("free", {pointer(0)}).kind, LibraryResult::Kind::Return);
}

TEST_P(HeapTest, InvalidFreesAreUndefined) {
    const Address block = malloc(8);
    EXPECT_TRUE(isUndefined(call("free", {pointer(block + 1)}), "not to its start"));
    EXPECT_TRUE(isUndefined(call("free", {pointer(string("global"))}), "not a heap block"));
    EXPECT_TRUE(isUndefined(call("free", {pointer(buffer(8))}), "not a heap block"));
    EXPECT_TRUE(isUndefined(call("free", {pointer(0x1234)}), "not a heap block"));
    ASSERT_EQ(call("free", {pointer(block)}).kind, LibraryResult::Kind::Return);
    EXPECT_TRUE(isUndefined(call("free", {pointer(block)}), "freed before"));
}

TEST_P(HeapTest, ReallocMovesContentsInitializationAndOrigins) {
    const Address block = malloc(8);
    writeInteger(block, 0x11223344, ScalarType::Int);
    memory().setOrigin(block, 4, InputId{7});
    const Address larger = callPointer("realloc", {pointer(block), size(16)});
    EXPECT_NE(larger, block);
    EXPECT_FALSE(memory().find(block)->isAlive);
    EXPECT_EQ(memory().find(larger)->size, 16u);
    EXPECT_EQ(readInteger(larger, ScalarType::Int), 0x11223344);
    EXPECT_EQ(memory().origin(larger, 4), InputId{7});
    EXPECT_TRUE(memory().isInitialized(larger, 4));
    EXPECT_FALSE(memory().isInitialized(larger + 4, 1)); // uninitialized in the old block
    EXPECT_FALSE(memory().isInitialized(larger + 8, 1)); // beyond the old block
}

TEST_P(HeapTest, ReallocToASmallerSizeKeepsThePrefix) {
    const Address block = malloc(8);
    writeInteger(block, 0x0102030405060708, ScalarType::LongLong);
    const Address smaller = callPointer("realloc", {pointer(block), size(2)});
    EXPECT_EQ(memory().find(smaller)->size, 2u);
    EXPECT_EQ(readInteger(smaller, ScalarType::Short), 0x0708);
}

TEST_P(HeapTest, ReallocOfNullIsMalloc) {
    const Address block = callPointer("realloc", {pointer(0), size(5)});
    EXPECT_EQ(memory().find(block)->size, 5u);
    EXPECT_EQ(memory().find(block)->region, Region::Heap);
}

TEST_P(HeapTest, ReallocToSizeZeroFreesAndReturnsNull) {
    const Address block = malloc(5);
    EXPECT_EQ(callPointer("realloc", {pointer(block), size(0)}), 0u);
    EXPECT_FALSE(memory().find(block)->isAlive);
}

TEST_P(HeapTest, ReallocOfInvalidPointersIsUndefined) {
    const Address block = malloc(5);
    EXPECT_TRUE(isUndefined(call("realloc", {pointer(block + 2), size(8)}), "not to its start"));
    EXPECT_TRUE(isUndefined(call("realloc", {pointer(string("x")), size(8)}), "not a heap block"));
    call("free", {pointer(block)});
    EXPECT_TRUE(isUndefined(call("realloc", {pointer(block), size(8)}), "freed before"));
}

TEST_P(HeapTest, AllocaReturnsABlockThatLivesOn) {
    const Address block = callPointer("alloca", {size(32)});
    EXPECT_EQ(memory().find(block)->size, 32u);
    EXPECT_EQ(callPointer("__builtin_alloca", {size(4)}) % 16, 0u);
}

} // namespace
} // namespace nitwit::interp::test
