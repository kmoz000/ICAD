if(NOT DEFINED ICAD_EXECUTABLE OR NOT DEFINED ICAD_SOURCE OR NOT DEFINED OUTPUT_ROOT)
    message(FATAL_ERROR "modeling-tools benchmark is missing required paths")
endif()

file(REMOVE_RECURSE "${OUTPUT_ROOT}")
file(MAKE_DIRECTORY "${OUTPUT_ROOT}")
execute_process(
    COMMAND "${ICAD_EXECUTABLE}" inspect-json "${ICAD_SOURCE}"
    RESULT_VARIABLE inspect_result
    OUTPUT_VARIABLE inspect_output
    ERROR_VARIABLE inspect_error
)
if(NOT inspect_result EQUAL 0 OR NOT inspect_output MATCHES "\"modelingOperations\":4" OR
   NOT inspect_output MATCHES "\"solids\":8" OR
   NOT inspect_output MATCHES "native CHAMFER" OR NOT inspect_output MATCHES "native FILLET")
    message(FATAL_ERROR "modeling-tools inspection mismatch: ${inspect_error}${inspect_output}")
endif()

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" build "${ICAD_SOURCE}" --output-dir "${OUTPUT_ROOT}"
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_output
    ERROR_VARIABLE build_error
)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "modeling-tools build failed: ${build_error}${build_output}")
endif()

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" inspect-step "${OUTPUT_ROOT}/modeling_tools.step"
    RESULT_VARIABLE step_result
    OUTPUT_VARIABLE step_output
    ERROR_VARIABLE step_error
)
if(NOT step_result EQUAL 0 OR NOT step_output MATCHES "STEP_SOLIDS 8")
    message(FATAL_ERROR "modeling-tools STEP read-back mismatch: ${step_error}${step_output}")
endif()
execute_process(
    COMMAND "${ICAD_EXECUTABLE}" inspect-stl "${OUTPUT_ROOT}/modeling_tools.stl"
    RESULT_VARIABLE stl_result
    OUTPUT_VARIABLE stl_output
    ERROR_VARIABLE stl_error
)
if(NOT stl_result EQUAL 0 OR NOT stl_output MATCHES "STL_SOLIDS 8")
    message(FATAL_ERROR "modeling-tools STL read-back mismatch: ${stl_error}${stl_output}")
endif()

message(STATUS "modeling tools passed: semantic selection, 8 solids, STEP/STL read-back")
