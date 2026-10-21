nitwit_add_module(interp
    SOURCES
        library.cpp
        library_builtins.cpp
        library_call.cpp
        library_ctype.cpp
        library_format.cpp
        library_heap.cpp
        library_math.cpp
        library_stdio.cpp
        library_stdlib.cpp
        library_string.cpp
        memory.cpp
    DEPENDS nitwit_value m) # m: the host libm computes the math functions

# The math functions must call glibc's libm, as natively compiled programs do (tools/difftest compiles them with
# -fno-builtin). The compiler's inline expansions can differ, e.g. for fmin(-0.0, 0.0).
set_source_files_properties(library_math.cpp PROPERTIES COMPILE_OPTIONS -fno-builtin)
