cmake_minimum_required(VERSION 3.25)

project(
    ICAD
    VERSION 0.21.0
    DESCRIPTION "Compiler for the ICAD product-definition language"
    LANGUAGES CXX
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CTest)
include(CompilerWarnings)
include(Sanitizers)

option(ICAD_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(ICAD_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(ICAD_BUILD_DESKTOP_VIEWER "Build the optional webview/webview desktop host" OFF)
find_program(ICAD_CHROMIUM_EXECUTABLE NAMES chromium chromium-browser google-chrome
             PATHS "/Applications/Chromium.app/Contents/MacOS")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

file(READ "${PROJECT_SOURCE_DIR}/web/icad-viewer.js" ICAD_VIEWER_JS)
file(READ "${PROJECT_SOURCE_DIR}/examples/robotic_arm.icad" ICAD_ROBOTIC_ARM_TEMPLATE)
file(READ "${PROJECT_SOURCE_DIR}/examples/advanced.icad" ICAD_BRIDGE_TEMPLATE)
configure_file(
    "${PROJECT_SOURCE_DIR}/cmake/viewer_source.hpp.in"
    "${PROJECT_BINARY_DIR}/generated/icad/viewer_source.hpp"
    @ONLY
)
configure_file(
    "${PROJECT_SOURCE_DIR}/cmake/agent_templates.hpp.in"
    "${PROJECT_BINARY_DIR}/generated/icad/agent_templates.hpp"
    @ONLY
)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

add_library(icad_project_options INTERFACE)
target_compile_features(icad_project_options INTERFACE cxx_std_23)
icad_set_project_warnings(icad_project_options ${ICAD_WARNINGS_AS_ERRORS})

if(ICAD_ENABLE_SANITIZERS)
    icad_enable_sanitizers(icad_project_options)
endif()

add_subdirectory(src)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

install(FILES README.md LICENSE DESTINATION share/icad)
install(FILES examples/advanced.icad examples/robotic_arm.icad
        DESTINATION share/icad/examples)
