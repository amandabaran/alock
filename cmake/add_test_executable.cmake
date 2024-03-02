# Copyright (c) 2023 Scalable Systems and Software Research Group   

function(add_test_executable TARGET SOURCE)
    set(options DISABLE_TEST)
    set(oneValueArgs "")
    set(multiValueArgs LIBS)

    cmake_parse_arguments(_ "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN} )

    add_executable(${TARGET} ${SOURCE})
 
    if( NOT("LIBS" IN_LIST __KEYWORDS_MISSING_VALUES OR NOT DEFINED __LIBS) )
      target_link_libraries(${TARGET} PRIVATE ${__LIBS})
    endif()
    
    if(NOT ${__DISABLE_TEST})
        add_test(NAME ${TARGET} COMMAND ${TARGET})
    endif()
endfunction()