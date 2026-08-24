if(NOT DEFINED ICAD_EXECUTABLE OR NOT DEFINED ICAD_SOURCE OR NOT DEFINED OUTPUT_ROOT)
    message(FATAL_ERROR "rounded tangency benchmark is missing required paths")
endif()

file(REMOVE_RECURSE "${OUTPUT_ROOT}")
file(MAKE_DIRECTORY "${OUTPUT_ROOT}")

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" visual-json "${ICAD_SOURCE}"
    RESULT_VARIABLE visual_result OUTPUT_FILE "${OUTPUT_ROOT}/visual.json"
    ERROR_VARIABLE visual_error TIMEOUT 20
)
if(NOT visual_result EQUAL 0)
    message(FATAL_ERROR "rounded tangency visual-json failed: ${visual_error}")
endif()
file(READ "${OUTPUT_ROOT}/visual.json" visual_output)
foreach(expected IN ITEMS
        "\"status\":\"fullyConstrained\""
        "\"degreesOfFreedom\":0"
        "\"type\":\"TANGENT\""
        "\"capsule.bottom\""
        "\"capsule.right_end\""
        "\"capsule.lower_right\"")
    if(NOT visual_output MATCHES "${expected}")
        message(FATAL_ERROR "rounded tangency visual contract is missing ${expected}")
    endif()
endforeach()

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" measure "${ICAD_SOURCE}"
    RESULT_VARIABLE measure_result OUTPUT_VARIABLE measure_output ERROR_VARIABLE measure_error
    TIMEOUT 20
)
if(NOT measure_result EQUAL 0 OR
   NOT measure_output MATCHES "PARTS 1" OR
   NOT measure_output MATCHES "BOUNDS_MIN -40 -10 0" OR
   NOT measure_output MATCHES "BOUNDS_MAX 40 10 8")
    message(FATAL_ERROR "rounded tangency measurements mismatch: ${measure_error}${measure_output}")
endif()

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" build "${ICAD_SOURCE}" --output-dir "${OUTPUT_ROOT}"
    RESULT_VARIABLE build_result OUTPUT_VARIABLE build_output ERROR_VARIABLE build_error
    TIMEOUT 30
)
if(NOT build_result EQUAL 0 OR NOT build_output MATCHES "BUILD components=1 solids=1")
    message(FATAL_ERROR "rounded tangency build failed: ${build_error}${build_output}")
endif()

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" inspect-step "${OUTPUT_ROOT}/rounded_tangent_plate.step"
    RESULT_VARIABLE step_result OUTPUT_VARIABLE step_output ERROR_VARIABLE step_error
)
if(NOT step_result EQUAL 0 OR NOT step_output MATCHES "STEP_SOLIDS 1")
    message(FATAL_ERROR "rounded tangency STEP read-back mismatch: ${step_error}${step_output}")
endif()

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" inspect-stl "${OUTPUT_ROOT}/rounded_tangent_plate.stl"
    RESULT_VARIABLE stl_result OUTPUT_VARIABLE stl_output ERROR_VARIABLE stl_error
)
if(NOT stl_result EQUAL 0 OR NOT stl_output MATCHES "STL_SOLIDS 1" OR
   NOT stl_output MATCHES "STL_FACETS 132")
    message(FATAL_ERROR "rounded tangency STL read-back mismatch: ${stl_error}${stl_output}")
endif()

message(STATUS "rounded line-arc tangency passed solver, visual, build, STEP, and STL gates")
