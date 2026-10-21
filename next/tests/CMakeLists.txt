# Unit tests per module, in the order of src/CMakeLists.txt; corpus tests only with a dataset.
foreach(module IN ITEMS support value lex ast parse sema interp witness validate driver)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/${module}/CMakeLists.txt)
        add_subdirectory(unit/${module})
    endif()
    if(NITWIT_DATASET_DIR AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/corpus/${module}/CMakeLists.txt)
        add_subdirectory(corpus/${module})
    endif()
endforeach()
