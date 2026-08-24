if(NOT DEFINED ICAD_EXECUTABLE OR NOT DEFINED ICAD_SOURCE OR NOT DEFINED OUTPUT_ROOT)
    message(FATAL_ERROR "region plate benchmark is missing required paths")
endif()

file(REMOVE_RECURSE "${OUTPUT_ROOT}")
file(MAKE_DIRECTORY "${OUTPUT_ROOT}")

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" visual-json "${ICAD_SOURCE}"
    RESULT_VARIABLE visual_result OUTPUT_FILE "${OUTPUT_ROOT}/visual.json"
    ERROR_VARIABLE visual_error TIMEOUT 15
)
if(NOT visual_result EQUAL 0)
    message(FATAL_ERROR "REGION visual-json failed: ${visual_error}")
endif()
file(READ "${OUTPUT_ROOT}/visual.json" visual_output)
foreach(expected IN ITEMS
        "\"name\":\"perforated_plate\""
        "\"outer\":\"outer\""
        "\"holes\":"
        "\"hole_left\""
        "\"hole_right\""
        "\"regionHoleProfiles\":2")
    if(NOT visual_output MATCHES "${expected}")
        message(FATAL_ERROR "REGION visual contract is missing ${expected}")
    endif()
endforeach()

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" measure "${ICAD_SOURCE}"
    RESULT_VARIABLE measure_result OUTPUT_VARIABLE measure_output ERROR_VARIABLE measure_error
    TIMEOUT 15
)
if(NOT measure_result EQUAL 0 OR
   NOT measure_output MATCHES "BOUNDS_MIN 0 0 0" OR
   NOT measure_output MATCHES "BOUNDS_MAX 100 60 8" OR
   NOT measure_output MATCHES "VOLUME_MM3 46743")
    message(FATAL_ERROR "REGION plate geometry mismatch: ${measure_error}${measure_output}")
endif()

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" build "${ICAD_SOURCE}" --output-dir "${OUTPUT_ROOT}"
    RESULT_VARIABLE build_result OUTPUT_VARIABLE build_output ERROR_VARIABLE build_error
    TIMEOUT 20
)
if(NOT build_result EQUAL 0 OR
   NOT build_output MATCHES "BUILD components=1 solids=1 vertices=687 triangles=1378")
    message(FATAL_ERROR "REGION plate build mismatch: ${build_error}${build_output}")
endif()

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" inspect-step "${OUTPUT_ROOT}/region_plate.step"
    RESULT_VARIABLE step_result OUTPUT_VARIABLE step_output ERROR_VARIABLE step_error
)
if(NOT step_result EQUAL 0 OR NOT step_output MATCHES "STEP_SOLIDS 1")
    message(FATAL_ERROR "REGION STEP read-back mismatch: ${step_error}${step_output}")
endif()

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" inspect-stl "${OUTPUT_ROOT}/region_plate.stl"
    RESULT_VARIABLE stl_result OUTPUT_VARIABLE stl_output ERROR_VARIABLE stl_error
)
if(NOT stl_result EQUAL 0 OR NOT stl_output MATCHES "STL_SOLIDS 1" OR
   NOT stl_output MATCHES "STL_FACETS 1378")
    message(FATAL_ERROR "REGION STL read-back mismatch: ${stl_error}${stl_output}")
endif()

message(STATUS "explicit REGION passed visual, volume, STEP, and STL read-back")
