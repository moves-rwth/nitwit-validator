nitwit_add_module(support
    SOURCES
        assert.cpp
        diagnostics.cpp
        interner.cpp
        large_stack.cpp
        source_manager.cpp
        target.cpp)

find_package(Threads REQUIRED)
target_link_libraries(nitwit_support PUBLIC Threads::Threads)
