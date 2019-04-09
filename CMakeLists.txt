cmake_minimum_required(VERSION 3.13)
project(cwvalidator C CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_FLAGS "-Wall -pedantic -g") # xml2 fails to link w/o -I

file(GLOB P_C_FILES picoc/**/*.c)
file(GLOB P_H_FILES picoc/**/*.h)

set(P_SOURCE_FILES picoc/picoc.c picoc/table.c picoc/lex.c picoc/parse.c picoc/expression.c picoc/heap.c picoc/type.c picoc/variable.c picoc/clibrary.c picoc/platform.c picoc/include.c picoc/debug.c picoc/platform/platform_unix.c picoc/platform/library_unix.c picoc/cstdlib/stdio.c picoc/cstdlib/math.c picoc/cstdlib/string.c picoc/cstdlib/stdlib.c picoc/cstdlib/time.c picoc/cstdlib/errno.c picoc/cstdlib/ctype.c picoc/cstdlib/stdbool.c picoc/cstdlib/unistd.c )
set(W_SOURCE_FILES witness/witness.hpp witness/witness.cpp witness/witness.cpp witness/automaton.hpp witness/automaton_parse.cpp)
set(U_SOURCE_FILES utils/files.hpp utils/files.cpp utils/pugixml/pugiconfig.hpp utils/pugixml/pugixml.cpp utils/pugixml/pugixml.hpp)
#set(CMAKE_VERBOSE_MAKEFILE ON)

add_compile_definitions(VER="2.1")
add_compile_definitions(UNIX_HOST)

add_executable(cwvalidator cwvalidator.cpp ${P_SOURCE_FILES} ${P_H_FILES}
        ${W_SOURCE_FILES} ${U_SOURCE_FILES})
target_link_libraries(cwvalidator m)
target_link_libraries(cwvalidator readline)
target_link_libraries(cwvalidator xml2)

