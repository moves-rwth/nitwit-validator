#include "interp/memory.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iterator>
#include <map>
#include <utility>
#include <vector>

#include "support/assert.hpp"
#include "support/int128.hpp"
#include "support/interner.hpp"

namespace nitwit::interp {

namespace {

constexpr Address pageSize = 4096;
/// Unallocated bytes between any two allocations, live or dead, so that small overruns hit no other object and a
/// one-past-the-end address never lies inside another allocation.
constexpr std::uint64_t gap = 16;
constexpr std::size_t regionCount = 5;

std::size_t indexOf(Region region) {
    return static_cast<std::size_t>(region);
}

struct Range {
    Address begin = 0;
    Address end = 0;
};

/// The part of the address space of each region, indexed by Region. Nothing lies below the code region, so the null
/// page and the pages above it stay unmapped.
std::array<Range, regionCount> layout(unsigned pointerBits) {
    std::array<Range, regionCount> ranges{};
    if (pointerBits == 32) {
        ranges[indexOf(Region::Code)] = {.begin = 0x0001'0000, .end = 0x0100'0000};
        ranges[indexOf(Region::ReadOnly)] = {.begin = 0x0100'0000, .end = 0x0800'0000};
        ranges[indexOf(Region::Global)] = {.begin = 0x0800'0000, .end = 0x6000'0000};
        ranges[indexOf(Region::Heap)] = {.begin = 0x6000'0000, .end = 0xC000'0000};
        ranges[indexOf(Region::Stack)] = {.begin = 0xC000'0000, .end = 0xFFFF'0000};
    } else {
        constexpr Address tebibyte = Address{1} << 40;
        ranges[indexOf(Region::Code)] = {.begin = 0x0001'0000, .end = tebibyte};
        ranges[indexOf(Region::ReadOnly)] = {.begin = tebibyte, .end = 2 * tebibyte};
        ranges[indexOf(Region::Global)] = {.begin = 2 * tebibyte, .end = 3 * tebibyte};
        ranges[indexOf(Region::Heap)] = {.begin = 3 * tebibyte, .end = 4 * tebibyte};
        ranges[indexOf(Region::Stack)] = {.begin = 4 * tebibyte, .end = 5 * tebibyte};
    }
    return ranges;
}

struct FreeDeleter {
    void operator()(void* memory) const { std::free(memory); }
};

template <typename T>
using HostArray = std::unique_ptr<T[], FreeDeleter>;

/// Zeroed host memory. glibc serves large blocks with fresh mmap pages, which are committed only when written.
template <typename T>
HostArray<T> allocateZeroed(std::uint64_t count) {
    return HostArray<T>(static_cast<T*>(std::calloc(count, sizeof(T))));
}

// Initialization bitmaps: bit i % 8 of byte i / 8 tells whether byte i of the allocation has been written.

std::uint8_t bitMask(std::uint64_t index) {
    return static_cast<std::uint8_t>(1u << (index % 8));
}

bool testBit(const std::uint8_t* bitmap, std::uint64_t index) {
    return (bitmap[index / 8] & bitMask(index)) != 0;
}

void assignBit(std::uint8_t* bitmap, std::uint64_t index, bool value) {
    if (value) {
        bitmap[index / 8] |= bitMask(index);
    } else {
        bitmap[index / 8] &= static_cast<std::uint8_t>(~bitMask(index));
    }
}

void setBits(std::uint8_t* bitmap, std::uint64_t first, std::uint64_t count) {
    const std::uint64_t end = first + count;
    std::uint64_t index = first;
    for (; index < end && index % 8 != 0; ++index) {
        bitmap[index / 8] |= bitMask(index);
    }
    const std::uint64_t wholeBytes = (end - index) / 8;
    std::memset(bitmap + index / 8, 0xFF, wholeBytes);
    for (index += wholeBytes * 8; index < end; ++index) {
        bitmap[index / 8] |= bitMask(index);
    }
}

bool allBitsSet(const std::uint8_t* bitmap, std::uint64_t first, std::uint64_t count) {
    const std::uint64_t end = first + count;
    std::uint64_t index = first;
    for (; index < end && index % 8 != 0; ++index) {
        if (!testBit(bitmap, index)) {
            return false;
        }
    }
    const std::uint64_t wholeBytes = (end - index) / 8;
    if (!std::all_of(bitmap + index / 8, bitmap + index / 8 + wholeBytes, [](std::uint8_t b) { return b == 0xFF; })) {
        return false;
    }
    for (index += wholeBytes * 8; index < end; ++index) {
        if (!testBit(bitmap, index)) {
            return false;
        }
    }
    return true;
}

struct Allocation {
    Address base = 0;
    std::uint64_t size = 0;
    Region region = Region::Global;
    bool isAlive = true;
    Symbol name;
    HostArray<std::byte> bytes;          // null for code, size 0 and dead allocations
    HostArray<std::uint8_t> initialized; // only for Stack and Heap; elsewhere every byte counts as initialized

    [[nodiscard]] Address end() const { return base + size; }
    [[nodiscard]] std::byte* at(Address address) const { return bytes.get() + (address - base); }
};

enum class Access : std::uint8_t { Read, Write };

/// The allocation in `allocations` (sorted by ascending base) that contains `address` or ends exactly at it.
Allocation* searchAscending(std::vector<Allocation>& allocations, Address address) {
    auto next = std::ranges::upper_bound(allocations, address, {}, &Allocation::base);
    if (next == allocations.begin()) {
        return nullptr;
    }
    Allocation& candidate = *std::prev(next);
    return address <= candidate.end() ? &candidate : nullptr;
}

/// The same for allocations sorted by descending base.
Allocation* searchDescending(std::vector<Allocation>& allocations, Address address) {
    auto candidate = std::ranges::lower_bound(allocations, address, std::ranges::greater{}, &Allocation::base);
    return candidate != allocations.end() && address <= candidate->end() ? &*candidate : nullptr;
}

} // namespace

struct Memory::State {
    explicit State(unsigned pointerBits) : ranges(layout(pointerBits)) {
        NITWIT_ASSERT(pointerBits == 32 || pointerBits == 64, "pointers have 32 or 64 bits");
        for (std::size_t i = 0; i < regionCount; ++i) {
            next[i] = ranges[i].begin;
        }
    }

