cmake_minimum_required(VERSION 3.10)
project(cwvalidator CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_FLAGS "-Wall -pedantic -m32 -DUNIX_HOST -Wno-write-strings -DENABLE_TRANSITION_LIMIT")
set(CMAKE_CXX_FLAGS_DEBUG "-g3 -gdwarf-2 -O0 -DVERBOSE")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

file(GLOB P_C_FILES picoc/**/*.cpp picoc/*.cpp picoc/values.cpp)
file(GLOB P_H_FILES picoc/**/*.hpp picoc/*.hpp)

file(GLOB W_SOURCE_FILES witness/**/*.hpp witness/**/*.cpp witness/*.cpp witness/*.hpp)
file(GLOB U_SOURCE_FILES utils/**/*.hpp utils/**/*.cpp utils/*.cpp utils/*.hpp)

add_executable(cwvalidator cwvalidator.cpp ${P_C_FILES} ${P_H_FILES}
        ${W_SOURCE_FILES} ${U_SOURCE_FILES})


target_link_libraries(cwvalidator m)

