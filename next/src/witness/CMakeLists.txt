nitwit_add_module(witness
    SOURCES
        number.cpp
        parse.cpp
        parse_graphml.cpp
        parse_yaml.cpp
    DEPENDS nitwit_support)

# Private: the parsers' types do not appear in the public headers.
FetchContent_MakeAvailable(yaml-cpp pugixml)
target_link_libraries(nitwit_witness PRIVATE yaml-cpp::yaml-cpp pugixml::pugixml)
