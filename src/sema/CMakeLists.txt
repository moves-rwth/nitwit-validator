nitwit_add_module(sema
    SOURCES
        builtins.cpp
        constant.cpp
        conversions.cpp
        declarations.cpp
        expressions.cpp
        initializers.cpp
        layout.cpp
        operators.cpp
        records.cpp
        sema.cpp
        statements.cpp
        types.cpp
    DEPENDS nitwit_ast nitwit_value)
