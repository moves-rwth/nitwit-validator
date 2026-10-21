nitwit_add_module(witness
    SOURCES
        number.cpp
        parse.cpp
        parse_graphml.cpp
        parse_yaml.cpp
        yaml_tree.cpp
    DEPENDS nitwit_support)

# yaml-cpp 0.8.0 declares cmake_minimum_required(VERSION 3.4), which CMake 4 rejects unless this is set.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
# Private: the parsers' types do not appear in the public headers.
FetchContent_MakeAvailable(yaml-cpp pugixml)
target_link_libraries(nitwit_witness PRIVATE yaml-cpp::yaml-cpp pugixml::pugixml)
