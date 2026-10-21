# Warnings for NITWIT's own targets; third-party targets keep their defaults.
function(nitwit_set_warnings target)
    target_compile_options(${target} PRIVATE
        -Wall -Wextra -Wpedantic
        -Wconversion -Wsign-conversion -Wdouble-promotion
        -Wold-style-cast -Wcast-align -Wcast-qual
        # GCC's -Wshadow also fires on scoped enumerators such as ScalarType::Int128 next to the type Int128.
        $<$<CXX_COMPILER_ID:GNU>:-Wshadow=local> $<$<CXX_COMPILER_ID:Clang>:-Wshadow>
        -Wnon-virtual-dtor -Woverloaded-virtual
        -Wformat=2 -Wimplicit-fallthrough -Wmissing-declarations
        $<$<BOOL:${NITWIT_WERROR}>:-Werror>)
endfunction()
