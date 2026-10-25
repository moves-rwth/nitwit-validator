// malloc and friends (C17 7.22.3). alloca is the evaluator's: its blocks belong to the stack frame.

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <format>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "interp/library_call.hpp"
#include "support/assert.hpp"

namespace nitwit::interp::library {

namespace {

/// Undefined behaviour unless `address` is the start of a live heap block (C17 7.22.3.3p2, 7.22.3.5p3).
void requireHeapBlock(const Call& call, Address address) {
    const std::optional<AllocationInfo> block = call.memory().find(address);
    if (!block || block->region != Region::Heap) {
        call.undefinedBehavior(std::format("{} of {:#x}, which is not a heap block", call.name(), address));
    }
    // Dead blocks first: Memory merges adjacent ones into a range whose base is that of its first block.
    if (!block->isAlive) {
        call.undefinedBehavior(std::format("{} of {:#x}, a heap block that was freed before", call.name(), address));
    }
    if (block->base != address) {
        call.undefinedBehavior(
            std::format("{} of {:#x}, which points into a heap block but not to its start", call.name(), address));
    }
}

void freeBlock(const Call& call, Address address) {
    const auto freed = call.memory().free(address);
    NITWIT_ASSERT(freed.has_value(), "requireHeapBlock admits only blocks that can be freed");
}

/// Whether no native run can allocate a block of `size` bytes (interp/README.md, "Library"): glibc refuses sizes
/// above PTRDIFF_MAX, and no block reaches the size of the user address space, 2^47 bytes on x86-64 (all that mmap
/// gives without an address hint above it) and 4 GiB for an i386 process.
bool noRunCanAllocate(const Call& call, UInt128 size) {
    const UInt128 ptrdiffMax = (UInt128{1} << (call.format(call.target().ptrdiffType()).bits - 1)) - 1;
    const UInt128 addressSpace = UInt128{1} << (call.target().pointerBits() == 64 ? 47 : 32);
    return size > ptrdiffMax || size >= addressSpace;
}

/// What glibc returns for a block that no run can allocate: a null pointer, with errno ENOMEM.
LibraryResult refuseAllocation(const Call& call) {
    call.setErrno(ENOMEM);
    return call.returnPointer(0);
}

/// A new block of `size` bytes for malloc and calloc. A size no run can allocate gives a null pointer, as in a native
/// run; any other size gets a block, or ends the run beyond the memory limit (Call::allocateHeap).
LibraryResult allocateBlock(const Call& call, UInt128 size, bool zeroed) {
    if (noRunCanAllocate(call, size)) {
        return refuseAllocation(call);
    }
    return call.returnPointer(call.allocateHeap(static_cast<std::uint64_t>(size), zeroed));
}

LibraryResult malloc(const Call& call) {
    // A block of size 0 has a unique address, as glibc's malloc(0) returns a unique pointer (C17 7.22.3p1).
    return allocateBlock(call, call.sizeArgument(0), false);
}

LibraryResult calloc(const Call& call) {
    // A product that overflows size_t also exceeds PTRDIFF_MAX: glibc returns a null pointer for both.
    return allocateBlock(call, UInt128{call.sizeArgument(0)} * call.sizeArgument(1), true);
}

LibraryResult free(const Call& call) {
    const Address address = call.pointerArgument(0);
    if (address != 0) { // free(NULL) does nothing (C17 7.22.3.3p2)
        requireHeapBlock(call, address);
        freeBlock(call, address);
    }
    return Call::returnVoid();
}

/// realloc when the old and the new block do not fit under the memory limit at the same time, as when a block grows
/// close to the limit. The kept contents and their initialization state pass through host memory, so that the old
/// block can be freed first; the evaluator makes them opaque if they depend on inputs (interp/README.md, "Terms").
Address reallocateAtLimit(const Call& call, Address old, std::uint64_t oldSize, std::uint64_t size) {
    const std::uint64_t kept = std::min(oldSize, size);
    std::vector<std::byte> bytes(kept);
    call.read(old, bytes);
    std::vector<bool> initialized(kept);
    for (std::uint64_t i = 0; i < kept; ++i) {
        initialized[i] = call.memory().isInitialized(old + i, 1);
    }
    freeBlock(call, old);
    const Address block = call.allocateHeap(size, false);
    for (std::uint64_t start = 0; start < kept;) { // write each run of initialized bytes
        std::uint64_t end = start;
        while (end < kept && initialized[end]) {
            ++end;
        }
        call.write(block + start, std::span(bytes).subspan(start, end - start));
        start = end + 1;
    }
    return block;
}

LibraryResult realloc(const Call& call) {
    const Address old = call.pointerArgument(0);
    const std::uint64_t size = call.sizeArgument(1);
    if (old == 0) { // C17 7.22.3.5p3
        return allocateBlock(call, size, false);
    }
    requireHeapBlock(call, old);
    if (size == 0) { // glibc frees the block and returns a null pointer
        freeBlock(call, old);
        return call.returnPointer(0);
    }
    if (noRunCanAllocate(call, size)) { // the old block stays as it is (C17 7.22.3.5p4)
        return refuseAllocation(call);
    }
    // The contents up to the smaller size move to a new block, with their initialization state and terms
    // (C17 7.22.3.5p2). A new block also for a smaller size, so that the old one dies and stale pointers are caught.
    const std::uint64_t oldSize = call.memory().find(old)->size;
    const std::optional<Address> block = call.tryAllocateHeap(size, false);
    if (!block) {
        return call.returnPointer(reallocateAtLimit(call, old, oldSize, size));
    }
    call.copy(*block, old, std::min(oldSize, size));
    freeBlock(call, old);
    return call.returnPointer(*block);
}

constexpr FunctionEntry functions[] = {
    {"malloc", entryPoint<malloc>},
    {"calloc", entryPoint<calloc>},
    {"realloc", entryPoint<realloc>},
    {"free", entryPoint<free>},
};

} // namespace

std::span<const FunctionEntry> heapFunctions() {
    return functions;
}

} // namespace nitwit::interp::library
