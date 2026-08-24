if(NOT DEFINED ICAD_EXECUTABLE OR NOT DEFINED ICAD_SOURCE OR NOT DEFINED REFERENCE_ROOT OR
   NOT DEFINED OUTPUT_ROOT)
    message(FATAL_ERROR "robotic-arm benchmark is missing required paths")
endif()

file(GLOB reference_stls "${REFERENCE_ROOT}/*.STL")
list(LENGTH reference_stls reference_component_files)
if(NOT reference_component_files EQUAL 10)
    message(FATAL_ERROR "expected 10 reference STL component files, found ${reference_component_files}")
endif()
set(reference_facets 0)
foreach(stl IN LISTS reference_stls)
    file(STRINGS "${stl}" facets REGEX "^[ \\t]*facet normal")
    list(LENGTH facets facet_count)
    math(EXPR reference_facets "${reference_facets} + ${facet_count}")
endforeach()
if(NOT reference_facets EQUAL 23314)
    message(FATAL_ERROR "reference STL facet baseline changed: ${reference_facets}")
endif()

set(reference_step "${REFERENCE_ROOT}/Robotic Arm 3D Model.STEP")
execute_process(
    COMMAND "${ICAD_EXECUTABLE}" inspect-step "${reference_step}"
    RESULT_VARIABLE reference_step_result
    OUTPUT_VARIABLE reference_step_output
    ERROR_VARIABLE reference_step_error
)
if(NOT reference_step_result EQUAL 0 OR
   NOT reference_step_output MATCHES "STEP_SOLIDS 20" OR
   NOT reference_step_output MATCHES "STEP_ASSEMBLY_COMPONENTS 21")
    message(FATAL_ERROR "reference STEP baseline was not recognized: ${reference_step_error}${reference_step_output}")
endif()

file(REMOVE_RECURSE "${OUTPUT_ROOT}")
file(MAKE_DIRECTORY "${OUTPUT_ROOT}")
execute_process(
    COMMAND "${ICAD_EXECUTABLE}" inspect "${ICAD_SOURCE}"
    RESULT_VARIABLE inspect_result
    OUTPUT_VARIABLE inspect_output
    ERROR_VARIABLE inspect_error
)
if(NOT inspect_result EQUAL 0)
    message(FATAL_ERROR "robotic_arm.icad did not compile: ${inspect_error}")
endif()
foreach(expected IN ITEMS "PARAMETERS 7" "ANGLES 1" "POINTS3 12" "VECTORS 7" "POSES 1"
                          "JOINTS 10" "MATERIALS 3" "PROFILES 7" "BODIES 10"
                          "PROFILE_SEGMENTS 110" "CURVED_PROFILE_SEGMENTS 0"
                          "FEATURES 25" "CONSTRAINTS 3" "SCENES 1" "ANIMATION_TRACKS 9"
                          "KEYFRAMES 27")
    if(NOT inspect_output MATCHES "${expected}")
        message(FATAL_ERROR "robotic arm is missing metric ${expected}: ${inspect_output}")
    endif()
endforeach()

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" visual-json "${ICAD_SOURCE}"
    RESULT_VARIABLE visual_result
    OUTPUT_VARIABLE visual_output
    ERROR_VARIABLE visual_error
)
foreach(expected IN ITEMS "\"schema\":\"icad.visual.snapshot.v1\""
                          "\"name\":\"front\"" "\"name\":\"right\""
                          "\"name\":\"top\"" "\"name\":\"isometric\""
                          "\"body\":\"upper_arm\",\"parts\":2,\"triangles\":164"
                          "\"body\":\"forearm\",\"parts\":2,\"triangles\":172"
                          "\"body\":\"drive_gear\",\"parts\":2,\"triangles\":220"
                          "\"body\":\"follower_gear\",\"parts\":2,\"triangles\":220"
                          "\"attachmentSummary\":\{\"checkedJoints\":9,\"disconnectedJoints\":0"
                          "\"timeSeconds\":4,\"disconnectedJoints\":0,\"rootMaxDisplacementMm\":0")
    if(NOT visual_result EQUAL 0 OR NOT visual_output MATCHES "${expected}")
        message(FATAL_ERROR "robotic arm visual acceptance is missing ${expected}: ${visual_error}${visual_output}")
    endif()
