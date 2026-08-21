if(NOT DEFINED ICAD_EXECUTABLE OR NOT DEFINED ICAD_SOURCE OR NOT DEFINED SANDBOX_ROOT)
    message(FATAL_ERROR "sandbox test requires ICAD_EXECUTABLE, ICAD_SOURCE, and SANDBOX_ROOT")
endif()

file(REMOVE_RECURSE "${SANDBOX_ROOT}")
file(MAKE_DIRECTORY "${SANDBOX_ROOT}/workspace")
file(COPY_FILE "${ICAD_SOURCE}" "${SANDBOX_ROOT}/workspace/model.icad" ONLY_IF_DIFFERENT)

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" check model.icad
    WORKING_DIRECTORY "${SANDBOX_ROOT}/workspace"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)

if(NOT result EQUAL 0)
    message(FATAL_ERROR "compiler failed in isolated workspace: ${result}\n${output}\n${error}")
endif()

if(NOT output MATCHES "model.icad: compile check passed")
    message(FATAL_ERROR "unexpected sandbox output: ${output}")
endif()

file(GLOB workspace_entries RELATIVE "${SANDBOX_ROOT}/workspace" "${SANDBOX_ROOT}/workspace/*")
if(NOT workspace_entries STREQUAL "model.icad")
    message(FATAL_ERROR "compiler wrote unexpected sandbox files: ${workspace_entries}")
endif()
