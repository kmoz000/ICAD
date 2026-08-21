function(icad_enable_sanitizers target)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${target} INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target} INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
    else()
        message(WARNING "ICAD_ENABLE_SANITIZERS is not configured for this compiler")
    endif()
endfunction()

