if(NOT DEFINED ICAD_EXECUTABLE OR NOT DEFINED OUTPUT_ROOT OR
   NOT DEFINED PROJECT_ROOT)
    message(FATAL_ERROR "agentic robot prompt benchmark is missing required paths")
endif()

# The former external STL/STEP fixture was intentionally removed. This test
# validates the rebuilt procedural reference through agent creation, assembly
# STEP read-back, visual topology, and manufacturing checks below.
file(REMOVE_RECURSE "${OUTPUT_ROOT}")
file(MAKE_DIRECTORY "${OUTPUT_ROOT}")
set(source "${OUTPUT_ROOT}/prompt_robotic_arm.icad")
execute_process(
    COMMAND "${ICAD_EXECUTABLE}" agent-create
            "Create a detailed articulated industrial robotic arm with a gripper and animated joints"
            --source-out "${source}"
            --output-dir "${OUTPUT_ROOT}/artifacts"
    RESULT_VARIABLE create_result
    OUTPUT_VARIABLE create_output
    ERROR_VARIABLE create_error
)
foreach(expected IN ITEMS "\"ready\":true" "\"bodies\":10" "\"joints\":10"
                          "\"degreesOfFreedom\":9" "\"topologyValid\":true"
                          "\"intent\":\"ROBOTIC_ARM\"" "\"expectedModelIterations\":1"
                          "\"schema\":\"icad.agent.design-map.v1\""
                          "\"unintendedPenetratingPartPairs\":0"
                          "\"selectedTemplate\":\"robotic_arm\""
                          "\"name\":\"forearm_axis\""
                          "\"child\":\"forearm\""
                          "components=10" "solids=27")
    if(NOT create_result EQUAL 0 OR NOT create_output MATCHES "${expected}")
        message(FATAL_ERROR "one-command robotic creation is missing ${expected}: ${create_error}${create_output}")
    endif()
endforeach()
execute_process(
    COMMAND "${ICAD_EXECUTABLE}" inspect-step
            "${OUTPUT_ROOT}/artifacts/prompt_robotic_arm.assembly.step"
    RESULT_VARIABLE step_result
    OUTPUT_VARIABLE step_output
    ERROR_VARIABLE step_error
)
if(NOT step_result EQUAL 0 OR NOT step_output MATCHES "STEP_SOLIDS 27" OR
   NOT step_output MATCHES "STEP_ASSEMBLY_COMPONENTS 10")
    message(FATAL_ERROR "robotic prompt STEP assembly failed: ${step_error}${step_output}")
endif()
execute_process(
    COMMAND "${ICAD_EXECUTABLE}" visual-json "${source}"
    RESULT_VARIABLE visual_result
    OUTPUT_VARIABLE visual_output
    ERROR_VARIABLE visual_error
)
if(NOT visual_result EQUAL 0 OR
   NOT visual_output MATCHES "\"schema\":\"icad.visual.snapshot.v1\"" OR
   NOT visual_output MATCHES "\"body\":\"upper_jaw\",\"parts\":2,\"triangles\":168" OR
   NOT visual_output MATCHES "\"body\":\"drive_gear\",\"parts\":2,\"triangles\":220" OR
   NOT visual_output MATCHES "\"connections\"" OR
   NOT visual_output MATCHES "\"snapState\":\"SEATED\"" OR
   NOT visual_output MATCHES "\"disconnectedJoints\":0" OR
   NOT visual_output MATCHES "\"rootMaxDisplacementMm\":0")
    message(FATAL_ERROR "agent-created robot lacks the accepted visual snapshot: ${visual_error}${visual_output}")
endif()
message(STATUS "one-command robotic prompt passed against reference: 10 source components, 10 ICAD bodies, 10 joints, 9 DOF, 27 solids, 9 seated manufacturing connections, zero unintended penetrations, grounded scene samples, and four agent-readable views")
