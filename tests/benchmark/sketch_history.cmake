if(NOT DEFINED ICAD_EXECUTABLE OR NOT DEFINED ICAD_SOURCE OR NOT DEFINED OUTPUT_ROOT)
    message(FATAL_ERROR "sketch-history benchmark is missing required paths")
endif()

file(REMOVE_RECURSE "${OUTPUT_ROOT}")
file(MAKE_DIRECTORY "${OUTPUT_ROOT}")

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" visual-json "${ICAD_SOURCE}"
    RESULT_VARIABLE visual_result
    OUTPUT_VARIABLE visual_output
    ERROR_VARIABLE visual_error
)
foreach(expected IN ITEMS
        "\"body\":\"mounting_bracket\",\"parts\":1,\"triangles\":1492"
        "\"name\":\"base_solid\",\"command\":\"PAD\",\"type\":\"EXTRUDE\",\"operation\":\"NEW\""
        "\"name\":\"raised_boss\",\"command\":\"PAD\",\"type\":\"EXTRUDE\",\"operation\":\"ADD\""
        "\"supportFeature\":\"base_solid\",\"supportFace\":\"Z_MAX\""
        "\"name\":\"mounting_bore\",\"command\":\"POCKET\",\"type\":\"EXTRUDE\",\"operation\":\"CUT\"")
    if(NOT visual_result EQUAL 0 OR NOT visual_output MATCHES "${expected}")
        message(FATAL_ERROR "sketch-history visual contract is missing ${expected}: ${visual_error}${visual_output}")
    endif()
endforeach()

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" measure "${ICAD_SOURCE}"
    RESULT_VARIABLE measure_result
    OUTPUT_VARIABLE measure_output
    ERROR_VARIABLE measure_error
)
if(NOT measure_result EQUAL 0 OR
   NOT measure_output MATCHES "BOUNDS_MIN 0 0 0" OR
   NOT measure_output MATCHES "BOUNDS_MAX 100 60 32" OR
   NOT measure_output MATCHES "VOLUME_MM3 98004")
    message(FATAL_ERROR "sketch-history geometry lost its design envelope: ${measure_error}${measure_output}")
endif()

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" build "${ICAD_SOURCE}" --output-dir "${OUTPUT_ROOT}"
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_output
    ERROR_VARIABLE build_error
)
if(NOT build_result EQUAL 0 OR
   NOT build_output MATCHES "BUILD components=1 solids=1 vertices=748 triangles=1492")
    message(FATAL_ERROR "sketch-history build mismatch: ${build_error}${build_output}")
endif()

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" inspect-step "${OUTPUT_ROOT}/sketch_history.step"
    RESULT_VARIABLE step_result OUTPUT_VARIABLE step_output ERROR_VARIABLE step_error
)
if(NOT step_result EQUAL 0 OR NOT step_output MATCHES "STEP_SOLIDS 1")
    message(FATAL_ERROR "sketch-history STEP read-back mismatch: ${step_error}${step_output}")
endif()

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" inspect-stl "${OUTPUT_ROOT}/sketch_history.stl"
    RESULT_VARIABLE stl_result OUTPUT_VARIABLE stl_output ERROR_VARIABLE stl_error
)
if(NOT stl_result EQUAL 0 OR NOT stl_output MATCHES "STL_SOLIDS 1" OR
   NOT stl_output MATCHES "STL_FACETS 1492")
    message(FATAL_ERROR "sketch-history STL read-back mismatch: ${stl_error}${stl_output}")
endif()

message(STATUS "sketch history passed: ordered PAD/face PAD/POCKET, full envelope, STEP/STL read-back")