    std::array<Range, regionCount> ranges;
    // Code, ReadOnly, Global and Heap grow upward: their allocations sorted by base, and the next free address.
    std::array<std::vector<Allocation>, regionCount> upward;
    std::array<Address, regionCount> next{};
    // The stack grows downward. Live allocations are sorted by descending base, so the most recent is at the back.
    // Dead ones lie below all live ones and are sorted by ascending base, so the most recently ended is at the back.
    // They are dropped when a new allocation reuses their addresses.
    std::vector<Allocation> liveStack;
    std::vector<Allocation> deadStack;

    struct Origin {
        std::uint64_t size = 0;
        value::InputId input{};
    };
    std::map<Address, Origin> origins; // non-overlapping ranges, by start address

    StringInterner names;
    std::uint64_t liveBytes = 0;
    // The allocation found last; most accesses hit the same object as the previous one. It points into the vectors
    // above, so changes to them reset it. The const functions of Memory update it.
    Allocation* lastFound = nullptr;

    [[nodiscard]] std::optional<Region> regionOf(Address address) const {
        for (std::size_t i = 0; i < regionCount; ++i) {
            if (address >= ranges[i].begin && address < ranges[i].end) {
                return static_cast<Region>(i);
            }
        }
        return std::nullopt;
    }

    /// The allocation that contains `address` or ends exactly at it, alive or dead.
    Allocation* lookup(Address address) {
        if (lastFound != nullptr && lastFound->base <= address && address <= lastFound->end()) {
            return lastFound;
        }
        const std::optional<Region> region = regionOf(address);
        if (!region) {
            return nullptr;
        }
        Allocation* found = nullptr;
        if (*region != Region::Stack) {
            found = searchAscending(upward[indexOf(*region)], address);
        } else if (!liveStack.empty() && address >= liveStack.back().base) {
            found = searchDescending(liveStack, address);
        } else {
            found = searchAscending(deadStack, address);
        }
        if (found != nullptr) {
            lastFound = found;
        }
        return found;
    }

