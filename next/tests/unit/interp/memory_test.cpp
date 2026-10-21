#include "interp/memory.hpp"

#include <array>
#include <cstddef>
#include <fstream>
#include <initializer_list>
#include <optional>
#include <string>
#include <unistd.h>
#include <vector>

#include <gtest/gtest.h>

namespace nitwit::interp {
namespace {

using value::InputId;

constexpr std::array allRegions{Region::Code, Region::ReadOnly, Region::Global, Region::Stack, Region::Heap};

Address allocate(Memory& memory, Region region, std::uint64_t size, std::uint64_t align = 8) {
    return memory.allocate(region, size, align, "object").value();
}

std::vector<std::byte> bytes(std::initializer_list<unsigned> values) {
    std::vector<std::byte> result;
    for (const unsigned value : values) {
        result.push_back(static_cast<std::byte>(value));
    }
    return result;
}

std::vector<std::byte> readBytes(const Memory& memory, Address address, std::size_t size) {
    std::vector<std::byte> out(size);
    EXPECT_TRUE(memory.read(address, out).has_value());
    return out;
}

/// The kind of the error a failed operation reports.
template <typename T>
std::optional<MemoryErrorKind> errorOf(const std::expected<T, MemoryError>& result) {
    return result.has_value() ? std::nullopt : std::optional(result.error().kind);
}

/// Resident memory of this process in bytes (Linux).
std::uint64_t residentBytes() {
    std::ifstream statm("/proc/self/statm");
    std::uint64_t totalPages = 0;
    std::uint64_t residentPages = 0;
    statm >> totalPages >> residentPages;
    return residentPages * static_cast<std::uint64_t>(sysconf(_SC_PAGESIZE));
}
class MemoryTest : public testing::TestWithParam<unsigned> {
protected:
    Memory memory{GetParam()};
};

INSTANTIATE_TEST_SUITE_P(PointerBits, MemoryTest, testing::Values(32u, 64u),
                         [](const testing::TestParamInfo<unsigned>& bits) { return std::to_string(bits.param); });

// ---- Allocation ----

TEST_P(MemoryTest, AllocatesInEveryRegionWithSizesZeroOneAndLarge) {
    for (const Region region : allRegions) {
        for (const std::uint64_t size : {std::uint64_t{0}, std::uint64_t{1}, std::uint64_t{1} << 20}) {
            const auto base = memory.allocate(region, size, 8, "object");
            ASSERT_TRUE(base.has_value()) << static_cast<int>(region) << " " << size;
            const std::optional<AllocationInfo> info = memory.find(*base);
            ASSERT_TRUE(info.has_value());
            EXPECT_EQ(info->base, *base);
            EXPECT_EQ(info->size, size);
            EXPECT_EQ(info->region, region);
            EXPECT_TRUE(info->isAlive);
            EXPECT_EQ(info->name, "object");
        }
    }
}

TEST_P(MemoryTest, ZeroSizedAllocationsHaveUniqueAddressesWithoutBytes) {
    const Address first = allocate(memory, Region::Heap, 0);
    const Address second = allocate(memory, Region::Heap, 0);
    EXPECT_NE(first, second);
    std::array<std::byte, 1> out{};
    EXPECT_EQ(errorOf(memory.read(first, out)), MemoryErrorKind::OutOfBounds);
}

TEST_P(MemoryTest, BasesAreAligned) {
    for (const Region region : {Region::Global, Region::Stack, Region::Heap}) {
        allocate(memory, region, 3, 1);
        const Address base = allocate(memory, region, 5, 4096);
        EXPECT_EQ(base % 4096, 0u);
        EXPECT_EQ(allocate(memory, region, 1, 16) % 16, 0u);
    }
}

TEST_P(MemoryTest, NamesAreKept) {
    const Address base = memory.allocate(Region::Global, 4, 4, "counter").value();
    const Address literal = memory.allocate(Region::ReadOnly, 6, 1, "string literal").value();
    EXPECT_EQ(memory.find(base)->name, "counter");
    EXPECT_EQ(memory.find(literal)->name, "string literal");
}

TEST_P(MemoryTest, LowestSixtyFourKibibytesAreNeverMapped) {
    for (const Region region : allRegions) {
        EXPECT_GE(allocate(memory, region, 16), 0x1'0000u);
    }
    std::array<std::byte, 4> out{};
    EXPECT_EQ(errorOf(memory.read(0, out)), MemoryErrorKind::NullPointer);
    EXPECT_EQ(errorOf(memory.read(4092, out)), MemoryErrorKind::NullPointer);
    EXPECT_EQ(errorOf(memory.write(8, out)), MemoryErrorKind::NullPointer);
    EXPECT_EQ(errorOf(memory.fill(0, std::byte{0}, 1)), MemoryErrorKind::NullPointer);
    // A null pointer to a large struct plus a member offset.
    EXPECT_EQ(errorOf(memory.read(0xFFFF, out)), MemoryErrorKind::NullPointer);
    EXPECT_FALSE(memory.find(0).has_value());
    EXPECT_FALSE(memory.find(0xFFFF).has_value());
}

TEST_P(MemoryTest, AllocationsAreSeparatedByGaps) {
    for (const Region region : allRegions) {
        const Address first = allocate(memory, region, 10, 1);
        const Address second = allocate(memory, region, 10, 1);
        const Address lower = std::min(first, second);
        const Address higher = std::max(first, second);
        EXPECT_GE(higher, lower + 10 + 16);
        for (Address address = lower + 11; address < higher; ++address) {
            EXPECT_FALSE(memory.find(address).has_value()) << address - lower;
        }
    }
}

TEST_P(MemoryTest, StackGrowsDownward) {
    const Address first = allocate(memory, Region::Stack, 8);
    const Address second = allocate(memory, Region::Stack, 8);
    EXPECT_LT(second, first);
}

TEST_P(MemoryTest, AddressesAreDeterministic) {
    Memory other(GetParam());
    for (const Region region : {Region::Global, Region::Stack, Region::Heap, Region::Stack, Region::Code}) {
        EXPECT_EQ(allocate(memory, region, 24), allocate(other, region, 24));
    }
    const Address freed = allocate(memory, Region::Heap, 7);
    ASSERT_TRUE(memory.free(freed).has_value());
    ASSERT_TRUE(other.free(allocate(other, Region::Heap, 7)).has_value());
    EXPECT_EQ(allocate(memory, Region::Heap, 7), allocate(other, Region::Heap, 7));
}

TEST_P(MemoryTest, RegionsAreExhaustible) {
    const auto huge = memory.allocate(Region::Heap, std::uint64_t{1} << 62, 16, "malloc");
    ASSERT_FALSE(huge.has_value());
    EXPECT_EQ(huge.error().kind, MemoryErrorKind::OutOfAddressSpace);
    EXPECT_EQ(errorOf(memory.allocate(Region::Stack, ~std::uint64_t{0}, 1, "x")), MemoryErrorKind::OutOfAddressSpace);
    EXPECT_EQ(errorOf(memory.allocate(Region::Global, 8, std::uint64_t{1} << 63, "x")),
              MemoryErrorKind::OutOfAddressSpace);
    // A failed allocation does not disturb the next one.
    EXPECT_TRUE(memory.allocate(Region::Heap, 8, 16, "malloc").has_value());
}

TEST(MemoryLayoutTest, ThirtyTwoBitAddressesFitInThirtyTwoBits) {
    Memory memory(32);
    for (const Region region : allRegions) {
        const Address base = allocate(memory, region, 1 << 20);
        EXPECT_LT(base + (1 << 20), std::uint64_t{1} << 32);
    }
}

TEST(MemoryLayoutTest, ThirtyTwoBitRegionsHaveLimits) {
    Memory memory(32);
    EXPECT_EQ(errorOf(memory.allocate(Region::Heap, std::uint64_t{2} << 30, 16, "malloc")),
              MemoryErrorKind::OutOfAddressSpace);
    EXPECT_EQ(errorOf(memory.allocate(Region::Stack, std::uint64_t{1} << 30, 16, "array")),
              MemoryErrorKind::OutOfAddressSpace);
    EXPECT_TRUE(memory.allocate(Region::Global, std::uint64_t{1} << 30, 16, "big").has_value());
    EXPECT_EQ(errorOf(memory.allocate(Region::Global, std::uint64_t{1} << 30, 16, "bigger")),
              MemoryErrorKind::OutOfAddressSpace);
}

TEST(MemoryLayoutTest, SixtyFourBitAddressesUseTheWholeSpace) {
    Memory memory(64);
    EXPECT_GT(allocate(memory, Region::Heap, 8), std::uint64_t{1} << 32);
}

// ---- Accesses ----

TEST_P(MemoryTest, WriteThenReadInside) {
    for (const Region region : {Region::Global, Region::Stack, Region::Heap}) {
        const Address base = allocate(memory, region, 8);
        ASSERT_TRUE(memory.write(base + 2, bytes({1, 2, 3})).has_value());
        EXPECT_EQ(readBytes(memory, base, 8), bytes({0, 0, 1, 2, 3, 0, 0, 0}));
    }
}

TEST_P(MemoryTest, GlobalsStartAsZero) {
    const Address base = allocate(memory, Region::Global, 4);
    EXPECT_EQ(readBytes(memory, base, 4), bytes({0, 0, 0, 0}));
}

TEST_P(MemoryTest, AccessAcrossTheEndIsOutOfBounds) {
    const Address base = allocate(memory, Region::Heap, 8);
    std::array<std::byte, 4> out{};
    const auto result = memory.read(base + 6, out);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().kind, MemoryErrorKind::OutOfBounds);
    EXPECT_EQ(result.error().address, base + 6);
    EXPECT_EQ(result.error().size, 4u);
    EXPECT_EQ(errorOf(memory.write(base + 5, out)), MemoryErrorKind::OutOfBounds);
    EXPECT_EQ(errorOf(memory.fill(base, std::byte{1}, 9)), MemoryErrorKind::OutOfBounds);
    EXPECT_EQ(readBytes(memory, base + 4, 4), bytes({0, 0, 0, 0})); // nothing was written
}

TEST_P(MemoryTest, AccessOnePastTheEndIsOutOfBounds) {
    const Address base = allocate(memory, Region::Global, 8);
    std::array<std::byte, 1> out{};
    EXPECT_EQ(errorOf(memory.read(base + 8, out)), MemoryErrorKind::OutOfBounds);
    EXPECT_EQ(errorOf(memory.write(base + 8, out)), MemoryErrorKind::OutOfBounds);
    // The one-past-the-end address belongs to the allocation; the next one lies in the gap.
    ASSERT_TRUE(memory.find(base + 8).has_value());
    EXPECT_EQ(memory.find(base + 8)->base, base);
    EXPECT_FALSE(memory.find(base + 9).has_value());
    EXPECT_EQ(errorOf(memory.read(base + 9, out)), MemoryErrorKind::InvalidAddress);
}

TEST_P(MemoryTest, ZeroByteAccessesAlwaysSucceed) {
    std::array<std::byte, 0> none{};
    EXPECT_TRUE(memory.read(0, none).has_value());
    EXPECT_TRUE(memory.write(12345, none).has_value());
    EXPECT_TRUE(memory.copy(0, 8, 0).has_value());
    EXPECT_TRUE(memory.fill(0, std::byte{1}, 0).has_value());
}

TEST_P(MemoryTest, ReadOnlyMemoryIsInitializedButNotWritable) {
    const Address literal = allocate(memory, Region::ReadOnly, 3, 1);
    memory.initialize(literal, bytes({'h', 'i', 0}));
    EXPECT_EQ(readBytes(memory, literal, 3), bytes({'h', 'i', 0}));
    EXPECT_TRUE(memory.isInitialized(literal, 3));
    EXPECT_EQ(errorOf(memory.write(literal, bytes({'H'}))), MemoryErrorKind::ReadOnlyWrite);
    EXPECT_EQ(errorOf(memory.fill(literal, std::byte{0}, 1)), MemoryErrorKind::ReadOnlyWrite);
    const Address global = allocate(memory, Region::Global, 3);
    EXPECT_EQ(errorOf(memory.copy(literal, global, 3)), MemoryErrorKind::ReadOnlyWrite);
    EXPECT_TRUE(memory.copy(global, literal, 3).has_value());
    EXPECT_EQ(readBytes(memory, global, 3), bytes({'h', 'i', 0}));
}

TEST_P(MemoryTest, CodeIsNeitherReadableNorWritable) {
    const Address function = memory.allocate(Region::Code, 1, 1, "main").value();
    std::array<std::byte, 1> out{};
    EXPECT_EQ(errorOf(memory.read(function, out)), MemoryErrorKind::CodeAccess);
    EXPECT_EQ(errorOf(memory.write(function, out)), MemoryErrorKind::CodeAccess);
    EXPECT_EQ(memory.find(function)->name, "main");
    EXPECT_EQ(memory.find(function)->region, Region::Code);
}

// ---- Heap lifetime ----

TEST_P(MemoryTest, FreedBlocksAreDeadAndStayInTheTable) {
    const Address block = allocate(memory, Region::Heap, 16);
    ASSERT_TRUE(memory.free(block).has_value());
    std::array<std::byte, 4> out{};
    EXPECT_EQ(errorOf(memory.read(block + 4, out)), MemoryErrorKind::DeadObject);
    EXPECT_EQ(errorOf(memory.write(block, out)), MemoryErrorKind::DeadObject);
    ASSERT_TRUE(memory.find(block).has_value());
    EXPECT_FALSE(memory.find(block)->isAlive);
}

TEST_P(MemoryTest, HeapAddressesAreNeverReused) {
    const Address first = allocate(memory, Region::Heap, 32);
    ASSERT_TRUE(memory.free(first).has_value());
    const Address second = allocate(memory, Region::Heap, 32);
    EXPECT_GT(second, first + 32);
    std::array<std::byte, 1> out{};
    EXPECT_EQ(errorOf(memory.read(first, out)), MemoryErrorKind::DeadObject);
}

TEST_P(MemoryTest, FreeOfNullDoesNothing) {
    EXPECT_TRUE(memory.free(0).has_value());
}

TEST_P(MemoryTest, DoubleFreeIsInvalid) {
    const Address block = allocate(memory, Region::Heap, 8);
    ASSERT_TRUE(memory.free(block).has_value());
    const auto second = memory.free(block);
    ASSERT_FALSE(second.has_value());
    EXPECT_EQ(second.error().kind, MemoryErrorKind::InvalidFree);
    EXPECT_EQ(second.error().address, block);
}

TEST_P(MemoryTest, FreeNeedsTheBaseOfAHeapBlock) {
    const Address block = allocate(memory, Region::Heap, 8);
    EXPECT_EQ(errorOf(memory.free(block + 1)), MemoryErrorKind::InvalidFree);
    EXPECT_EQ(errorOf(memory.free(block + 8)), MemoryErrorKind::InvalidFree);
    for (const Region region : {Region::Code, Region::ReadOnly, Region::Global, Region::Stack}) {
        EXPECT_EQ(errorOf(memory.free(allocate(memory, region, 8))), MemoryErrorKind::InvalidFree);
    }
    EXPECT_EQ(errorOf(memory.free(16)), MemoryErrorKind::InvalidFree);
    EXPECT_EQ(errorOf(memory.free(0x1234'5678)), MemoryErrorKind::InvalidFree);
    EXPECT_TRUE(memory.free(block).has_value()); // the failed attempts did not free it
}

TEST_P(MemoryTest, FreeOfAZeroSizedBlock) {
    const Address block = allocate(memory, Region::Heap, 0);
    EXPECT_TRUE(memory.free(block).has_value());
    EXPECT_EQ(errorOf(memory.free(block)), MemoryErrorKind::InvalidFree);
}

// ---- Stack lifetime ----

TEST_P(MemoryTest, PoppedStackAddressesAreReused) {
    const Address outer = allocate(memory, Region::Stack, 12);
    const Address inner = allocate(memory, Region::Stack, 40);
    memory.popStack(inner);
    EXPECT_EQ(allocate(memory, Region::Stack, 40), inner);
    memory.popStack(inner);
    memory.popStack(outer);
    EXPECT_EQ(allocate(memory, Region::Stack, 12), outer);
}

TEST_P(MemoryTest, PoppedStackObjectIsDeadUntilReused) {
    const Address frame = allocate(memory, Region::Stack, 8);
    const Address local = allocate(memory, Region::Stack, 4);
    memory.popStack(local);
    std::array<std::byte, 4> out{};
    EXPECT_EQ(errorOf(memory.read(local, out)), MemoryErrorKind::DeadObject);
    EXPECT_FALSE(memory.find(local)->isAlive);
    // A new local takes the same place, as in a native run.
    const Address reused = allocate(memory, Region::Stack, 4);
    EXPECT_EQ(reused, local);
    EXPECT_TRUE(memory.write(local, out).has_value());
    EXPECT_TRUE(memory.find(local)->isAlive);
    EXPECT_TRUE(memory.find(frame)->isAlive);
}

TEST_P(MemoryTest, WritesToAPoppedStackObjectAreDead) {
    allocate(memory, Region::Stack, 8);
    const Address local = allocate(memory, Region::Stack, 4);
    const Address global = allocate(memory, Region::Global, 4);
    memory.popStack(local);
    EXPECT_EQ(errorOf(memory.write(local, bytes({1}))), MemoryErrorKind::DeadObject);
    EXPECT_EQ(errorOf(memory.fill(local, std::byte{0}, 4)), MemoryErrorKind::DeadObject);
    EXPECT_EQ(errorOf(memory.copy(local, global, 4)), MemoryErrorKind::DeadObject);
}

TEST_P(MemoryTest, DeepDeadFramesStayDeadBelowAShallowerFrame) {
    const Address deep1 = allocate(memory, Region::Stack, 8);
    const Address deep2 = allocate(memory, Region::Stack, 8);
    const Address deep3 = allocate(memory, Region::Stack, 8);
    memory.popStack(deep3);
    memory.popStack(deep2);
    memory.popStack(deep1);
    // A larger object takes the place of deep1 and deep2 and ends exactly a gap above deep3, which stays dead.
    const Address big = allocate(memory, Region::Stack, 32, 1);
    EXPECT_EQ(big, deep2);
    EXPECT_EQ(memory.find(deep1)->base, big);
    std::array<std::byte, 1> out{};
    EXPECT_TRUE(memory.read(deep1, out).has_value());
    EXPECT_EQ(errorOf(memory.read(deep3, out)), MemoryErrorKind::DeadObject);
    EXPECT_FALSE(memory.find(deep3)->isAlive);
}

TEST_P(MemoryTest, StackMemoryIsUninitializedAfterReuse) {
    const Address local = allocate(memory, Region::Stack, 4);
    ASSERT_TRUE(memory.write(local, bytes({1, 2, 3, 4})).has_value());
    memory.popStack(local);
    EXPECT_EQ(allocate(memory, Region::Stack, 4), local);
    EXPECT_FALSE(memory.isInitialized(local, 1));
}

TEST(MemoryDeathTest, PopStackOutOfOrderAsserts) {
    Memory memory(64);
    const Address first = allocate(memory, Region::Stack, 8);
    allocate(memory, Region::Stack, 8);
    EXPECT_DEATH(memory.popStack(first), "LIFO");
}

TEST_P(MemoryTest, LiveBytesCountsHeapAndStack) {
    allocate(memory, Region::Global, 1000);
    allocate(memory, Region::ReadOnly, 1000);
    const Address block = allocate(memory, Region::Heap, 100);
    const Address local = allocate(memory, Region::Stack, 20);
    EXPECT_EQ(memory.liveBytes(), 120u);
    ASSERT_TRUE(memory.free(block).has_value());
    EXPECT_EQ(memory.liveBytes(), 20u);
    memory.popStack(local);
    EXPECT_EQ(memory.liveBytes(), 0u);
}

// ---- Initialization ----

TEST_P(MemoryTest, InitializationIsTrackedPerByte) {
    for (const Region region : {Region::Stack, Region::Heap}) {
        const Address base = allocate(memory, region, 24);
        EXPECT_FALSE(memory.isInitialized(base, 1));
        ASSERT_TRUE(memory.write(base + 3, bytes({1, 2})).has_value());
        EXPECT_TRUE(memory.isInitialized(base + 3, 2));
        EXPECT_FALSE(memory.isInitialized(base + 2, 2));
        EXPECT_FALSE(memory.isInitialized(base + 4, 2));
        ASSERT_TRUE(memory.fill(base + 5, std::byte{0}, 19).has_value());
        EXPECT_TRUE(memory.isInitialized(base + 3, 21));
        EXPECT_FALSE(memory.isInitialized(base, 24));
    }
}

TEST_P(MemoryTest, GlobalsCountAsInitialized) {
    const Address base = allocate(memory, Region::Global, 64);
    EXPECT_TRUE(memory.isInitialized(base, 64));
}

TEST_P(MemoryTest, CopyCarriesTheInitializationState) {
    const Address source = allocate(memory, Region::Heap, 8);
    const Address destination = allocate(memory, Region::Stack, 8);
    ASSERT_TRUE(memory.write(source + 1, bytes({7})).has_value());
    ASSERT_TRUE(memory.fill(destination, std::byte{9}, 8).has_value());
    ASSERT_TRUE(memory.copy(destination, source, 8).has_value());
    EXPECT_FALSE(memory.isInitialized(destination, 1));
    EXPECT_TRUE(memory.isInitialized(destination + 1, 1));
    EXPECT_FALSE(memory.isInitialized(destination + 2, 6));
    EXPECT_EQ(readBytes(memory, destination + 1, 1), bytes({7}));
}

TEST_P(MemoryTest, CopyFromGlobalsInitializes) {
    const Address source = allocate(memory, Region::Global, 4);
    const Address destination = allocate(memory, Region::Heap, 4);
    ASSERT_TRUE(memory.copy(destination, source, 4).has_value());
    EXPECT_TRUE(memory.isInitialized(destination, 4));
}

TEST_P(MemoryTest, CopyOfUninitializedBytesIntoAGlobal) {
    // g = *p with p from malloc: the global's bytes stay nondeterministic instead of becoming zeros.
    const Address source = allocate(memory, Region::Heap, 8);
    const Address global = allocate(memory, Region::Global, 16);
    ASSERT_TRUE(memory.write(source + 2, bytes({5})).has_value());
    ASSERT_TRUE(memory.copy(global + 4, source, 8).has_value());
    EXPECT_TRUE(memory.isInitialized(global, 4));
    EXPECT_FALSE(memory.isInitialized(global + 4, 1));
    EXPECT_TRUE(memory.isInitialized(global + 6, 1));
    EXPECT_FALSE(memory.isInitialized(global + 7, 5));
    EXPECT_TRUE(memory.isInitialized(global + 12, 4));
    ASSERT_TRUE(memory.write(global + 4, bytes({1, 2})).has_value());
    EXPECT_TRUE(memory.isInitialized(global, 7));
}

TEST_P(MemoryTest, CopyOfInitializedBytesKeepsAGlobalInitialized) {
    const Address source = allocate(memory, Region::Stack, 4);
    const Address global = allocate(memory, Region::Global, 4);
    ASSERT_TRUE(memory.fill(source, std::byte{3}, 4).has_value());
    ASSERT_TRUE(memory.copy(global, source, 4).has_value());
    EXPECT_TRUE(memory.isInitialized(global, 4));
    EXPECT_EQ(readBytes(memory, global, 4), bytes({3, 3, 3, 3}));
}

TEST_P(MemoryTest, ZeroedAllocationIsInitialized) {
    for (const Region region : {Region::Heap, Region::Stack}) {
        const Address block = memory.allocateZeroed(region, 64, 16, "calloc").value();
        EXPECT_TRUE(memory.isInitialized(block, 64));
        EXPECT_EQ(readBytes(memory, block + 60, 4), bytes({0, 0, 0, 0}));
        EXPECT_EQ(memory.find(block)->name, "calloc");
    }
    EXPECT_EQ(memory.liveBytes(), 128u);
}

TEST_P(MemoryTest, ZeroedAllocationTakesUninitializedBytesFromACopy) {
    const Address source = allocate(memory, Region::Heap, 4);
    const Address block = memory.allocateZeroed(Region::Heap, 8, 16, "calloc").value();
    ASSERT_TRUE(memory.copy(block + 2, source, 4).has_value());
    EXPECT_TRUE(memory.isInitialized(block, 2));
    EXPECT_FALSE(memory.isInitialized(block + 2, 1));
    EXPECT_FALSE(memory.isInitialized(block + 5, 1));
    EXPECT_TRUE(memory.isInitialized(block + 6, 2));
    EXPECT_TRUE(memory.free(block).has_value());
}

TEST_P(MemoryTest, ZeroedHeapBlockOfOneGibibyteCostsNothing) {
    constexpr std::uint64_t size = std::uint64_t{1} << 30;
    const std::uint64_t before = residentBytes();
    const Address block = memory.allocateZeroed(Region::Heap, size, 16, "calloc").value();
    EXPECT_TRUE(memory.isInitialized(block + size - 8, 8));
    EXPECT_EQ(readBytes(memory, block + size / 2, 4), bytes({0, 0, 0, 0}));
    EXPECT_LT(residentBytes(), before + (std::uint64_t{64} << 20));
}

TEST_P(MemoryTest, FailedCopyWritesNothing) {
    const Address source = allocate(memory, Region::Heap, 8);
    const Address destination = allocate(memory, Region::Heap, 8);
    ASSERT_TRUE(memory.fill(source, std::byte{1}, 8).has_value());
    ASSERT_TRUE(memory.fill(destination, std::byte{2}, 8).has_value());
    memory.setOrigin(source, 4, InputId{1});
    memory.setOrigin(destination, 4, InputId{2});
    const auto expectFailure = [&](Address to, Address from, std::uint64_t size, MemoryErrorKind kind,
                                   Address reported) {
        const auto result = memory.copy(to, from, size);
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().kind, kind);
        EXPECT_EQ(result.error().address, reported);
        EXPECT_EQ(readBytes(memory, destination, 8), bytes({2, 2, 2, 2, 2, 2, 2, 2}));
        EXPECT_EQ(memory.origin(destination, 4), InputId{2});
    };
    expectFailure(destination, source + 4, 8, MemoryErrorKind::OutOfBounds, source + 4);      // source too short
    expectFailure(destination + 4, source, 8, MemoryErrorKind::OutOfBounds, destination + 4); // destination too short
    const Address freed = allocate(memory, Region::Heap, 8);
    ASSERT_TRUE(memory.free(freed).has_value());
    expectFailure(destination, freed, 8, MemoryErrorKind::DeadObject, freed);
    expectFailure(freed, source, 8, MemoryErrorKind::DeadObject, freed);
    EXPECT_EQ(memory.origin(source, 4), InputId{1});
}

TEST_P(MemoryTest, OverlappingCopyIsAMemmove) {
    const Address block = allocate(memory, Region::Heap, 12);
    ASSERT_TRUE(memory.write(block, bytes({1, 2, 3, 4, 5})).has_value());
    // Forward overlap: bytes 0..4 to 2..6; then backward overlap: bytes 2..6 to 1..5.
    ASSERT_TRUE(memory.copy(block + 2, block, 5).has_value());
    EXPECT_EQ(readBytes(memory, block, 7), bytes({1, 2, 1, 2, 3, 4, 5}));
    EXPECT_TRUE(memory.isInitialized(block, 7));
    EXPECT_FALSE(memory.isInitialized(block + 7, 1));
    ASSERT_TRUE(memory.write(block + 9, bytes({9})).has_value());
    ASSERT_TRUE(memory.copy(block + 5, block + 7, 3).has_value()); // bytes 7 (unwritten), 8 (unwritten), 9
    EXPECT_FALSE(memory.isInitialized(block + 5, 1));
    EXPECT_FALSE(memory.isInitialized(block + 6, 1));
    EXPECT_TRUE(memory.isInitialized(block + 7, 1));
    EXPECT_EQ(readBytes(memory, block + 7, 1), bytes({9}));
}

TEST_P(MemoryTest, CopyReportsTheSourceFirst) {
    const Address literal = allocate(memory, Region::ReadOnly, 4);
    const auto result = memory.copy(literal, 0x10, 4);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().kind, MemoryErrorKind::NullPointer);
    EXPECT_EQ(result.error().address, 0x10u);
}

TEST(MemoryDeathTest, IsInitializedNeedsAReadableRange) {
    Memory memory(64);
    const Address base = allocate(memory, Region::Heap, 4);
    EXPECT_DEATH((void)memory.isInitialized(base, 5), "readable range");
}

TEST(MemoryDeathTest, InitializeNeedsAReadOnlyAllocation) {
    Memory memory(64);
    const Address base = allocate(memory, Region::Global, 4);
    EXPECT_DEATH(memory.initialize(base, bytes({1})), "ReadOnly allocation");
}

// ---- Origins ----

TEST_P(MemoryTest, OriginIsReadBackForExactlyItsRange) {
    const Address base = allocate(memory, Region::Stack, 16);
    ASSERT_TRUE(memory.write(base, bytes({1, 2, 3, 4})).has_value());
    memory.setOrigin(base, 4, InputId{7});
    EXPECT_EQ(memory.origin(base, 4), InputId{7});
    EXPECT_FALSE(memory.origin(base, 2).has_value());
    EXPECT_FALSE(memory.origin(base + 1, 3).has_value());
    EXPECT_FALSE(memory.origin(base + 4, 4).has_value());
}

TEST_P(MemoryTest, PartialOverwriteDropsTheOrigin) {
    const Address base = allocate(memory, Region::Heap, 16);
    memory.setOrigin(base, 4, InputId{1});
    memory.setOrigin(base + 4, 4, InputId{2});
    memory.setOrigin(base + 8, 4, InputId{3});
    ASSERT_TRUE(memory.write(base + 7, bytes({0, 0})).has_value()); // last byte of 2 and first byte of 3
    EXPECT_EQ(memory.origin(base, 4), InputId{1});
    EXPECT_FALSE(memory.origin(base + 4, 4).has_value());
    EXPECT_FALSE(memory.origin(base + 8, 4).has_value());
    ASSERT_TRUE(memory.fill(base + 3, std::byte{0}, 1).has_value());
    EXPECT_FALSE(memory.origin(base, 4).has_value());
}

TEST_P(MemoryTest, SetOriginReplacesOverlappingOrigins) {
    const Address base = allocate(memory, Region::Global, 16);
    memory.setOrigin(base, 8, InputId{1});
    memory.setOrigin(base + 4, 4, InputId{2});
    EXPECT_FALSE(memory.origin(base, 8).has_value());
    EXPECT_EQ(memory.origin(base + 4, 4), InputId{2});
}

TEST_P(MemoryTest, CopyCarriesOriginsInsideTheRange) {
    const Address source = allocate(memory, Region::Heap, 16);
    const Address destination = allocate(memory, Region::Heap, 16);
    memory.setOrigin(source, 4, InputId{1});
    memory.setOrigin(source + 4, 8, InputId{2});
    memory.setOrigin(source + 12, 4, InputId{3});
    memory.setOrigin(destination, 4, InputId{9});
    ASSERT_TRUE(memory.copy(destination, source + 2, 12).has_value()); // source bytes 2..13
    EXPECT_FALSE(memory.origin(destination, 4).has_value());           // overwritten
    EXPECT_EQ(memory.origin(destination + 2, 8), InputId{2});          // entirely inside
    EXPECT_FALSE(memory.origin(destination + 10, 4).has_value());      // only partly inside
    // The source keeps its origins.
    EXPECT_EQ(memory.origin(source, 4), InputId{1});
    EXPECT_EQ(memory.origin(source + 4, 8), InputId{2});
}

TEST_P(MemoryTest, OverlappingCopyMovesOrigins) {
    const Address block = allocate(memory, Region::Heap, 32);
    memory.setOrigin(block, 4, InputId{1});
    memory.setOrigin(block + 4, 4, InputId{2});
    ASSERT_TRUE(memory.copy(block + 4, block, 8).has_value()); // memmove(block + 4, block, 8)
    EXPECT_EQ(memory.origin(block, 4), InputId{1});
    EXPECT_EQ(memory.origin(block + 4, 4), InputId{1});
    EXPECT_EQ(memory.origin(block + 8, 4), InputId{2});
    ASSERT_TRUE(memory.copy(block, block + 4, 8).has_value()); // and back
    EXPECT_EQ(memory.origin(block, 4), InputId{1});
    EXPECT_EQ(memory.origin(block + 4, 4), InputId{2});
    EXPECT_EQ(memory.origin(block + 8, 4), InputId{2});
}

TEST_P(MemoryTest, EndOfLifetimeDropsOrigins) {
    const Address block = allocate(memory, Region::Heap, 8);
    memory.setOrigin(block, 8, InputId{4});
    ASSERT_TRUE(memory.free(block).has_value());
    EXPECT_FALSE(memory.origin(block, 8).has_value());
    const Address local = allocate(memory, Region::Stack, 8);
    memory.setOrigin(local, 8, InputId{5});
    memory.popStack(local);
    EXPECT_EQ(allocate(memory, Region::Stack, 8), local);
    EXPECT_FALSE(memory.origin(local, 8).has_value());
}

// ---- Lazy commit ----

TEST_P(MemoryTest, OneGibibyteGlobalCostsOnlyTheWrittenPages) {
    constexpr std::uint64_t size = std::uint64_t{1} << 30;
    const std::uint64_t before = residentBytes();
    const Address array = memory.allocate(Region::Global, size, 16, "array").value();
    ASSERT_TRUE(memory.write(array + 12345, bytes({1, 2, 3, 4})).has_value());
    ASSERT_TRUE(memory.write(array + size - 4, bytes({5, 6, 7, 8})).has_value());
    EXPECT_EQ(readBytes(memory, array + 12345, 4), bytes({1, 2, 3, 4}));
    EXPECT_EQ(readBytes(memory, array + size - 4, 4), bytes({5, 6, 7, 8}));
    EXPECT_EQ(readBytes(memory, array + size / 2, 4), bytes({0, 0, 0, 0}));
    EXPECT_LT(residentBytes(), before + (std::uint64_t{64} << 20));
}

} // namespace
} // namespace nitwit::interp
