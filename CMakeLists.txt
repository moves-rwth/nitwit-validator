cmake_minimum_required(VERSION 3.10)
project(nitwit CXX)

option(NITWIT_ENABLE_TRANSITION_LIMIT "Whether the execution shall run ad infinitum or terminate after a high, but finite amount." ON)
option(NITWIT_REQUIRE_MATCHING_ORIGINFILENAME "Whether matching of edges also depends on the origin filename." OFF)
option(NITWIT_NO_HEADER_INCLUDE "Ignores any 'extern' declarations and automatically includes all available libraries." OFF)
option(NITWIT_STOP_IN_SINK "Whether the validation is terminated once the sink state is reached." OFF)
option(NITWIT_STRICT_VALIDATION "Whether traces not accepted by the witness automaton are not allowed." OFF)
set(NITWIT_TRANSITION_LIMIT "5000000" CACHE STRING "The maximum number of transitions taken when NITWIT_ENABLE_TRANSITION_LIMIT is enabled.")

# Debugging Support
option(NITWIT_VERBOSE "Whether verbose output shall be produced. Warning, this slows down execution a lot!" OFF)
option(NITWIT_DEBUG_EXPRESSIONS "Whether debug output shall be produced when traversing the expression stack." OFF)
option(NITWIT_USE_ASAN "Whether to use Clang Address Sanitizer (ASAN)" OFF)

if(MSVC)
	set(CMAKE_CXX_STANDARD 17)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
	set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
	set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
	add_definitions(-D_CRT_SECURE_NO_WARNINGS)
else()
	set(CMAKE_CXX_STANDARD 17)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -pedantic  -Wno-write-strings")
	set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g3 -gdwarf-2 -O0")
	set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
	add_definitions(-DUNIX_HOST)
	# Otherwise, stat() calls will fail:
	add_definitions(-D_FILE_OFFSET_BITS=64)
endif()

if (NITWIT_ENABLE_TRANSITION_LIMIT)
	add_definitions(-DENABLE_TRANSITION_LIMIT)
endif()

if (NITWIT_REQUIRE_MATCHING_ORIGINFILENAME)
	add_definitions(-DREQUIRE_MATCHING_ORIGINFILENAME)
endif()

if (NITWIT_NO_HEADER_INCLUDE)
	add_definitions(-DNO_HEADER_INCLUDE)
endif()

if (NITWIT_STOP_IN_SINK)
	add_definitions(-DSTOP_IN_SINK)
endif()

if (NITWIT_STRICT_VALIDATION)
	add_definitions(-DSTRICT_VALIDATION)
endif()

if (("${NITWIT_TRANSITION_LIMIT}" STREQUAL "") OR ("${NITWIT_TRANSITION_LIMIT}" LESS_EQUAL "0") OR (NOT NITWIT_TRANSITION_LIMIT MATCHES "^[0-9]+$"))
	message(FATAL_ERROR "Expected a positive number for NITWIT_TRANSITION_LIMIT, got '${NITWIT_TRANSITION_LIMIT}'.")
else()
	add_definitions(-DUNSUCCESSFUL_TRIES_LIMIT=${NITWIT_TRANSITION_LIMIT})
endif()

# Debugging Support
if (NITWIT_VERBOSE)
	add_definitions(-DVERBOSE)
endif()

if (NITWIT_DEBUG_EXPRESSIONS)
	add_definitions(-DDEBUG_EXPRESSIONS)
endif()

if (NITWIT_USE_ASAN)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
endif()

file(GLOB P_C_FILES picoc/**/*.cpp picoc/*.cpp picoc/values.cpp)
file(GLOB P_H_FILES picoc/**/*.hpp picoc/*.hpp)
file(GLOB W_SOURCE_FILES witness/**/*.hpp witness/**/*.cpp witness/*.cpp witness/*.hpp)
file(GLOB U_SOURCE_FILES utils/**/*.hpp utils/**/*.cpp utils/*.cpp utils/*.hpp)

add_executable(nitwit32 main.cpp ${P_C_FILES} ${P_H_FILES} ${W_SOURCE_FILES} ${U_SOURCE_FILES})
set_target_properties(nitwit32 PROPERTIES COMPILE_FLAGS "-m32" LINK_FLAGS "-m32")

add_executable(nitwit64 main.cpp ${P_C_FILES} ${P_H_FILES} ${W_SOURCE_FILES} ${U_SOURCE_FILES})

target_link_libraries(nitwit32 m)
target_link_libraries(nitwit64 m)

