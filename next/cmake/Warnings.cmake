# Warnings for NITWIT's own targets; third-party targets keep their defaults.
function(nitwit_set_warnings target)
    target_compile_options(${target} PRIVATE
        -Wall -Wextra -Wpedantic
        -Wconversion -Wsign-conversion -Wdouble-promotion
        -Wshadow -Wold-style-cast -Wcast-align -Wcast-qual
        -Wnon-virtual-dtor -Woverloaded-virtual
        -Wformat=2 -Wimplicit-fallthrough -Wmissing-declarations
        $<$<BOOL:${NITWIT_WERROR}>:-Werror>)
endfunction()
