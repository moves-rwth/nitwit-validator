# Golden tests: a tool's output for an input file must equal an expected file next to it.
#
# nitwit_add_golden_tests(<module> <tool-target> <extension> <tool-args>...)
# Registers one CTest with the label "golden" per tests/golden/<module>/<name>.c. The test runs
# `<tool> <tool-args>... <name>.c` and compares its stdout with <name>.<extension>. With the environment variable
# NITWIT_UPDATE_GOLDEN=1, the test writes the output to <name>.<extension> instead; review the changes before
# committing them.
#
# The test itself runs this file as a script (cmake -P) with TOOL, ARGS, INPUT and EXPECTED defined.

if(NOT CMAKE_SCRIPT_MODE_FILE)
    function(nitwit_add_golden_tests module tool extension)
        set(directory ${PROJECT_SOURCE_DIR}/tests/golden/${module})
        file(GLOB inputs CONFIGURE_DEPENDS ${directory}/*.c)
        list(JOIN ARGN " " arguments)
        foreach(input IN LISTS inputs)
            get_filename_component(name ${input} NAME_WE)
            add_test(NAME golden_${module}_${name}
                COMMAND ${CMAKE_COMMAND}
                    -DTOOL=$<TARGET_FILE:${tool}>
                    "-DARGS=${arguments}"
                    -DINPUT=${input}
                    -DEXPECTED=${directory}/${name}.${extension}
                    -P ${CMAKE_CURRENT_FUNCTION_LIST_FILE})
            set_tests_properties(golden_${module}_${name} PROPERTIES LABELS golden)
        endforeach()
    endfunction()
    return()
endif()

separate_arguments(arguments UNIX_COMMAND "${ARGS}")
execute_process(COMMAND ${TOOL} ${arguments} ${INPUT}
    OUTPUT_VARIABLE actual
    ERROR_VARIABLE errors
    RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "${TOOL} failed (${result}) on ${INPUT}:\n${errors}")
endif()

if("$ENV{NITWIT_UPDATE_GOLDEN}" STREQUAL "1")
    file(WRITE ${EXPECTED} "${actual}")
    message(STATUS "Updated ${EXPECTED}")
    return()
endif()

if(NOT EXISTS ${EXPECTED})
    message(FATAL_ERROR "${EXPECTED} does not exist; run the test with NITWIT_UPDATE_GOLDEN=1 to create it")
endif()
file(READ ${EXPECTED} expected)
if(NOT "${actual}" STREQUAL "${expected}")
    get_filename_component(name ${EXPECTED} NAME)
    set(actualFile ${CMAKE_CURRENT_BINARY_DIR}/${name}.actual)
    file(WRITE ${actualFile} "${actual}")
    execute_process(COMMAND diff -u ${EXPECTED} ${actualFile} OUTPUT_VARIABLE difference)
    message(FATAL_ERROR "The output differs from ${EXPECTED} (actual output: ${actualFile}):\n${difference}")
endif()
