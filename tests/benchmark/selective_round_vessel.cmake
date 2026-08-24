if(NOT DEFINED ICAD_EXECUTABLE OR NOT DEFINED ICAD_SOURCE OR NOT DEFINED OUTPUT_ROOT)
    message(FATAL_ERROR "selective edge-rounding benchmark is missing required paths")
endif()

file(REMOVE_RECURSE "${OUTPUT_ROOT}")
file(MAKE_DIRECTORY "${OUTPUT_ROOT}")

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" visual-json "${ICAD_SOURCE}"
    RESULT_VARIABLE visual_result OUTPUT_FILE "${OUTPUT_ROOT}/visual.json"
    ERROR_VARIABLE visual_error TIMEOUT 30
)
if(NOT visual_result EQUAL 0)
    message(FATAL_ERROR "selective edge visual-json failed: ${visual_error}")
endif()
file(READ "${OUTPUT_ROOT}/visual.json" visual_output)
foreach(expected IN ITEMS
        "\"kind\":\"EDGE_LOOP\""
        "\"classification\":\"INNER\""
        "\"classification\":\"OUTER\""
        "\"applicableOperations\":[\"FILLET\",\"CHAMFER\"]")
    string(FIND "${visual_output}" "${expected}" expected_offset)
    if(expected_offset EQUAL -1)
        message(FATAL_ERROR "selective edge visual contract is missing ${expected}")
    endif()
endforeach()

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" measure "${ICAD_SOURCE}"
    RESULT_VARIABLE measure_result OUTPUT_VARIABLE measure_output ERROR_VARIABLE measure_error
    TIMEOUT 30
)
if(NOT measure_result EQUAL 0 OR NOT measure_output MATCHES "PARTS 2" OR
   NOT measure_output MATCHES "BOUNDS_MIN -40 -40 0" OR
   NOT measure_output MATCHES "BOUNDS_MAX 40 40 50")
    message(FATAL_ERROR "selective edge measurements mismatch: ${measure_error}${measure_output}")
endif()

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" build "${ICAD_SOURCE}" --output-dir "${OUTPUT_ROOT}"
    RESULT_VARIABLE build_result OUTPUT_VARIABLE build_output ERROR_VARIABLE build_error
    TIMEOUT 45
)
if(NOT build_result EQUAL 0 OR NOT build_output MATCHES "BUILD components=2 solids=2" OR
   NOT build_output MATCHES "triangles=1536")
    message(FATAL_ERROR "selective edge build failed: ${build_error}${build_output}")
endif()

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" inspect-step "${OUTPUT_ROOT}/selective_round_vessel.step"
    RESULT_VARIABLE step_result OUTPUT_VARIABLE step_output ERROR_VARIABLE step_error
)
if(NOT step_result EQUAL 0 OR NOT step_output MATCHES "STEP_SOLIDS 2")
    message(FATAL_ERROR "selective edge STEP read-back mismatch: ${step_error}${step_output}")
endif()

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" inspect-stl "${OUTPUT_ROOT}/selective_round_vessel.stl"
    RESULT_VARIABLE stl_result OUTPUT_VARIABLE stl_output ERROR_VARIABLE stl_error
)
if(NOT stl_result EQUAL 0 OR NOT stl_output MATCHES "STL_SOLIDS 2" OR
   NOT stl_output MATCHES "STL_FACETS 1536")
    message(FATAL_ERROR "selective edge STL read-back mismatch: ${stl_error}${stl_output}")
endif()

message(STATUS "inside/outside edge selection passed fillet, visual applicability, STEP, and STL gates")
