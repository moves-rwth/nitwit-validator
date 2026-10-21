nitwit_add_module(driver
    SOURCES
        frontend.cpp
        preprocess.cpp
        replay.cpp
    DEPENDS nitwit_parse nitwit_sema nitwit_interp)

add_executable(nitwit main.cpp)
target_link_libraries(nitwit PRIVATE nitwit_driver)
target_compile_definitions(nitwit PRIVATE NITWIT_VERSION="${PROJECT_VERSION}")
nitwit_set_warnings(nitwit)
