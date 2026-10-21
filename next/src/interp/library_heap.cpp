// malloc and friends (C17 7.22.3) and alloca.

#include <algorithm>
#include <format>
#include <string_view>

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
    if (block->base != address) {
        call.undefinedBehavior(
            std::format("{} of {:#x}, which points into a heap block but not to its start", call.name(), address));
    }
    if (!block->isAlive) {
        call.undefinedBehavior(std::format("{} of {:#x}, a heap block that was freed before", call.name(), address));
    }
}

void freeBlock(const Call& call, Address address) {
    const auto freed = call.memory().free(address);
    NITWIT_ASSERT(freed.has_value(), "requireHeapBlock admits only blocks that can be freed");
}

LibraryResult malloc(const Call& call) {
    // A block of size 0 has a unique address, as glibc's malloc(0) returns a unique pointer (C17 7.22.3p1).
    return call.returnPointer(call.allocateHeap(call.sizeArgument(0), false));
}

LibraryResult calloc(const Call& call) {
    const UInt128 size = UInt128{call.sizeArgument(0)} * call.sizeArgument(1);
    const UInt128 sizeMax = (UInt128{1} << call.format(call.target().sizeType()).bits) - 1;
    if (size > sizeMax) {
        // glibc returns a null pointer here; SV-COMP assumes allocation succeeds (see Call::allocateHeap).
        throw Stop{ending(LibraryResult::Kind::OutOfMemory, "calloc: the size of the block overflows size_t")};
    }
    return call.returnPointer(call.allocateHeap(static_cast<std::uint64_t>(size), true));
}

LibraryResult free(const Call& call) {
    const Address address = call.pointerArgument(0);
    if (address != 0) { // free(NULL) does nothing (C17 7.22.3.3p2)
        requireHeapBlock(call, address);
        freeBlock(call, address);
    }
    return Call::returnVoid();
}

LibraryResult realloc(const Call& call) {
    const Address old = call.pointerArgument(0);
    const std::uint64_t size = call.sizeArgument(1);
    if (old == 0) { // C17 7.22.3.5p3
        return call.returnPointer(call.allocateHeap(size, false));
    }
    requireHeapBlock(call, old);
    if (size == 0) { // glibc frees the block and returns a null pointer
        freeBlock(call, old);
        return call.returnPointer(0);
    }
    // The contents up to the smaller size move to a new block, with their initialization state and origins
    // (C17 7.22.3.5p2). A new block also for a smaller size, so that the old one dies and stale pointers are caught.
    const std::uint64_t oldSize = call.memory().find(old)->size;
    const Address block = call.allocateHeap(size, false);
    call.copy(block, old, std::min(oldSize, size));
    freeBlock(call, old);
    return call.returnPointer(block);
}

/// alloca (not named so: glibc's <stdlib.h> defines a macro alloca). The block should die when the calling function
/// returns, but the library does not see the frames, so it lives until the end of the run. It is a heap block, so
/// that the memory limit covers it.
LibraryResult allocateOnStack(const Call& call) {
    return call.returnPointer(call.allocateHeap(call.sizeArgument(0), false));
}

constexpr FunctionEntry functions[] = {
    {"malloc", entryPoint<malloc>}, {"calloc", entryPoint<calloc>},          {"realloc", entryPoint<realloc>},
    {"free", entryPoint<free>},     {"alloca", entryPoint<allocateOnStack>},
};

} // namespace

std::span<const FunctionEntry> heapFunctions() {
    return functions;
}

} // namespace nitwit::interp::library
