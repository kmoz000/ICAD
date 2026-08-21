function(icad_set_project_warnings target warnings_as_errors)
    if(MSVC)
        set(warnings /W4 /permissive-)
        if(warnings_as_errors)
            list(APPEND warnings /WX)
        endif()
    else()
        set(warnings
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wshadow
            -Wsign-conversion
        )
        if(warnings_as_errors)
            list(APPEND warnings -Werror)
        endif()
    endif()

    target_compile_options(${target} INTERFACE ${warnings})
endfunction()

