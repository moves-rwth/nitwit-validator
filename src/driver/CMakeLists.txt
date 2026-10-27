nitwit_add_module(driver
    SOURCES
        command_line.cpp
        frontend.cpp
        preprocess.cpp
        replay.cpp
        validation.cpp
    DEPENDS nitwit_parse nitwit_sema nitwit_interp nitwit_validate nitwit_witness)
# The producer version of concrete witnesses (validation.cpp).
target_compile_definitions(nitwit_driver PRIVATE NITWIT_VERSION="${PROJECT_VERSION}")

add_executable(nitwit main.cpp)
target_link_libraries(nitwit PRIVATE nitwit_driver)
target_compile_definitions(nitwit PRIVATE NITWIT_VERSION="${PROJECT_VERSION}")
nitwit_set_warnings(nitwit)
if(NITWIT_STATIC_RUNTIME)
    # The release executable needs no C++ runtime on the host, only glibc (docs/release.md).
    target_link_options(nitwit PRIVATE -static-libstdc++ -static-libgcc)
endif()
