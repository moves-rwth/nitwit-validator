nitwit_add_unit_test(sema
    SOURCES
        builtins_test.cpp
        constant_test.cpp
        constraint_test.cpp
        conversions_test.cpp
        declarations_test.cpp
        expressions_test.cpp
        initializers_test.cpp
        layout_test.cpp
        statements_test.cpp
        types_test.cpp
    DEPENDS nitwit_parse nitwit_sema)
