if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED ICAD_EXECUTABLE OR
   NOT DEFINED EXPECTED_PROJECT_VERSION OR NOT DEFINED EXPECTED_RELEASE_VERSION)
    message(FATAL_ERROR "release metadata test is missing required values")
endif()
execute_process(COMMAND "${ICAD_EXECUTABLE}" --version
                RESULT_VARIABLE version_status OUTPUT_VARIABLE version_output
                ERROR_VARIABLE version_error OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT version_status EQUAL 0 OR
   NOT version_output STREQUAL "icad ${EXPECTED_RELEASE_VERSION}")
    message(FATAL_ERROR "compiler reports '${version_output}${version_error}', expected icad ${EXPECTED_RELEASE_VERSION}")
endif()
foreach(relative IN ITEMS "src/cli/main.cpp" "src/mcp/server.cpp")
    file(READ "${PROJECT_ROOT}/${relative}" content)
    if(NOT content MATCHES "ICAD_VERSION")
        message(FATAL_ERROR "${relative} does not use generated release metadata")
    endif()
endforeach()
foreach(relative IN ITEMS "editors/vscode/package.json" "editors/vscode/package-lock.json")
    file(READ "${PROJECT_ROOT}/${relative}" content)
    if(NOT content MATCHES "${EXPECTED_PROJECT_VERSION}")
        message(FATAL_ERROR "${relative} does not contain project version ${EXPECTED_PROJECT_VERSION}")
    endif()
endforeach()
file(READ "${PROJECT_ROOT}/.github/workflows/release.yml" release_workflow)
if(NOT release_workflow MATCHES "zip.sha256" OR
   NOT release_workflow MATCHES "release_tag" OR
   NOT release_workflow MATCHES "package_version")
    message(FATAL_ERROR "release workflow does not publish checksummed, generated-version assets")
endif()
file(READ "${PROJECT_ROOT}/editors/vscode/toolchain.js" toolchain_source)
if(NOT toolchain_source MATCHES "zip.sha256" OR
   NOT toolchain_source MATCHES "icad-macos-arm64" OR
   NOT toolchain_source MATCHES "icad-windows-x86_64")
    message(FATAL_ERROR "VS Code managed toolchain does not match release assets")
endif()
message(STATUS "release metadata agrees on ${EXPECTED_RELEASE_VERSION}")
