if(NOT DEFINED ICAD_EXECUTABLE OR NOT DEFINED ICAD_SOURCE OR NOT DEFINED OUTPUT_ROOT)
    message(FATAL_ERROR "assembly-instance benchmark is missing required paths")
endif()
file(REMOVE_RECURSE "${OUTPUT_ROOT}")
file(MAKE_DIRECTORY "${OUTPUT_ROOT}")
execute_process(
    COMMAND "${ICAD_EXECUTABLE}" inspect-json "${ICAD_SOURCE}"
    RESULT_VARIABLE inspect_result OUTPUT_VARIABLE inspect_output ERROR_VARIABLE inspect_error
)
if(NOT inspect_result EQUAL 0 OR NOT inspect_output MATCHES "\"instances\":2" OR
   NOT inspect_output MATCHES "\"jointDriven\":true" OR
   NOT inspect_output MATCHES "\"solvedBoundsMin\":\\[30")
    message(FATAL_ERROR "assembly-instance inspection mismatch: ${inspect_error}${inspect_output}")
endif()
execute_process(
    COMMAND "${ICAD_EXECUTABLE}" build "${ICAD_SOURCE}" --output-dir "${OUTPUT_ROOT}"
    RESULT_VARIABLE build_result OUTPUT_VARIABLE build_output ERROR_VARIABLE build_error
)
if(NOT build_result EQUAL 0 OR NOT build_output MATCHES "BUILD components=3 solids=3")
    message(FATAL_ERROR "assembly-instance build mismatch: ${build_error}${build_output}")
endif()
execute_process(
    COMMAND "${ICAD_EXECUTABLE}" inspect-step "${OUTPUT_ROOT}/assembly_instances.step"
    RESULT_VARIABLE step_result OUTPUT_VARIABLE step_output ERROR_VARIABLE step_error
)
if(NOT step_result EQUAL 0 OR NOT step_output MATCHES "STEP_SOLIDS 3")
    message(FATAL_ERROR "assembly-instance STEP mismatch: ${step_error}${step_output}")
endif()
message(STATUS "assembly instances passed: 2 occurrences, revolute solve, 3 STEP solids")
