nitwit_add_unit_test(interp
    SOURCES
        evaluation_context_test.cpp
        evaluator_test.cpp
        inputs_test.cpp
        library_builtins_test.cpp
        library_ctype_test.cpp
        library_dispatch_test.cpp
        library_heap_test.cpp
        library_math_test.cpp
        library_printf_test.cpp
        library_stdlib_test.cpp
        library_string_test.cpp
        lowering_test.cpp
        memory_test.cpp
        termination_test.cpp
        terms_test.cpp
    DEPENDS nitwit_interp nitwit_parse nitwit_sema) # sema: its table of builtins, for library_dispatch_test.cpp
