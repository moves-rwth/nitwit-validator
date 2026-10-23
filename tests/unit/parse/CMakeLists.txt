nitwit_add_unit_test(parse
    SOURCES
        constraint_test.cpp
        declarations_test.cpp
        errors_test.cpp
        expressions_test.cpp
        gnu_test.cpp
        scopes_test.cpp
        statements_test.cpp
    # sema: the constraint tests check that sema accepts parsed constraints.
    DEPENDS nitwit_parse nitwit_sema)
