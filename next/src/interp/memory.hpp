#pragma once

// The interpreter's memory: a flat virtual address space of 32 or 64 bits in which every object is an allocation.
// Pointers are plain addresses; every access is checked against the allocation table. This makes pointer/integer
// casts, unions and memcpy of structs that contain pointers work, and finds out-of-bounds and use-after-free accesses.

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "value/value.hpp"

namespace nitwit::interp {

using Address = std::uint64_t;

enum class Region : std::uint8_t {
    Code,     // one byte per function, so that function pointers are addresses; never readable or writable
    ReadOnly, // string literals
    Global,   // static storage duration; zero-initialized
    Stack,    // automatic storage duration; uninitialized until written
    Heap,     // malloc, calloc, realloc; uninitialized until written
};

enum class MemoryErrorKind : std::uint8_t {
    NullPointer,      // access within the first page
    InvalidAddress,   // no allocation at the address
    OutOfBounds,      // the access starts inside an allocation but ends beyond it
    DeadObject,       // a freed heap block, or a stack object whose lifetime ended
    ReadOnlyWrite,    // write to a ReadOnly allocation
    CodeAccess,       // read or write of a function
    InvalidFree,      // free of an address that is not the base of a live heap block
    OutOfAddressSpace // the region of the address space is exhausted
};

struct MemoryError {
    MemoryErrorKind kind = MemoryErrorKind::InvalidAddress;
    Address address = 0;
    std::uint64_t size = 0;
};

struct AllocationInfo {
    Address base = 0;
    std::uint64_t size = 0;
    Region region = Region::Global;
    bool isAlive = true;
    std::string_view name; // the variable's name, "malloc", "string literal", ...
};

class Memory {
public:
    /// `pointerBits` is 32 or 64. Addresses are deterministic: the same sequence of operations gives the same
    /// addresses. The first page is never allocated, and allocations are separated by unallocated gaps so that small
    /// overruns hit no other object.
    explicit Memory(unsigned pointerBits);
    ~Memory();
    Memory(const Memory&) = delete;
    Memory& operator=(const Memory&) = delete;

    /// Allocates `size` bytes aligned to `align` (a power of two). Size 0 gives a unique address without accessible
    /// bytes. Host memory is committed lazily: untouched parts of a large allocation cost nothing.
    [[nodiscard]] std::expected<Address, MemoryError> allocate(Region region, std::uint64_t size, std::uint64_t align,
                                                               std::string name);

    /// free(): `address` must be 0 (no effect) or the base of a live heap allocation. The block stays in the table as
    /// dead, and its addresses are not reused, so later accesses are reported as DeadObject.
    [[nodiscard]] std::expected<void, MemoryError> free(Address address);

    /// Ends the lifetime of the most recently allocated live stack allocation, which must start at `base` (stack
    /// allocations end in LIFO order). Its addresses can be reused by later stack allocations.
    void popStack(Address base);

    [[nodiscard]] std::expected<void, MemoryError> read(Address address, std::span<std::byte> out) const;
    /// Writes bytes, marks them initialized, and removes the origins of the overwritten bytes.
    [[nodiscard]] std::expected<void, MemoryError> write(Address address, std::span<const std::byte> bytes);
    /// Writes into a ReadOnly allocation, to set up string literals. Precondition: a valid range.
    void initialize(Address address, std::span<const std::byte> bytes);

    /// memmove semantics; copies the bytes, their initialization state and the origins inside the range.
    [[nodiscard]] std::expected<void, MemoryError> copy(Address destination, Address source, std::uint64_t size);
    [[nodiscard]] std::expected<void, MemoryError> fill(Address address, std::byte value, std::uint64_t size);

    /// Whether every byte of the range has been written (Global and ReadOnly bytes always are). Precondition: the
    /// range is readable.
    [[nodiscard]] bool isInitialized(Address address, std::uint64_t size) const;

    /// The allocation that contains `address` or ends exactly at it (a one-past-the-end pointer), alive or dead.
    [[nodiscard]] std::optional<AllocationInfo> find(Address address) const;

    /// Marks the range as an unmodified copy of `input`; read back with origin().
    void setOrigin(Address address, std::uint64_t size, value::InputId input);
    /// The origin of exactly this range, if one was set for exactly this range and not overwritten since.
    [[nodiscard]] std::optional<value::InputId> origin(Address address, std::uint64_t size) const;

    /// Total size of the live Heap and Stack allocations, for the memory limit.
    [[nodiscard]] std::uint64_t liveBytes() const;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace nitwit::interp
