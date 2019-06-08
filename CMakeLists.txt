cmake_minimum_required(VERSION 3.10)
project(cwvalidator CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_FLAGS "-Wall -pedantic -m32 -DUNIX_HOST")
#set(CMAKE_C_FLAGS "-Wall -pedantic -m32 -DUNIX_HOST")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DVERBOSE")
#set(CMAKE_C_FLAGS_DEBUG "-g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")
#set(CMAKE_C_FLAGS_RELEASE "-O3")

file(GLOB P_C_FILES picoc/**/*.cpp)
file(GLOB P_H_FILES picoc/**/*.hpp)

set(P_SOURCE_FILES picoc/picoc.cpp picoc/table.cpp picoc/lex.cpp picoc/parse.cpp picoc/expression.cpp picoc/assumption_expr.cpp
        picoc/heap.cpp picoc/type.cpp picoc/variable.cpp picoc/clibrary.cpp picoc/platform.cpp picoc/include.cpp picoc/debug.cpp
        picoc/platform/platform_unix.cpp picoc/platform/library_unix.cpp picoc/cstdlib/stdio.cpp picoc/cstdlib/math.cpp
        picoc/cstdlib/string.cpp picoc/cstdlib/stdlib.cpp picoc/cstdlib/time.cpp picoc/cstdlib/errno.cpp picoc/cstdlib/ctype.cpp
        picoc/cstdlib/stdbool.cpp picoc/cstdlib/unistd.cpp picoc/veriflib/verif.cpp picoc/cstdlib/assert.cpp)
set(W_SOURCE_FILES witness/witness.hpp witness/witness.cpp witness/witness.cpp witness/automaton.hpp
        witness/automaton_parse.cpp)
set(U_SOURCE_FILES utils/files.hpp utils/files.cpp utils/pugixml/pugiconfig.hpp utils/pugixml/pugixml.cpp
        utils/pugixml/pugixml.hpp witness/automaton_run.cpp)

add_executable(cwvalidator cwvalidator.cpp ${P_C_FILES} ${P_H_FILES}
        ${W_SOURCE_FILES} ${U_SOURCE_FILES})
target_link_libraries(cwvalidator m)

