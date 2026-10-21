# nitwit_add_module(<name> SOURCES ... [DEPENDS <modules or targets>...])
# Creates the static library nitwit_<name>. Modules include each other's headers as "<module>/<file>.hpp".
function(nitwit_add_module name)
    cmake_parse_arguments(ARG "" "" "SOURCES;DEPENDS" ${ARGN})
    add_library(nitwit_${name} STATIC ${ARG_SOURCES})
    target_include_directories(nitwit_${name} PUBLIC ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(nitwit_${name} PUBLIC ${ARG_DEPENDS})
    nitwit_set_warnings(nitwit_${name})
endfunction()

# nitwit_add_unit_test(<name> SOURCES ... DEPENDS ...)
# Creates the GoogleTest executable unit_<name>; its tests carry the CTest label "unit".
function(nitwit_add_unit_test name)
    cmake_parse_arguments(ARG "" "" "SOURCES;DEPENDS" ${ARGN})
    add_executable(unit_${name} ${ARG_SOURCES})
    target_link_libraries(unit_${name} PRIVATE ${ARG_DEPENDS} GTest::gtest_main GTest::gmock)
    target_compile_definitions(unit_${name} PRIVATE NITWIT_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
    nitwit_set_warnings(unit_${name})
    gtest_discover_tests(unit_${name} DISCOVERY_MODE PRE_TEST PROPERTIES LABELS unit)
endfunction()

# nitwit_add_corpus_test(<name> SOURCES ... DEPENDS ...)
# Creates the executable corpus_<name>, run as `corpus_<name> <dataset-dir> <repository-root>` with the CTest label
# "corpus". Only built when NITWIT_DATASET_DIR is set.
function(nitwit_add_corpus_test name)
    cmake_parse_arguments(ARG "" "" "SOURCES;DEPENDS" ${ARGN})
    add_executable(corpus_${name} ${ARG_SOURCES})
    target_link_libraries(corpus_${name} PRIVATE ${ARG_DEPENDS})
    nitwit_set_warnings(corpus_${name})
    add_test(NAME corpus_${name} COMMAND corpus_${name} ${NITWIT_DATASET_DIR} ${PROJECT_SOURCE_DIR}/..)
    set_tests_properties(corpus_${name} PROPERTIES LABELS corpus)
endfunction()
