# Modules, see docs/architecture.md. A module is built once its directory has a CMakeLists.txt.
foreach(module IN ITEMS support value lex ast parse sema interp witness validate driver)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${module}/CMakeLists.txt)
        add_subdirectory(${module})
    endif()
endforeach()
