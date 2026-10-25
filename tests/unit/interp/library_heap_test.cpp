#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "library_test_support.hpp"

namespace nitwit::interp::test {
namespace {

using value::TermId;

constexpr value::IntegerFormat s32{.bits = 32, .isSigned = true};
constexpr Int128 enomem = 12; // ENOMEM of Linux

class HeapTest : public LibraryTest {
protected:
    Address malloc(std::uint64_t bytes) { return callPointer("malloc", {size(static_cast<Int128>(bytes))}); }

    /// The largest size that malloc does not refuse (interp/README.md, "Library"): PTRDIFF_MAX with 32-bit pointers,
    /// which glibc 2.39 allocates natively (gcc-14 -m32), and 2^47 - 1 with 64-bit ones.
    [[nodiscard]] Int128 largestSize() const { return lp64() ? (Int128{1} << 47) - 1 : (Int128{1} << 31) - 1; }
    [[nodiscard]] Int128 sizeMax() const { return lp64() ? (Int128{1} << 64) - 1 : (Int128{1} << 32) - 1; }

    /// Whether the call returns a null pointer and sets errno to ENOMEM, as glibc does for a block no run can
    /// allocate. errno is cleared first.
    ::testing::AssertionResult refuses(std::string_view name, const std::vector<TypedValue>& arguments) {
        writeInteger(context.objectAddress("errno"), 0, ScalarType::Int);
        const LibraryResult result = call(name, arguments);
        if (result.kind != LibraryResult::Kind::Return) {
            return ::testing::AssertionFailure() << name << " did not return: " << result.message;
        }
        if (result.value.bits() != 0 || errnoValue() != enomem) {
            return ::testing::AssertionFailure()
                   << name << " returned " << static_cast<std::uint64_t>(result.value.bits()) << " with errno "
                   << static_cast<int>(errnoValue());
        }
        return ::testing::AssertionSuccess();
    }
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

TEST_P(HeapTest, MallocOfASizeNoRunCanAllocateReturnsNullWithEnomem) {
    EXPECT_TRUE(refuses("malloc", {size(largestSize() + 1)}));
    EXPECT_TRUE(refuses("malloc", {size(sizeMax() / 2 + 1)})); // PTRDIFF_MAX + 1
    EXPECT_TRUE(refuses("malloc", {size(sizeMax())}));
    if (lp64()) {
        EXPECT_TRUE(refuses("malloc", {size((Int128{1} << 55) - 2)})); // what witnesses of aws harnesses give
        EXPECT_TRUE(refuses("malloc", {size(sizeMax() / 2)}));         // PTRDIFF_MAX
    }
    EXPECT_TRUE(refuses("__builtin_malloc", {size(sizeMax())}));
}

TEST_P(HeapTest, MallocBelowThoseSizesEndsTheRunAtTheMemoryLimit) {
    // Below the sizes that no run can allocate, the interpreter's limit ends the run: a native run might get the block.
    writeInteger(context.objectAddress("errno"), 0, ScalarType::Int);
    EXPECT_TRUE(endedWith(call("malloc", {size(largestSize())}), LibraryResult::Kind::OutOfMemory, "cannot allocate"));
    EXPECT_EQ(errnoValue(), 0);
}

TEST_P(HeapTest, CallocOfAProductNoRunCanAllocateReturnsNullWithEnomem) {
    EXPECT_TRUE(refuses("calloc", {size((largestSize() + 1) / 2), size(2)}));
    EXPECT_TRUE(refuses("calloc", {size(1), size(largestSize() + 1)}));
    const Int128 root = lp64() ? Int128{1} << 32 : Int128{1} << 16; // root * root overflows size_t
    EXPECT_TRUE(refuses("calloc", {size(root), size(root)}));
    EXPECT_TRUE(refuses("calloc", {size(sizeMax()), size(sizeMax())}));
    EXPECT_TRUE(endedWith(call("calloc", {size(largestSize() / 2), size(2)}), LibraryResult::Kind::OutOfMemory));
    EXPECT_TRUE(endedWith(call("calloc", {size(largestSize()), size(1)}), LibraryResult::Kind::OutOfMemory));
}

TEST_P(HeapTest, ReallocToASizeNoRunCanAllocateReturnsNullAndKeepsTheBlock) {
    const Address block = malloc(8);
    writeInteger(block, 0x11223344, ScalarType::Int);
    EXPECT_TRUE(refuses("realloc", {pointer(block), size(largestSize() + 1)}));
    EXPECT_TRUE(refuses("realloc", {pointer(block), size(sizeMax())}));
    const std::optional<AllocationInfo> info = memory().find(block);
    ASSERT_TRUE(info.has_value());
    EXPECT_TRUE(info->isAlive);
    EXPECT_EQ(info->size, 8u);
    EXPECT_EQ(readInteger(block, ScalarType::Int), 0x11223344);
    EXPECT_FALSE(memory().isInitialized(block + 4, 1));
    EXPECT_TRUE(refuses("realloc", {pointer(0), size(largestSize() + 1)}));
    EXPECT_TRUE(endedWith(call("realloc", {pointer(block), size(largestSize())}), LibraryResult::Kind::OutOfMemory));
}

TEST_P(HeapTest, ReallocChecksThePointerBeforeTheSize) {
    const Address block = malloc(5);
    EXPECT_TRUE(isUndefined(call("realloc", {pointer(block + 2), size(sizeMax())}), "not to its start"));
}

TEST_P(HeapTest, AllocationBeyondTheLimitIsOutOfMemory) {
    context.heapLimit = 100;
    malloc(60);
    EXPECT_TRUE(endedWith(call("malloc", {size(60)}), LibraryResult::Kind::OutOfMemory, "cannot allocate 60 bytes"));
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
    // A freed block may be part of a merged dead range: what counts is that it is dead.
    EXPECT_TRUE(isUndefined(call("free", {pointer(block + 1)}), "freed before"));
}

TEST_P(HeapTest, ReallocMovesContentsInitializationAndTerms) {
    const Address block = malloc(8);
    const std::array bytes{std::byte{0x44}, std::byte{0x33}, std::byte{0x22}, std::byte{0x11}};
    ASSERT_TRUE(memory().write(block, bytes, StoredTerm{.term = TermId{7}, .format = s32}).has_value());
    const Address larger = callPointer("realloc", {pointer(block), size(16)});
    EXPECT_NE(larger, block);
    EXPECT_FALSE(memory().find(block)->isAlive);
    EXPECT_EQ(memory().find(larger)->size, 16u);
    EXPECT_EQ(readInteger(larger, ScalarType::Int), 0x11223344);
    EXPECT_EQ(memory().term(larger, 4).kind, RangeTerm::Kind::Stored);
    EXPECT_EQ(memory().term(larger, 4).stored.term, TermId{7});
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

TEST_P(HeapTest, ReallocGrowsABlockCloseToTheLimit) {
    context.heapLimit = 100;
    const Address block = malloc(60);
    writeInteger(block, 0x11223344, ScalarType::Int);
    writeInteger(block + 56, 0x55667788, ScalarType::Int);
    const Address grown = callPointer("realloc", {pointer(block), size(70)});
    EXPECT_FALSE(memory().find(block)->isAlive);
    EXPECT_EQ(memory().find(grown)->size, 70u);
    EXPECT_EQ(readInteger(grown, ScalarType::Int), 0x11223344);
    EXPECT_EQ(readInteger(grown + 56, ScalarType::Int), 0x55667788);
    EXPECT_FALSE(memory().isInitialized(grown + 4, 1)); // uninitialized in the old block
    EXPECT_FALSE(memory().isInitialized(grown + 60, 1));
    EXPECT_TRUE(endedWith(call("realloc", {pointer(grown), size(101)}), LibraryResult::Kind::OutOfMemory));
}

TEST_P(HeapTest, AllocaIsTheEvaluators) {
    // Its blocks belong to the stack frame, so free and realloc reject them as non-heap pointers.
    EXPECT_EQ(findLibraryFunction("alloca"), nullptr);
    EXPECT_EQ(findLibraryFunction("__builtin_alloca"), nullptr);
    const Address local = buffer(16, Region::Stack);
    EXPECT_TRUE(isUndefined(call("free", {pointer(local)}), "not a heap block"));
    EXPECT_TRUE(isUndefined(call("realloc", {pointer(local), size(32)}), "not a heap block"));
}

} // namespace
} // namespace nitwit::interp::test
