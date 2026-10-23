nitwit_add_unit_test(sema
    SOURCES
        builtins_test.cpp
        constant_test.cpp
        constraint_test.cpp
        conversions_test.cpp
        declarations_test.cpp
        expressions_test.cpp
        initializers_test.cpp
        layout_test.cpp
        statements_test.cpp
        types_test.cpp
        zoo_test.cpp
    DEPENDS nitwit_parse nitwit_sema)

# GCC checks the zoo's assertions too (zoo_test.cpp checks sema's), for each data model it can compile for.
find_program(NITWIT_GCC14 gcc-14)
if(NITWIT_GCC14)
    set(zoo ${CMAKE_CURRENT_SOURCE_DIR}/data)
    foreach(model IN ITEMS 32 64)
        execute_process(COMMAND ${NITWIT_GCC14} -m${model} -fsyntax-only -x c /dev/null
            RESULT_VARIABLE unsupported OUTPUT_QUIET ERROR_QUIET)
        if(unsupported)
            message(STATUS "gcc-14 cannot compile with -m${model}; the zoo's gcc test for it is skipped")
            continue()
        endif()
        if(model EQUAL 32)
            set(specific zoo_ilp32)
        else()
            set(specific zoo_lp64)
        endif()
        foreach(name IN ITEMS zoo_common ${specific})
            add_test(NAME gcc_${name}_m${model}
                COMMAND ${NITWIT_GCC14} -m${model} -std=gnu17 -fsyntax-only ${zoo}/${name}.c)
            set_tests_properties(gcc_${name}_m${model} PROPERTIES LABELS unit)
        endforeach()
    endforeach()
endif()
