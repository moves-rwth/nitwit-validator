nitwit_add_module(interp
    SOURCES
        library.cpp
        library_call.cpp
        library_heap.cpp
        library_stdlib.cpp
        library_string.cpp
        memory.cpp
    DEPENDS nitwit_value)
