if(NOT DEFINED ICAD_EXECUTABLE OR NOT DEFINED ICAD_SOURCE OR NOT DEFINED OUTPUT_ROOT)
    message(FATAL_ERROR "advanced-surfaces benchmark is missing required paths")
endif()

file(REMOVE_RECURSE "${OUTPUT_ROOT}")
file(MAKE_DIRECTORY "${OUTPUT_ROOT}")

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" inspect-json "${ICAD_SOURCE}"
    RESULT_VARIABLE inspect_result
    OUTPUT_VARIABLE inspect_output
    ERROR_VARIABLE inspect_error
)
if(NOT inspect_result EQUAL 0 OR
   NOT inspect_output MATCHES "\"surfaceOperations\":4" OR
   NOT inspect_output MATCHES "\"solids\":4" OR
   NOT inspect_output MATCHES "\"type\":\"SWEEP\"" OR
   NOT inspect_output MATCHES "\"type\":\"LOFT\"" OR
   NOT inspect_output MATCHES "\"type\":\"FREEFORM\"" OR
   NOT inspect_output MATCHES "\"type\":\"REVOLVE\"")
    message(FATAL_ERROR "advanced-surfaces inspection mismatch: ${inspect_error}${inspect_output}")
endif()

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" build "${ICAD_SOURCE}" --output-dir "${OUTPUT_ROOT}"
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_output
    ERROR_VARIABLE build_error
)
if(NOT build_result EQUAL 0 OR
   NOT build_output MATCHES "BUILD components=4 solids=4 vertices=1080 triangles=2148")
    message(FATAL_ERROR "advanced-surfaces build mismatch: ${build_error}${build_output}")
endif()

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" inspect-step "${OUTPUT_ROOT}/advanced_surfaces.step"
    RESULT_VARIABLE step_result
    OUTPUT_VARIABLE step_output
    ERROR_VARIABLE step_error
)
if(NOT step_result EQUAL 0 OR NOT step_output MATCHES "STEP_SOLIDS 4")
    message(FATAL_ERROR "advanced-surfaces STEP read-back mismatch: ${step_error}${step_output}")
endif()

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" inspect-stl "${OUTPUT_ROOT}/advanced_surfaces.stl"
    RESULT_VARIABLE stl_result
    OUTPUT_VARIABLE stl_output
    ERROR_VARIABLE stl_error
)
if(NOT stl_result EQUAL 0 OR
   NOT stl_output MATCHES "STL_SOLIDS 4" OR
   NOT stl_output MATCHES "STL_FACETS 2148")
    message(FATAL_ERROR "advanced-surfaces STL read-back mismatch: ${stl_error}${stl_output}")
endif()

message(STATUS
    "advanced surfaces passed: sweep/loft/free-form/curved revolution, 4 solids, 2148 facets, STEP/STL read-back")
