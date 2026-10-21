nitwit_add_unit_test(parse
    SOURCES
        declarations_test.cpp
        errors_test.cpp
        expressions_test.cpp
        gnu_test.cpp
        scopes_test.cpp
        statements_test.cpp
    DEPENDS nitwit_parse)
