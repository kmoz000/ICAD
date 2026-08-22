if(NOT DEFINED ICAD_EXECUTABLE OR NOT DEFINED FIRST_SOURCE OR NOT DEFINED SECOND_SOURCE)
    message(FATAL_ERROR "cli_compare.cmake requires ICAD_EXECUTABLE, FIRST_SOURCE, SECOND_SOURCE")
endif()

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" compare-json "${FIRST_SOURCE}" "${SECOND_SOURCE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)

if(NOT result EQUAL 0)
    message(FATAL_ERROR "compare-json failed with ${result}\nstdout: ${output}\nstderr: ${error}")
endif()

foreach(required
        "\"schema\":\"icad.agent.comparison.v2\""
        "\"firstOnlyBodies\""
        "\"secondOnlyBodies\""
        "\"changedBodies\""
        "\"selectionDimensions\""
        "\"mechanismDelta\""
        "\"viewDelta\""
        "\"differenceGrid\""
        "\"optimizationMatrix\""
        "\"decisionPolicy\""
        "\"visual\":{\"schema\":\"icad.visual.snapshot.v1\"")
    if(NOT output MATCHES "${required}")
        message(FATAL_ERROR "compare-json omitted required contract field ${required}")
    endif()
endforeach()