    /// Checks an access of `size` > 0 bytes and returns the allocation it is in.
    std::expected<Allocation*, MemoryError> access(Address address, std::uint64_t size, Access kind) {
        const auto error = [&](MemoryErrorKind errorKind) {
            return std::unexpected(MemoryError{.kind = errorKind, .address = address, .size = size});
        };
        if (address < pageSize) {
            return error(MemoryErrorKind::NullPointer);
        }
        Allocation* allocation = lookup(address);
        if (allocation == nullptr || address == allocation->end()) {
            return error(MemoryErrorKind::InvalidAddress);
        }
        if (!allocation->isAlive) {
            return error(MemoryErrorKind::DeadObject);
        }
        if (allocation->region == Region::Code) {
            return error(MemoryErrorKind::CodeAccess);
        }
        if (size > allocation->end() - address) {
            return error(MemoryErrorKind::OutOfBounds);
        }
        if (kind == Access::Write && allocation->region == Region::ReadOnly) {
            return error(MemoryErrorKind::ReadOnlyWrite);
        }
        return allocation;
    }

    /// The base for a new allocation, or nothing if the region is exhausted.
    [[nodiscard]] std::optional<Address> placement(Region region, std::uint64_t size, std::uint64_t align) const {
        const Range range = ranges[indexOf(region)];
        // UInt128 keeps the arithmetic free of overflow for any size and alignment.
        const UInt128 alignMask = ~UInt128{align - 1};
        if (region == Region::Stack) {
            const Address top = liveStack.empty() ? range.end : liveStack.back().base;
            if (UInt128{range.begin} + size + gap > top) {
                return std::nullopt;
            }
            const UInt128 base = (UInt128{top} - gap - size) & alignMask;
            return base >= range.begin ? std::optional(static_cast<Address>(base)) : std::nullopt;
        }
        const UInt128 base = (UInt128{next[indexOf(region)]} + align - 1) & alignMask;
        return base + size + gap <= range.end ? std::optional(static_cast<Address>(base)) : std::nullopt;
    }

    void add(Allocation allocation) {
        lastFound = nullptr;
        if (allocation.region == Region::Stack || allocation.region == Region::Heap) {
            liveBytes += allocation.size;
        }
        if (allocation.region != Region::Stack) {
            next[indexOf(allocation.region)] = allocation.end() + gap;
            upward[indexOf(allocation.region)].push_back(std::move(allocation));
            return;
        }
        while (!deadStack.empty() && deadStack.back().end() + gap > allocation.base) {
            deadStack.pop_back();
        }
        liveStack.push_back(std::move(allocation));
    }

    /// Ends the lifetime of a Heap or Stack allocation; its bookkeeping stays for error reports.
    void kill(Allocation& allocation) {
        allocation.isAlive = false;
        allocation.bytes.reset();
        allocation.initialized.reset();
        dropOrigins(allocation.base, allocation.size);
        liveBytes -= allocation.size;
    }

    void markInitialized(const Allocation& allocation, Address address, std::uint64_t size) {
        if (allocation.initialized) {
            setBits(allocation.initialized.get(), address - allocation.base, size);
        }
    }

