if(NOT DEFINED ICAD_EXECUTABLE OR NOT DEFINED ICAD_SOURCE OR NOT DEFINED EXPECT_SUCCESS)
    message(FATAL_ERROR "cli_check.cmake requires ICAD_EXECUTABLE, ICAD_SOURCE, and EXPECT_SUCCESS")
endif()

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" check "${ICAD_SOURCE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)

if(EXPECT_SUCCESS AND NOT result EQUAL 0)
    message(FATAL_ERROR "expected check success, got ${result}\nstdout: ${output}\nstderr: ${error}")
endif()

if(NOT EXPECT_SUCCESS AND result EQUAL 0)
    message(FATAL_ERROR "expected check failure\nstdout: ${output}\nstderr: ${error}")
endif()

if(EXPECT_SUCCESS AND NOT output MATCHES "compile check passed")
    message(FATAL_ERROR "success output did not contain the expected status: ${output}")
endif()

if(NOT EXPECT_SUCCESS AND DEFINED EXPECT_CODE AND NOT error MATCHES "${EXPECT_CODE}")
    message(FATAL_ERROR "failure output did not contain a stable diagnostic code: ${error}")
endif()

