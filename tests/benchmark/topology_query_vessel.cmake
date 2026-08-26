if(NOT DEFINED ICAD_EXECUTABLE OR NOT DEFINED ICAD_SOURCE OR NOT DEFINED OUTPUT_ROOT)
    message(FATAL_ERROR "topology-query benchmark is missing required paths")
endif()

file(REMOVE_RECURSE "${OUTPUT_ROOT}")
file(MAKE_DIRECTORY "${OUTPUT_ROOT}")

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" visual-json "${ICAD_SOURCE}"
    RESULT_VARIABLE visual_result OUTPUT_FILE "${OUTPUT_ROOT}/visual.json"
    ERROR_VARIABLE visual_error TIMEOUT 30
)
if(NOT visual_result EQUAL 0)
    message(FATAL_ERROR "topology-query visual-json failed: ${visual_error}")
endif()
file(READ "${OUTPUT_ROOT}/visual.json" visual_output)
foreach(expected IN ITEMS
        "\"name\":\"upper_inner_rim\""
        "\"matchedTopologyId\":\"vessel/wall_solid/edge.loop.top.inner\""
        "\"reference\":\"upper_inner_rim\""
        "\"matchReason\":\"matched one circular concave edge loop adjacent to the top face of the source feature\""
        "\"allowed\":[\"FILLET\",\"CHAMFER\"]"
        "\"operation\":\"SHELL\"")
    string(FIND "${visual_output}" "${expected}" expected_offset)
    if(expected_offset EQUAL -1)
        message(FATAL_ERROR "topology-query visual contract is missing ${expected}")
    endif()
endforeach()

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" inspect-json "${ICAD_SOURCE}"
    RESULT_VARIABLE inspect_result OUTPUT_VARIABLE inspect_output ERROR_VARIABLE inspect_error
    TIMEOUT 30
)
if(NOT inspect_result EQUAL 0 OR
   NOT inspect_output MATCHES "\"kind\":\"topology_selection\"")
    message(FATAL_ERROR "topology-query dependency evidence mismatch: ${inspect_error}${inspect_output}")
endif()

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" measure "${ICAD_SOURCE}"
    RESULT_VARIABLE measure_result OUTPUT_VARIABLE measure_output ERROR_VARIABLE measure_error
    TIMEOUT 30
)
if(NOT measure_result EQUAL 0 OR NOT measure_output MATCHES "PARTS 1" OR
   NOT measure_output MATCHES "BOUNDS_MIN -40 -40 0" OR
   NOT measure_output MATCHES "BOUNDS_MAX 40 40 50")
    message(FATAL_ERROR "topology-query measurements mismatch: ${measure_error}${measure_output}")
endif()

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" build "${ICAD_SOURCE}" --output-dir "${OUTPUT_ROOT}"
    RESULT_VARIABLE build_result OUTPUT_VARIABLE build_output ERROR_VARIABLE build_error
    TIMEOUT 45
)
if(NOT build_result EQUAL 0 OR NOT build_output MATCHES "BUILD components=1 solids=1" OR
   NOT build_output MATCHES "triangles=2304")
    message(FATAL_ERROR "topology-query build failed: ${build_error}${build_output}")
endif()

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" inspect-step "${OUTPUT_ROOT}/topology_query_vessel.step"
    RESULT_VARIABLE step_result OUTPUT_VARIABLE step_output ERROR_VARIABLE step_error
)
if(NOT step_result EQUAL 0 OR NOT step_output MATCHES "STEP_SOLIDS 1")
    message(FATAL_ERROR "topology-query STEP read-back mismatch: ${step_error}${step_output}")
endif()

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" inspect-stl "${OUTPUT_ROOT}/topology_query_vessel.stl"
    RESULT_VARIABLE stl_result OUTPUT_VARIABLE stl_output ERROR_VARIABLE stl_error
)
if(NOT stl_result EQUAL 0 OR NOT stl_output MATCHES "STL_SOLIDS 1" OR
   NOT stl_output MATCHES "STL_FACETS 2304")
    message(FATAL_ERROR "topology-query STL read-back mismatch: ${stl_error}${stl_output}")
endif()

message(STATUS "named topology query passed visual, dependency, native, STEP, and STL gates")
