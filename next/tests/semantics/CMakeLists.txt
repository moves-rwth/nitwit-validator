# The semantics suite (tools/difftest/README.md, "Semantics tests"): every program runs with `nitwit --replay` in both
# data models and must produce the output, outcome and input log that GCC and Clang produce. CTest label `semantics`.

find_package(Python3 REQUIRED COMPONENTS Interpreter)

file(GLOB programs CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.c)
foreach(program IN LISTS programs)
    get_filename_component(name ${program} NAME_WE)
    foreach(bits IN ITEMS 32 64)
        add_test(NAME semantics_${name}_${bits}
            COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/check_replay.py $<TARGET_FILE:nitwit> ${program}
                    ${bits})
        set_tests_properties(semantics_${name}_${bits} PROPERTIES
            LABELS semantics
            ENVIRONMENT "ASAN_OPTIONS=hard_rss_limit_mb=6000")
    endforeach()
endforeach()
