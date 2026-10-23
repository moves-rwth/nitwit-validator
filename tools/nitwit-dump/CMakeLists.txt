add_executable(nitwit-dump main.cpp)
target_link_libraries(nitwit-dump PRIVATE nitwit_parse nitwit_sema)
nitwit_set_warnings(nitwit-dump)
