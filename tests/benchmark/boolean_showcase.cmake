if(NOT DEFINED ICAD_EXECUTABLE OR NOT DEFINED ICAD_SOURCE OR NOT DEFINED OUTPUT_ROOT)
    message(FATAL_ERROR "boolean benchmark is missing required paths")
endif()

file(REMOVE_RECURSE "${OUTPUT_ROOT}")
file(MAKE_DIRECTORY "${OUTPUT_ROOT}")

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" inspect-json "${ICAD_SOURCE}"
    RESULT_VARIABLE inspect_result
    OUTPUT_VARIABLE inspect_output
    ERROR_VARIABLE inspect_error
)
if(NOT inspect_result EQUAL 0 OR NOT inspect_output MATCHES "\"booleanOperations\":3" OR
   NOT inspect_output MATCHES "\"volumeMm3\":2840" OR
   NOT inspect_output MATCHES "conforming boolean boundary splits")
    message(FATAL_ERROR "boolean inspection mismatch: ${inspect_error}${inspect_output}")
endif()

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" build "${ICAD_SOURCE}" --output-dir "${OUTPUT_ROOT}"
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_output
    ERROR_VARIABLE build_error
)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "boolean artifact build failed: ${build_error}${build_output}")
endif()

foreach(extension IN ITEMS step assembly.step stl obj html scene.json bom.json
                           manufacturing.json drawing.svg topology.json)
    if(NOT EXISTS "${OUTPUT_ROOT}/boolean_showcase.${extension}")
        message(FATAL_ERROR "boolean build omitted boolean_showcase.${extension}")
    endif()
endforeach()

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" inspect-step "${OUTPUT_ROOT}/boolean_showcase.step"
    RESULT_VARIABLE step_result
    OUTPUT_VARIABLE step_output
    ERROR_VARIABLE step_error
)
if(NOT step_result EQUAL 0 OR NOT step_output MATCHES "STEP_SOLIDS 3")
    message(FATAL_ERROR "boolean STEP read-back mismatch: ${step_error}${step_output}")
endif()

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" inspect-stl "${OUTPUT_ROOT}/boolean_showcase.stl"
    RESULT_VARIABLE stl_result
    OUTPUT_VARIABLE stl_output
    ERROR_VARIABLE stl_error
)
if(NOT stl_result EQUAL 0 OR NOT stl_output MATCHES "STL_SOLIDS 3")
    message(FATAL_ERROR "boolean STL read-back mismatch: ${stl_error}${stl_output}")
endif()

file(READ "${OUTPUT_ROOT}/boolean_showcase.topology.json" topology)
if(NOT topology MATCHES "\"valid\":true" OR NOT topology MATCHES "\"solids\":3")
    message(FATAL_ERROR "boolean topology artifact is invalid")
endif()

message(STATUS "boolean showcase passed: 3 operations, 2840 mm3, closed topology, STEP/STL read-back")
