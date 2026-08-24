if(NOT DEFINED ICAD_EXECUTABLE OR NOT DEFINED ICAD_SOURCE OR NOT DEFINED OUTPUT_ROOT)
    message(FATAL_ERROR "persistent face benchmark is missing required paths")
endif()

file(REMOVE_RECURSE "${OUTPUT_ROOT}")
file(MAKE_DIRECTORY "${OUTPUT_ROOT}")

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" visual-json "${ICAD_SOURCE}"
    RESULT_VARIABLE visual_result OUTPUT_FILE "${OUTPUT_ROOT}/visual.json"
    ERROR_VARIABLE visual_error TIMEOUT 15
)
if(NOT visual_result EQUAL 0)
    message(FATAL_ERROR "persistent FACE visual-json failed: ${visual_error}")
endif()
file(READ "${OUTPUT_ROOT}/visual.json" visual_output)
foreach(expected IN ITEMS
        "\"name\":\"mounting_face\""
        "\"topologyId\":\"plate/plate_solid/face.top\""
        "\"supportReference\":\"mounting_face\""
        "\"supportTopologyId\":\"plate/boss_solid/face.top\"")
    if(NOT visual_output MATCHES "${expected}")
        message(FATAL_ERROR "persistent FACE visual contract is missing ${expected}")
    endif()
endforeach()

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" build "${ICAD_SOURCE}" --output-dir "${OUTPUT_ROOT}"
    RESULT_VARIABLE build_result OUTPUT_VARIABLE build_output ERROR_VARIABLE build_error
    TIMEOUT 45
)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "persistent FACE build failed: ${build_error}${build_output}")
endif()

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" inspect-step "${OUTPUT_ROOT}/persistent_face_plate.step"
    RESULT_VARIABLE step_result OUTPUT_VARIABLE step_output ERROR_VARIABLE step_error
)
if(NOT step_result EQUAL 0 OR NOT step_output MATCHES "STEP_SOLIDS 1")
    message(FATAL_ERROR "persistent FACE STEP read-back mismatch: ${step_error}${step_output}")
endif()

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" inspect-stl "${OUTPUT_ROOT}/persistent_face_plate.stl"
    RESULT_VARIABLE stl_result OUTPUT_VARIABLE stl_output ERROR_VARIABLE stl_error
)
if(NOT stl_result EQUAL 0 OR NOT stl_output MATCHES "STL_SOLIDS 1")
    message(FATAL_ERROR "persistent FACE STL read-back mismatch: ${stl_error}${stl_output}")
endif()

message(STATUS "persistent FACE aliases passed provenance, build, STEP, and STL read-back")
