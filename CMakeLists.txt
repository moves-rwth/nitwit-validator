cmake_minimum_required(VERSION 3.28)
project(nitwit VERSION 4.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF) # no C++ modules; avoids needing clang-scan-deps
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(NITWIT_BUILD_TESTS "Build the tests" ON)
option(NITWIT_WERROR "Treat warnings as errors" ON)
option(NITWIT_STATIC_RUNTIME "Link libstdc++ and libgcc into nitwit statically, for the release archive" OFF)
set(NITWIT_SANITIZE "" CACHE STRING "Sanitizers to build with, e.g. address,undefined")
set(NITWIT_DATASET_DIR "" CACHE PATH "SV-COMP 2026 witness dataset for the corpus tests (optional)")

if(NITWIT_STATIC_RUNTIME)
    if(NITWIT_SANITIZE)
        message(FATAL_ERROR "NITWIT_STATIC_RUNTIME is for the release build; "
                            "it cannot be combined with NITWIT_SANITIZE")
    endif()
    # The release executable embeds no paths of the build machine, e.g. in the file names of assertion messages. Only
    # here, because relative file names in the debug information keep debuggers from finding the sources.
    add_compile_options(-ffile-prefix-map=${PROJECT_SOURCE_DIR}/= -ffile-prefix-map=${PROJECT_BINARY_DIR}/=)
endif()

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)
include(Warnings)
include(Sanitizers)
include(Dependencies)
include(NitwitModule)
include(GoldenTest)

add_subdirectory(src)

# Executables: the validator (driver) and developer tools.
foreach(tool IN ITEMS nitwit-dump layout-probe)
    if(EXISTS ${PROJECT_SOURCE_DIR}/tools/${tool}/CMakeLists.txt)
        add_subdirectory(tools/${tool})
    endif()
endforeach()

if(NITWIT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
