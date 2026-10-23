#include "interp/host_stack.hpp"

#include <pthread.h>

namespace nitwit::interp {

HostStack::HostStack(std::size_t reserve) {
    // glibc reports the bounds of the main thread's stack (from its resource limit) and of created threads.
    pthread_attr_t attributes;
    if (pthread_getattr_np(pthread_self(), &attributes) != 0) {
        return; // unknown bounds: never exhausted
    }
    void* lowest = nullptr;
    std::size_t size = 0;
    if (pthread_attr_getstack(&attributes, &lowest, &size) == 0 && size > reserve) {
        limit_ = reinterpret_cast<std::uintptr_t>(lowest) + reserve;
    }
    pthread_attr_destroy(&attributes);
}

} // namespace nitwit::interp
