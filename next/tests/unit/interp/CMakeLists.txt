nitwit_add_unit_test(interp
    SOURCES
        library_builtins_test.cpp
        library_ctype_test.cpp
        library_dispatch_test.cpp
        library_heap_test.cpp
        library_math_test.cpp
        library_printf_test.cpp
        library_stdlib_test.cpp
        library_string_test.cpp
        memory_test.cpp
    DEPENDS nitwit_interp)
