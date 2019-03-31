cmake_minimum_required(VERSION 3.13)
project(cwvalidator C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_FLAGS "-Wall -pedantic -g")

file(GLOB P_C_FILES picoc/**/*.c)
file(GLOB P_H_FILES picoc/**/*.h)

set(P_SOURCE_FILES picoc/picoc.c picoc/table.c picoc/lex.c picoc/parse.c picoc/expression.c picoc/heap.c picoc/type.c picoc/variable.c picoc/clibrary.c picoc/platform.c picoc/include.c picoc/debug.c picoc/platform/platform_unix.c picoc/platform/library_unix.c picoc/cstdlib/stdio.c picoc/cstdlib/math.c picoc/cstdlib/string.c picoc/cstdlib/stdlib.c picoc/cstdlib/time.c picoc/cstdlib/errno.c picoc/cstdlib/ctype.c picoc/cstdlib/stdbool.c picoc/cstdlib/unistd.c)
add_compile_definitions(VER="2.1")
add_compile_definitions(UNIX_HOST)

add_executable(cwvalidator main.c ${P_SOURCE_FILES})
target_link_libraries(cwvalidator m)
target_link_libraries(cwvalidator readline)

