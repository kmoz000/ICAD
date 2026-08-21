if(NOT DEFINED ICAD_EXECUTABLE OR NOT DEFINED ICAD_SOURCE OR NOT DEFINED OUTPUT_ROOT)
    message(FATAL_ERROR "constrained-sketch benchmark is missing required paths")
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
   NOT inspect_output MATCHES "\"sketches\":1" OR
   NOT inspect_output MATCHES "\"status\":\"fullyConstrained\"" OR
   NOT inspect_output MATCHES "\"degreesOfFreedom\":0" OR
   NOT inspect_output MATCHES "\"volumeMm3\":4000")
    message(FATAL_ERROR "constrained-sketch inspection mismatch: ${inspect_error}${inspect_output}")
endif()

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" build "${ICAD_SOURCE}" --output-dir "${OUTPUT_ROOT}"
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_output
    ERROR_VARIABLE build_error
)
if(NOT build_result EQUAL 0 OR
   NOT build_output MATCHES "BUILD components=1 solids=1 vertices=8 triangles=12")
    message(FATAL_ERROR "constrained-sketch build mismatch: ${build_error}${build_output}")
endif()
execute_process(
    COMMAND "${ICAD_EXECUTABLE}" inspect-step "${OUTPUT_ROOT}/constrained_sketch.step"
    RESULT_VARIABLE step_result OUTPUT_VARIABLE step_output ERROR_VARIABLE step_error
)
if(NOT step_result EQUAL 0 OR NOT step_output MATCHES "STEP_SOLIDS 1")
    message(FATAL_ERROR "constrained-sketch STEP read-back mismatch: ${step_error}${step_output}")
endif()
execute_process(
    COMMAND "${ICAD_EXECUTABLE}" inspect-stl "${OUTPUT_ROOT}/constrained_sketch.stl"
    RESULT_VARIABLE stl_result OUTPUT_VARIABLE stl_output ERROR_VARIABLE stl_error
)
if(NOT stl_result EQUAL 0 OR NOT stl_output MATCHES "STL_SOLIDS 1")
    message(FATAL_ERROR "constrained-sketch STL read-back mismatch: ${stl_error}${stl_output}")
endif()

message(STATUS "constrained sketch passed: zero DOF, solved profile extrusion, STEP/STL read-back")
