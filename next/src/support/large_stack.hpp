#pragma once

// Runs work on a thread with a large stack. The parser and the interpreter recurse on the host stack, and sv-benchmarks
// contains deeply nested and deeply recursive programs.

#include <cstddef>
#include <functional>

namespace nitwit {

/// Runs `work` on a new thread with `stackBytes` of stack and waits for it. Exceptions escaping `work` terminate the
/// program, as on any thread. Aborts if the thread cannot be created.
void runWithLargeStack(std::size_t stackBytes, const std::function<void()>& work);

} // namespace nitwit
