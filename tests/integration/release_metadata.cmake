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
message(STATUS "release metadata agrees on ${EXPECTED_VERSION}")
