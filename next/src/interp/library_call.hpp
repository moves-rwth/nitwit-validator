#pragma once

// Internal to the library implementation (library*.cpp): typed access to a call's arguments and results in the
// target's formats, checked memory access that turns memory errors into undefined behaviour, and the tables that map
// names to functions and objects.

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "interp/library.hpp"
#include "support/int128.hpp"

namespace nitwit::interp::library {

/// Ends a library function early with a result other than a return: undefined behaviour, an unsupported argument,
/// exhausted memory. Call throws it and entryPoint catches it, so that the functions read as straight-line code and
/// no memory access needs error handling of its own.
struct Stop {
    LibraryResult result;
};

[[nodiscard]] LibraryResult ending(LibraryResult::Kind kind, std::string message);

/// One call of a library function.
class Call {
public:
    Call(LibraryContext& context, const LibraryCall& call) : context_(context), call_(call) {}

    [[nodiscard]] std::string_view name() const { return call_.name; }
    [[nodiscard]] LibraryContext& context() const { return context_; }
    [[nodiscard]] Memory& memory() const { return context_.memory(); }
    [[nodiscard]] const TargetInfo& target() const { return context_.target(); }
    [[nodiscard]] value::IntegerFormat format(ScalarType type) const;

    // ---- Arguments; a missing argument or one of the wrong kind is undefined behaviour (C17 6.5.2.2p6) ----

    [[nodiscard]] std::size_t argumentCount() const { return call_.arguments.size(); }
    [[nodiscard]] const value::Value& argument(std::size_t index) const;
    /// An integer or pointer argument converted to `type`, as the callee sees its parameter: canonical bits,
    /// sign-extended if `type` is signed.
    [[nodiscard]] UInt128 integerArgument(std::size_t index, ScalarType type) const;
    [[nodiscard]] Int128 signedArgument(std::size_t index, ScalarType type) const;
    [[nodiscard]] int intArgument(std::size_t index) const;
    [[nodiscard]] std::uint64_t sizeArgument(std::size_t index) const;
    [[nodiscard]] Address pointerArgument(std::size_t index) const;
    /// A floating argument converted to `format`, as a prototype would convert it (C17 6.5.2.2p7).
    [[nodiscard]] value::Value floatArgument(std::size_t index, value::FloatFormat format) const;

    // ---- Results ----

    [[nodiscard]] LibraryResult returnInteger(UInt128 bits, ScalarType type) const;
    [[nodiscard]] LibraryResult returnInt(int value) const;
    [[nodiscard]] LibraryResult returnSize(std::uint64_t value) const;
    [[nodiscard]] LibraryResult returnPointer(Address address) const;
    [[nodiscard]] static LibraryResult returnVoid() { return LibraryResult::returning(value::Value()); }

    // ---- Ending the call otherwise ----

    [[noreturn]] void undefinedBehavior(std::string message) const;
    [[noreturn]] void unsupported(std::string message) const;

    // ---- Memory ----

    void read(Address address, std::span<std::byte> out) const;
    /// The `size` bytes at `address`; checks the range before it allocates host memory for them.
    [[nodiscard]] std::vector<std::byte> readBytes(Address address, std::uint64_t size) const;
    void write(Address address, std::span<const std::byte> bytes) const;
    /// memmove semantics, with initialization state and origins (Memory::copy).
    void copy(Address destination, Address source, std::uint64_t size) const;
    void fill(Address address, std::byte value, std::uint64_t size) const;
    [[nodiscard]] UInt128 readInteger(Address address, ScalarType type) const;
    void writeInteger(Address address, UInt128 bits, ScalarType type) const;
    /// The string at `address` without its terminating null character, or its first `limit` bytes if it is longer.
    /// Reads no byte beyond the terminator or the limit.
    [[nodiscard]] std::string readString(Address address,
                                         std::uint64_t limit = std::numeric_limits<std::uint64_t>::max()) const;
    /// Undefined behaviour if the two ranges overlap, for the functions whose parameters are `restrict`
    /// (C17 7.24.1p2 and the description of each function).
    void requireDisjoint(Address first, std::uint64_t firstSize, Address second, std::uint64_t secondSize) const;
    /// A new heap block; ends the run with OutOfMemory if the context refuses it. SV-COMP assumes that allocation
    /// never fails, so returning a null pointer instead would explore executions the verifiers do not consider.
    [[nodiscard]] Address allocateHeap(std::uint64_t size, bool zeroed) const;
    [[nodiscard]] Address objectAddress(std::string_view object) const { return context_.objectAddress(object); }
    /// Stores `value` in errno (the library object behind __errno_location).
    void setErrno(int value) const;

private:
    [[noreturn]] void memoryError(bool isWrite, const MemoryError& error) const;

