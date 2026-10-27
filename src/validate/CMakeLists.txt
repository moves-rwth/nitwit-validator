nitwit_add_module(validate
    SOURCES
        concrete.cpp
        constraints.cpp
        guard.cpp
        location.cpp
        monitor.cpp
        oracle.cpp
        restart.cpp
        search.cpp
        support_check.cpp
        validator.cpp
        values.cpp
        verdict.cpp
    DEPENDS nitwit_interp nitwit_witness nitwit_parse nitwit_sema)
