nitwit_add_module(driver
    SOURCES
        command_line.cpp
        frontend.cpp
        preprocess.cpp
        replay.cpp
        validation.cpp
    DEPENDS nitwit_parse nitwit_sema nitwit_interp nitwit_validate nitwit_witness)

add_executable(nitwit main.cpp)
target_link_libraries(nitwit PRIVATE nitwit_driver)
target_compile_definitions(nitwit PRIVATE NITWIT_VERSION="${PROJECT_VERSION}")
nitwit_set_warnings(nitwit)
if(NITWIT_STATIC)
    # The release archive runs on any x86-64 Linux, whatever its C and C++ libraries (docs/release.md).
    target_link_options(nitwit PRIVATE -static)
endif()