    LibraryContext& context_;
    const LibraryCall& call_;
};

/// Reads the bytes of an object one after the other, fetching them in blocks that do not extend beyond the object.
/// So a function reads exactly as far as C lets it (strcmp up to the first difference, memchr up to the first match).
class ByteReader {
public:
    ByteReader(const Call& call, Address address) : call_(call), position_(address) {}

    /// The next byte; undefined behaviour if it cannot be read.
    [[nodiscard]] std::byte next();
    /// The address of the byte that next() returns.
    [[nodiscard]] Address position() const { return position_; }

private:
    static constexpr std::size_t blockSize = 256;

    const Call& call_;
    Address position_;
    std::array<std::byte, blockSize> block_{};
    std::size_t index_ = 0; // index of the byte at position_
    std::size_t filled_ = 0;
};

using Implementation = LibraryResult (*)(const Call& call);

/// The implementation of functions without effect in the interpreter (__VERIFIER_atomic_begin, ...).
LibraryResult doNothing(const Call& call);

/// The LibraryFunction for an implementation: catches Stop, so that no exception leaves the library.
template <Implementation implementation>
LibraryResult entryPoint(LibraryContext& context, const LibraryCall& libraryCall) {
    const Call call(context, libraryCall);
    try {
        return implementation(call);
    } catch (Stop& stop) {
        return std::move(stop.result);
    }
}

struct FunctionEntry {
    std::string_view name;
    LibraryFunction function;
};

struct ObjectEntry {
    std::string_view name;
    LibraryObject (*describe)(const TargetInfo& target);
};

/// Writes the initial contents of a library object (LibraryObject::initialize). The context allocates library objects
/// in writable memory.
void initializeObject(LibraryContext& context, Address address, std::span<const std::byte> bytes);
/// A LibraryObject::initialize for objects that start with zero bytes.
void initializeZero(LibraryContext& context, Address address);
/// Initializes a library object of pointer type to `value`.
void initializePointer(LibraryContext& context, Address address, Address value);

// The parts of the library, one per file.
[[nodiscard]] std::span<const FunctionEntry> heapFunctions();    // library_heap.cpp
[[nodiscard]] std::span<const FunctionEntry> stringFunctions();  // library_string.cpp
[[nodiscard]] std::span<const FunctionEntry> stdlibFunctions();  // library_stdlib.cpp
[[nodiscard]] std::span<const ObjectEntry> stdlibObjects();      // library_stdlib.cpp
[[nodiscard]] std::span<const FunctionEntry> stdioFunctions();   // library_stdio.cpp
[[nodiscard]] std::span<const ObjectEntry> stdioObjects();       // library_stdio.cpp
[[nodiscard]] std::span<const FunctionEntry> ctypeFunctions();   // library_ctype.cpp
[[nodiscard]] std::span<const ObjectEntry> ctypeObjects();       // library_ctype.cpp
[[nodiscard]] std::span<const FunctionEntry> mathFunctions();    // library_math.cpp
[[nodiscard]] std::span<const FunctionEntry> builtinFunctions(); // library_builtins.cpp

} // namespace nitwit::interp::library
