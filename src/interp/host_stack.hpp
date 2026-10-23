#pragma once

// Internal to the interpreter: how much of the host thread's stack is left. Calls and expressions of the program
// recurse on the host stack, so a deeply recursive program could overflow it before RunOptions::maxCallDepth is
// reached; the evaluator checks this and ends the run with a limit instead.

#include <cstddef>
#include <cstdint>

namespace nitwit::interp {

class HostStack {
public:
    /// The stack of the calling thread, of which `reserve` bytes at the end stay unused (for the library and the
    /// unwinding).
    explicit HostStack(std::size_t reserve);

    /// Whether the frame of the caller lies within the reserve.
    [[nodiscard]] bool isExhausted() const {
        // The frame address, not the address of a local: with AddressSanitizer, locals may live on a fake stack.
        // GCC and Clang both provide the builtin.
        return reinterpret_cast<std::uintptr_t>(__builtin_frame_address(0)) < limit_;
    }

private:
    std::uintptr_t limit_ = 0; // the lowest frame address allowed; the stack grows down on x86
};

} // namespace nitwit::interp