endforeach()

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" inspect-json "${ICAD_SOURCE}"
    RESULT_VARIABLE agent_result
    OUTPUT_VARIABLE agent_output
    ERROR_VARIABLE agent_error
)
if(NOT agent_result EQUAL 0 OR NOT agent_output MATCHES "\"degreesOfFreedom\":9" OR
   NOT agent_output MATCHES "\"name\":\"elbow_hinge\",\"type\":\"revolute\"" OR
   NOT agent_output MATCHES "\"name\":\"upper_arm_axis\",\"kind\":\"betweenPoints\"" OR
   NOT agent_output MATCHES "\"from\":\"shoulder_center\",\"to\":\"elbow_center\"" OR
   NOT agent_output MATCHES "\"name\":\"tool_on_target\".*\"passed\":true" OR
   NOT agent_output MATCHES "\"name\":\"waist_motion\".*\"target\":\"waist_turn\"" OR
   NOT agent_output MATCHES "\"name\":\"shoulder_motion\".*\"target\":\"shoulder_hinge\"" OR
   NOT agent_output MATCHES "\"name\":\"wrist_pitch_motion\".*\"target\":\"wrist_pitch\"" OR
   NOT agent_output MATCHES "\"name\":\"drive_gear_motion\".*\"target\":\"drive_gear_joint\"" OR
   NOT agent_output MATCHES "\"name\":\"follower_gear_motion\".*\"target\":\"follower_gear_joint\"")
    message(FATAL_ERROR "robotic mechanism graph is not agent-readable: ${agent_error}${agent_output}")
endif()

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" build "${ICAD_SOURCE}" --output-dir "${OUTPUT_ROOT}"
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_output
    ERROR_VARIABLE build_error
)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "robotic arm build failed: ${build_error}")
endif()
foreach(output IN ITEMS robotic_arm.step robotic_arm.assembly.step robotic_arm.obj robotic_arm.stl
                        robotic_arm.scene.json
                        robotic_arm.bom.json robotic_arm.manufacturing.json robotic_arm.drawing.svg
                        robotic_arm.topology.json)
    if(NOT EXISTS "${OUTPUT_ROOT}/${output}")
        message(FATAL_ERROR "robotic arm build is missing ${output}")
    endif()
endforeach()

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" topology-json "${ICAD_SOURCE}"
    RESULT_VARIABLE topology_result
    OUTPUT_VARIABLE topology_output
    ERROR_VARIABLE topology_error
)
if(NOT topology_result EQUAL 0 OR NOT topology_output MATCHES
   "\\\"counts\\\":\\{\\\"solids\\\":25,\\\"vertices\\\":274,\\\"edges\\\":411,\\\"wires\\\":187,\\\"faces\\\":187\\}" OR
   NOT topology_output MATCHES "upper_arm/upper_arm_shell/face.side.1")
    message(FATAL_ERROR "robotic-arm exact topology mismatch: ${topology_error}${topology_output}")
endif()

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" inspect-step "${OUTPUT_ROOT}/robotic_arm.assembly.step"
    RESULT_VARIABLE assembly_result
    OUTPUT_VARIABLE assembly_output
    ERROR_VARIABLE assembly_error
)
if(NOT assembly_result EQUAL 0 OR NOT assembly_output MATCHES "STEP_SOLIDS 25" OR
   NOT assembly_output MATCHES "STEP_ASSEMBLY_COMPONENTS 10")
    message(FATAL_ERROR "generated robotic assembly mismatch: ${assembly_error}${assembly_output}")
endif()
execute_process(
    COMMAND "${ICAD_EXECUTABLE}" inspect-stl "${OUTPUT_ROOT}/robotic_arm.stl"
    RESULT_VARIABLE stl_result
    OUTPUT_VARIABLE stl_output
    ERROR_VARIABLE stl_error
)
if(NOT stl_result EQUAL 0 OR NOT stl_output MATCHES "STL_SOLIDS 25" OR
   NOT stl_output MATCHES "STL_FACETS 2368")
    message(FATAL_ERROR "generated robotic STL mismatch: ${stl_error}${stl_output}")
endif()

message(STATUS
    "robotic arm benchmark passed: reference=10 STL files/23314 facets/20 STEP solids; ICAD=10 components/25 solids/187 exact faces/2368 facets with toothed gears, mechanical gripper silhouettes, exact rest attachments, three connected animation samples, a stationary ground, four agent-readable views, and a fully animated 10-joint/9-DOF mechanism graph")
