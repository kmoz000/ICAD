if(NOT DEFINED ICAD_EXECUTABLE OR NOT DEFINED ICAD_SOURCE OR NOT DEFINED OUTPUT_ROOT OR
   NOT DEFINED CHROMIUM_EXECUTABLE)
    message(FATAL_ERROR "viewer smoke is missing required paths")
endif()
file(REMOVE_RECURSE "${OUTPUT_ROOT}")
file(MAKE_DIRECTORY "${OUTPUT_ROOT}")
execute_process(
    COMMAND "${ICAD_EXECUTABLE}" build "${ICAD_SOURCE}" --output-dir "${OUTPUT_ROOT}"
    RESULT_VARIABLE build_result
    ERROR_VARIABLE build_error
)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "viewer fixture build failed: ${build_error}")
endif()
execute_process(
    COMMAND "${CHROMIUM_EXECUTABLE}" --headless --disable-gpu-sandbox
            --run-all-compositor-stages-before-draw --virtual-time-budget=2000 --dump-dom
            "file://${OUTPUT_ROOT}/advanced.html"
    RESULT_VARIABLE browser_result
    OUTPUT_VARIABLE browser_output
    ERROR_VARIABLE browser_error
)
if(NOT browser_result EQUAL 0)
    message(FATAL_ERROR "headless viewer failed: ${browser_error}")
endif()
foreach(expected IN ITEMS "icad-viewer-controls" "icad-semantic-tree"
                          "Interactive ICAD WebGL design viewport" "substructure")
    if(NOT browser_output MATCHES "${expected}")
        message(FATAL_ERROR "rendered viewer DOM is missing ${expected}")
    endif()
endforeach()
message(STATUS "headless Chromium viewer smoke passed with WebGL controls and semantic tree")
