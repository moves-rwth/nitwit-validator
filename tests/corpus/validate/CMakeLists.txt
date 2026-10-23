find_package(Threads REQUIRED)

nitwit_add_corpus_test(validate
    SOURCES corpus_validate.cpp
    DEPENDS Threads::Threads)

# The test runs the validator as a process per witness, as BenchExec does.
add_dependencies(corpus_validate nitwit)
target_compile_definitions(corpus_validate PRIVATE NITWIT_EXECUTABLE="$<TARGET_FILE:nitwit>"
                                                   NITWIT_CORPUS_LOG_DIR="${CMAKE_CURRENT_BINARY_DIR}/logs")
set_tests_properties(corpus_validate PROPERTIES TIMEOUT 7200)