    /// Copies the initialization state of `size` bytes; the ranges may overlap within one allocation (memmove).
    static void copyInitialization(const Allocation& to, Address destination, const Allocation& from, Address source,
                                   std::uint64_t size) {
        if (!to.initialized) {
            return;
        }
        const std::uint64_t toOffset = destination - to.base;
        if (!from.initialized) {
            setBits(to.initialized.get(), toOffset, size);
            return;
        }
        const std::uint64_t fromOffset = source - from.base;
        const auto copyBit = [&](std::uint64_t i) {
            assignBit(to.initialized.get(), toOffset + i, testBit(from.initialized.get(), fromOffset + i));
        };
        if (destination > source) {
            for (std::uint64_t i = size; i-- > 0;) {
                copyBit(i);
            }
        } else {
            for (std::uint64_t i = 0; i < size; ++i) {
                copyBit(i);
            }
        }
    }

    void dropOrigins(Address address, std::uint64_t size) {
        if (origins.empty() || size == 0) {
            return;
        }
        auto first = origins.lower_bound(address);
        if (first != origins.begin()) {
            const auto previous = std::prev(first);
            if (previous->first + previous->second.size > address) {
                first = previous;
            }
        }
        origins.erase(first, origins.lower_bound(address + size));
    }

    /// Copies the origins that lie entirely inside the source range, replacing those of the destination range.
    void copyOrigins(Address destination, Address source, std::uint64_t size) {
        if (origins.empty()) {
            return;
        }
        std::vector<std::pair<Address, Origin>> copied;
        for (auto it = origins.lower_bound(source); it != origins.end() && it->first + it->second.size <= source + size;
             ++it) {
            copied.emplace_back(it->first - source + destination, it->second);
        }
        dropOrigins(destination, size);
        origins.insert(copied.begin(), copied.end());
    }
};

Memory::Memory(unsigned pointerBits) : state_(std::make_unique<State>(pointerBits)) {}

Memory::~Memory() = default;

std::expected<Address, MemoryError> Memory::allocate(Region region, std::uint64_t size, std::uint64_t align,
                                                     std::string name) {
    NITWIT_ASSERT(std::has_single_bit(align), "alignment must be a power of two");
    const MemoryError exhausted{.kind = MemoryErrorKind::OutOfAddressSpace, .address = 0, .size = size};
    const std::optional<Address> base = state_->placement(region, size, align);
    if (!base) {
        return std::unexpected(exhausted);
    }
    HostArray<std::byte> bytes;
    HostArray<std::uint8_t> initialized;
    if (region != Region::Code && size > 0) {
        const bool tracksInitialization = region == Region::Stack || region == Region::Heap;
        bytes = allocateZeroed<std::byte>(size);
        if (tracksInitialization) {
            initialized = allocateZeroed<std::uint8_t>((size + 7) / 8);
        }
        // The host cannot back the allocation; for the program, that is the same as an exhausted address space.
        if (!bytes || (tracksInitialization && !initialized)) {
            return std::unexpected(exhausted);
        }
    }
    state_->add(Allocation{.base = *base,
                           .size = size,
                           .region = region,
                           .isAlive = true,
                           .name = state_->names.intern(name),
                           .bytes = std::move(bytes),
                           .initialized = std::move(initialized)});
    return *base;
}

std::expected<void, MemoryError> Memory::free(Address address) {
    if (address == 0) {
        return {};
    }
    Allocation* allocation = state_->lookup(address);
    if (allocation == nullptr || allocation->base != address || allocation->region != Region::Heap
        || !allocation->isAlive) {
        return std::unexpected(MemoryError{.kind = MemoryErrorKind::InvalidFree, .address = address, .size = 0});
    }
    state_->kill(*allocation);
    return {};
}

void Memory::popStack(Address base) {
    State& state = *state_;
    NITWIT_ASSERT(!state.liveStack.empty() && state.liveStack.back().base == base,
                  "stack allocations must end in LIFO order");
    state.kill(state.liveStack.back());
    state.deadStack.push_back(std::move(state.liveStack.back()));
    state.liveStack.pop_back();
    state.lastFound = nullptr;
}

// Accesses of zero bytes touch no memory and always succeed.

std::expected<void, MemoryError> Memory::read(Address address, std::span<std::byte> out) const {
    if (out.empty()) {
        return {};
    }
    return state_->access(address, out.size(), Access::Read).transform([&](const Allocation* allocation) {
        std::memcpy(out.data(), allocation->at(address), out.size());
    });
}

std::expected<void, MemoryError> Memory::write(Address address, std::span<const std::byte> bytes) {
    if (bytes.empty()) {
        return {};
    }
    return state_->access(address, bytes.size(), Access::Write).transform([&](const Allocation* allocation) {
        std::memcpy(allocation->at(address), bytes.data(), bytes.size());
        state_->markInitialized(*allocation, address, bytes.size());
        state_->dropOrigins(address, bytes.size());
    });
}

void Memory::initialize(Address address, std::span<const std::byte> bytes) {
    if (bytes.empty()) {
        return;
    }
    const Allocation* allocation = state_->lookup(address);
    NITWIT_ASSERT(allocation != nullptr && allocation->region == Region::ReadOnly && address < allocation->end()
                      && bytes.size() <= allocation->end() - address,
                  "initialize needs a range inside a ReadOnly allocation");
    std::memcpy(allocation->at(address), bytes.data(), bytes.size());
}

std::expected<void, MemoryError> Memory::copy(Address destination, Address source, std::uint64_t size) {
    if (size == 0) {
        return {};
    }
    const auto from = state_->access(source, size, Access::Read);
    if (!from) {
        return std::unexpected(from.error());
    }
    const auto to = state_->access(destination, size, Access::Write);
    if (!to) {
        return std::unexpected(to.error());
    }
    std::memmove((*to)->at(destination), (*from)->at(source), size);
    State::copyInitialization(**to, destination, **from, source, size);
    state_->copyOrigins(destination, source, size);
    return {};
}

std::expected<void, MemoryError> Memory::fill(Address address, std::byte value, std::uint64_t size) {
    if (size == 0) {
        return {};
    }
    return state_->access(address, size, Access::Write).transform([&](const Allocation* allocation) {
        std::memset(allocation->at(address), std::to_integer<int>(value), size);
        state_->markInitialized(*allocation, address, size);
        state_->dropOrigins(address, size);
    });
}

bool Memory::isInitialized(Address address, std::uint64_t size) const {
    if (size == 0) {
        return true;
    }
    const auto allocation = state_->access(address, size, Access::Read);
    NITWIT_ASSERT(allocation.has_value(), "isInitialized needs a readable range");
    const HostArray<std::uint8_t>& bitmap = (*allocation)->initialized;
    return !bitmap || allBitsSet(bitmap.get(), address - (*allocation)->base, size);
}

std::optional<AllocationInfo> Memory::find(Address address) const {
    const Allocation* allocation = state_->lookup(address);
    if (allocation == nullptr) {
        return std::nullopt;
    }
    return AllocationInfo{.base = allocation->base,
                          .size = allocation->size,
                          .region = allocation->region,
                          .isAlive = allocation->isAlive,
                          .name = allocation->name.text()};
}

void Memory::setOrigin(Address address, std::uint64_t size, value::InputId input) {
    NITWIT_ASSERT(size > 0, "an origin needs at least one byte");
    state_->dropOrigins(address, size);
    state_->origins.emplace(address, State::Origin{.size = size, .input = input});
}

std::optional<value::InputId> Memory::origin(Address address, std::uint64_t size) const {
    const auto found = state_->origins.find(address);
    if (found == state_->origins.end() || found->second.size != size) {
        return std::nullopt;
    }
    return found->second.input;
}

std::uint64_t Memory::liveBytes() const {
    return state_->liveBytes;
}

} // namespace nitwit::interp
