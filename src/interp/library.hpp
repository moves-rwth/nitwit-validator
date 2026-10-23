#pragma once

// The C library functions and GCC builtins that the interpreter implements itself, and the library objects programs
// refer to (stdout, ...). The evaluator calls a library function when the called function has no definition in the
// program and the library knows its name. Functions get their arguments as values; they reach memory, the output
// stream and the heap through LibraryContext, so they can be tested without the evaluator.

#include <cstdint>
#include <expected>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>

#include "interp/memory.hpp"
#include "support/target.hpp"
#include "value/value.hpp"

namespace nitwit::interp {

/// What a library function can use while it runs.
class LibraryContext {
public:
    virtual ~LibraryContext() = default;

    [[nodiscard]] virtual Memory& memory() = 0;
    [[nodiscard]] virtual const TargetInfo& target() const = 0;
    /// Where stdout output goes; null discards it. stderr output is discarded.
    [[nodiscard]] virtual std::ostream* output() = 0;
    /// Allocates heap memory and accounts for it against the memory limit (malloc and friends).
    [[nodiscard]] virtual std::expected<Address, MemoryError> allocateHeap(std::uint64_t size, bool zeroed) = 0;
    /// The address of a library object (see libraryObject), allocated on first use: in Region::ReadOnly if the object
    /// is readOnly, else in Region::Global. Its initialize function may ask for the addresses of other objects.
    [[nodiscard]] virtual Address objectAddress(std::string_view name) = 0;
};

/// A call as the evaluator passes it. Variadic arguments arrive after the default argument promotions (C17 6.5.2.2p6),
/// so `printf` sees int, unsigned, long, ..., double, long double and pointers; pointers are unsigned integers of the
/// pointer width. For vprintf, vfprintf, vsprintf, vsnprintf and vasprintf, the evaluator passes the arguments the
/// `va_list` refers to, from its current position on, in place of the `va_list`.
struct LibraryCall {
    std::string_view name;
    std::span<const value::Value> arguments;
    /// The type of each argument after the default argument promotions: Int, UnsignedInt, Long, ..., Double,
    /// LongDouble, and Pointer for pointers. One per argument; records are never passed to library functions. printf
    /// checks its arguments against the conversion specifications with them (C17 7.21.6.1p9).
    std::span<const ScalarType> argumentTypes;
};

/// How a library function ends.
struct LibraryResult {
    enum class Kind : std::uint8_t {
        Return,            // `value` is the return value (Value() for void)
        Exit,              // exit(status), _Exit(status); `status`
        Abort,             // abort(), __assert_fail()
        UndefinedBehavior, // e.g. strlen of an invalid pointer, free of a non-heap pointer; `message`
        Unsupported,       // a function or argument the interpreter does not handle (threads, files, ...); `message`
        OutOfMemory,
    };

    Kind kind = Kind::Return;
    value::Value value;
    int status = 0;
    std::string message;
    /// Whether the function wrote bytes that depend on the value of a pointer argument, not only on the bytes it points
    /// to: a pointer printed with %p, the end pointer of strtol. The evaluator then makes the written bytes opaque if a
    /// pointer argument depends on inputs (interp/README.md, "Terms").
    bool writesPointerValues = false;

    static LibraryResult returning(value::Value value) { return {Kind::Return, std::move(value), 0, {}, false}; }
};

using LibraryFunction = LibraryResult (*)(LibraryContext& context, const LibraryCall& call);

/// The implementation of a library function or builtin, or null if the library does not know `name`.
/// Not here: __VERIFIER_nondet_* and calls the oracle answers, which the evaluator handles.
[[nodiscard]] LibraryFunction findLibraryFunction(std::string_view name);

/// A data object the library provides, such as `stdout` or `errno` storage behind `__errno_location`.
struct LibraryObject {
    std::uint64_t size = 0;
    std::uint64_t alignment = 1;
    /// Read-only objects (like glibc's tables in .rodata) are allocated in Region::ReadOnly, and initialize fills them
    /// with Memory::initialize.
    bool readOnly = false;
    /// Writes the initial contents; called once when the object is allocated.
    void (*initialize)(LibraryContext& context, Address address) = nullptr;
};

/// The library object `name` (e.g. "stdout"), if the library provides it.
[[nodiscard]] std::optional<LibraryObject> libraryObject(std::string_view name, const TargetInfo& target);

} // namespace nitwit::interp
