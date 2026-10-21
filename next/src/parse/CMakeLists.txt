nitwit_add_module(parse
    SOURCES
        declarations.cpp
        expressions.cpp
        parser.cpp
        scope.cpp
        statements.cpp
    DEPENDS nitwit_ast nitwit_lex)
