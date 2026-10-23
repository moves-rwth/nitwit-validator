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
#include <string_view>
#include <utility>
#include <vector>

#include "value/term.hpp"
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
    NullPointer,      // access below the lowest address any region uses (64 KiB), e.g. through a null pointer
    InvalidAddress,   // no allocation contains the address or ends exactly at it
    OutOfBounds,      // the access starts inside an allocation or exactly at its end, and ends beyond it
    DeadObject,       // a freed heap block or a gap in a range of merged ones, or a stack object whose lifetime ended
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
    std::string_view name; // the name given to allocate: the variable's name, "malloc", "string literal", ...
};

/// The term of a scalar that was written to memory, and the format it was written in (interp/README.md, "Terms").
struct StoredTerm {
    value::TermId term{};
    value::ScalarFormat format{};
};

/// What memory knows about the inputs that the bytes of a range depend on.
struct RangeTerm {
    enum class Kind : std::uint8_t {
        None,   // no input
        Stored, // the range is exactly that of a scalar written with a term, `stored`
        Opaque, // inputs, in a way that no term written for exactly this range describes
    };

    Kind kind = Kind::None;
    StoredTerm stored{}; // for Stored
};

/// The accesses of raw bytes between Memory::beginRawAccesses and endRawAccesses.
struct RawAccesses {
    bool readsInputs = false; // whether read() read bytes that depend on inputs
    /// The ranges written by write() without a term and by fill(), as address and size; adjacent ranges are merged.
    std::vector<std::pair<Address, std::uint64_t>> writes{};
};

class Memory {
public:
    /// `pointerBits` is 32 or 64. Addresses are deterministic: the same sequence of operations gives the same
    /// addresses. The first 64 KiB are never allocated, and allocations are separated by unallocated gaps so that small
    /// overruns hit no other object.
    explicit Memory(unsigned pointerBits);
    ~Memory();
    Memory(const Memory&) = delete;
    Memory& operator=(const Memory&) = delete;

    /// Allocates `size` bytes aligned to `align` (a power of two). Size 0 gives a unique address without accessible
    /// bytes. Host memory is committed lazily: untouched parts of a large allocation cost nothing. `name` must outlive
    /// the Memory (an interned symbol or a string literal); find() returns it.
    [[nodiscard]] std::expected<Address, MemoryError> allocate(Region region, std::uint64_t size, std::uint64_t align,
                                                               std::string_view name);
    /// Like allocate, but the bytes are zero and count as initialized (calloc), without touching host pages.
    [[nodiscard]] std::expected<Address, MemoryError> allocateZeroed(Region region, std::uint64_t size,
                                                                     std::uint64_t align, std::string_view name);

    /// free(): `address` must be 0 (no effect) or the base of a live heap allocation. The block stays in the table as
    /// dead, and its addresses are not reused, so later accesses are reported as DeadObject. Adjacent dead heap blocks
    /// are merged into one dead range from time to time: find() then gives the range, and the gaps between its blocks
    /// are dead too.
    [[nodiscard]] std::expected<void, MemoryError> free(Address address);
    /// The entries of the allocation table of a region: live allocations and dead ones or dead ranges, for tests of
    /// how it grows.
    [[nodiscard]] std::size_t tableSize(Region region) const;

    /// Ends the lifetime of the most recently allocated live stack allocation, which must start at `base` (stack
    /// allocations end in LIFO order). Its addresses can be reused by later stack allocations.
    void popStack(Address base);

    /// Reads bytes. Although const, it notes whether they depend on inputs while raw accesses are recorded
    /// (beginRawAccesses).
    [[nodiscard]] std::expected<void, MemoryError> read(Address address, std::span<std::byte> out) const;

    /// What readScalar tells besides the bytes.
    struct ScalarRead {
        bool isInitialized = true; // every byte of the range has been written
        RangeTerm term;            // of exactly this range
    };
    /// read, isInitialized and term in one lookup, for the loads of the evaluator.
    [[nodiscard]] std::expected<ScalarRead, MemoryError> readScalar(Address address, std::span<std::byte> out) const;

    // Terms of the bytes (interp/README.md, "Terms"): a write records the term of the scalar it writes for exactly
    // its range. Writing bytes removes the terms of other ranges that overlap them; the parts of those ranges outside
    // the written bytes still hold parts of a value that depends on inputs, so they become opaque.

    /// Writes bytes that depend on no input and marks them initialized.
    [[nodiscard]] std::expected<void, MemoryError> write(Address address, std::span<const std::byte> bytes);
    /// The same for the bytes of a scalar with the term `term`.
    [[nodiscard]] std::expected<void, MemoryError> write(Address address, std::span<const std::byte> bytes,
                                                         const StoredTerm& term);
    /// Writes into a ReadOnly allocation, to set up string literals. Precondition: a valid range.
    void initialize(Address address, std::span<const std::byte> bytes);

    /// memmove semantics; copies the bytes, their initialization state and their terms. A range with a term that lies
    /// only partly in the source becomes opaque where it is copied.
    [[nodiscard]] std::expected<void, MemoryError> copy(Address destination, Address source, std::uint64_t size);
    /// memset semantics; marks the bytes initialized. They depend on no input.
    [[nodiscard]] std::expected<void, MemoryError> fill(Address address, std::byte value, std::uint64_t size);

    /// Whether every byte of the range has been written. Global and ReadOnly bytes and those of allocateZeroed count as
    /// written, unless copy brought uninitialized bytes in. Precondition: the range is readable.
    [[nodiscard]] bool isInitialized(Address address, std::uint64_t size) const;
    /// Makes the bytes uninitialized again and removes their terms: an automatic object whose declaration is reached
    /// again holds an indeterminate value (C17 6.2.4p6). The range must be writable.
    [[nodiscard]] std::expected<void, MemoryError> markUninitialized(Address address, std::uint64_t size);

    /// The allocation that contains `address` or ends exactly at it (a one-past-the-end pointer), alive or dead. For a
    /// range of merged dead heap blocks (free), it is the whole range, from the start of its first block to the end of
    /// its last, with the gaps between them.
    [[nodiscard]] std::optional<AllocationInfo> find(Address address) const;

    /// What is known about the inputs the bytes of the range depend on.
    [[nodiscard]] RangeTerm term(Address address, std::uint64_t size) const;
    /// Marks the bytes of the range, which must lie inside one allocation, as depending on inputs in a way that no
    /// term records.
    void markOpaque(Address address, std::uint64_t size);

    /// Records the accesses of raw bytes until endRawAccesses, for an operation that moves bytes without knowing their
    /// terms, such as a library function. copy() is not recorded: it carries the terms itself.
    void beginRawAccesses();
    [[nodiscard]] RawAccesses endRawAccesses();

    /// Total size of the live Heap and Stack allocations, for the memory limit.
    [[nodiscard]] std::uint64_t liveBytes() const;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace nitwit::interp
