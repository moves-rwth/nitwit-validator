nitwit_add_corpus_test(witness
    SOURCES corpus_witness.cpp
    DEPENDS nitwit_witness)

find_package(Threads REQUIRED)
target_link_libraries(corpus_witness PRIVATE Threads::Threads)
