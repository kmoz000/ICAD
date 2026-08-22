if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED EXPECTED_VERSION)
    message(FATAL_ERROR "release metadata test is missing required values")
endif()
set(version_files
    "src/cli/main.cpp"
    "src/mcp/server.cpp"
    "editors/vscode/package.json"
    "editors/vscode/package-lock.json"
)
foreach(relative IN LISTS version_files)
    file(READ "${PROJECT_ROOT}/${relative}" content)
    if(NOT content MATCHES "${EXPECTED_VERSION}")
        message(FATAL_ERROR "${relative} does not contain release version ${EXPECTED_VERSION}")
    endif()
endforeach()
file(READ "${PROJECT_ROOT}/.github/workflows/release.yml" release_workflow)
if(NOT release_workflow MATCHES "zip.sha256")
    message(FATAL_ERROR "release workflow does not publish native archive checksums")
endif()
file(READ "${PROJECT_ROOT}/editors/vscode/toolchain.js" toolchain_source)
if(NOT toolchain_source MATCHES "zip.sha256" OR
   NOT toolchain_source MATCHES "icad-macos-arm64" OR
   NOT toolchain_source MATCHES "icad-windows-x86_64")
    message(FATAL_ERROR "VS Code managed toolchain does not match release assets")
endif()
message(STATUS "release metadata agrees on ${EXPECTED_VERSION}")
