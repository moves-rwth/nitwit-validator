add_executable(layout-probe main.cpp)
target_link_libraries(layout-probe PRIVATE nitwit_parse nitwit_sema)
nitwit_set_warnings(layout-probe)
