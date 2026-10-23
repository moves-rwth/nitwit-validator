#include "support/large_stack.hpp"

#include <pthread.h>

#include "support/assert.hpp"

namespace nitwit {

namespace {

void* runWork(void* argument) {
    (*static_cast<const std::function<void()>*>(argument))();
    return nullptr;
}

} // namespace

void runWithLargeStack(std::size_t stackBytes, const std::function<void()>& work) {
    // std::thread cannot set the stack size, so this uses POSIX threads directly.
    pthread_attr_t attributes;
    NITWIT_ASSERT(pthread_attr_init(&attributes) == 0, "cannot initialize thread attributes");
    NITWIT_ASSERT(pthread_attr_setstacksize(&attributes, stackBytes) == 0, "invalid stack size");
    pthread_t thread;
    const int created = pthread_create(&thread, &attributes, runWork, const_cast<std::function<void()>*>(&work));
    pthread_attr_destroy(&attributes);
    NITWIT_ASSERT(created == 0, "cannot create a thread with a large stack");
    NITWIT_ASSERT(pthread_join(thread, nullptr) == 0, "cannot join the thread");
}

} // namespace nitwit
