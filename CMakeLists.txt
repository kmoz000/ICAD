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
option(ICAD_ENABLE_THREAD_SANITIZER "Enable ThreadSanitizer" OFF)
option(ICAD_BUILD_DESKTOP_VIEWER "Build the optional webview/webview desktop host" OFF)
find_program(ICAD_CHROMIUM_EXECUTABLE NAMES chromium chromium-browser google-chrome
             PATHS "/Applications/Chromium.app/Contents/MacOS")
set(ICAD_BROWSER_TEST_EXECUTABLE "")
if(BUILD_TESTING AND ICAD_CHROMIUM_EXECUTABLE)
    set(chromium_probe_root "${CMAKE_BINARY_DIR}/.chromium-probe")
    file(REMOVE_RECURSE "${chromium_probe_root}")
    execute_process(
        COMMAND "${ICAD_CHROMIUM_EXECUTABLE}" --headless --disable-gpu-sandbox
                --no-first-run --user-data-dir=${chromium_probe_root} --dump-dom about:blank
        RESULT_VARIABLE chromium_probe_result
        OUTPUT_QUIET
        ERROR_QUIET
        TIMEOUT 15
    )
    file(REMOVE_RECURSE "${chromium_probe_root}")
    if(chromium_probe_result EQUAL 0)
        set(ICAD_BROWSER_TEST_EXECUTABLE "${ICAD_CHROMIUM_EXECUTABLE}")
    else()
        message(STATUS
            "Chromium was found but cannot run headless; browser.viewer_smoke is disabled")
    endif()
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

file(READ "${PROJECT_SOURCE_DIR}/web/icad-viewer.js" ICAD_VIEWER_JS)
file(READ "${PROJECT_SOURCE_DIR}/assets/icons/icad-32.png" ICAD_ICON_PNG_HEX HEX)
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
if(ICAD_ENABLE_THREAD_SANITIZER)
    if(ICAD_ENABLE_SANITIZERS)
        message(FATAL_ERROR "ThreadSanitizer cannot be combined with ASan/UBSan")
    endif()
    icad_enable_thread_sanitizer(icad_project_options)
endif()

add_subdirectory(src)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

install(FILES README.md LICENSE DESTINATION share/icad)
install(FILES assets/icons/icad.ico assets/icons/icad.icns assets/icons/icad-256.png
        DESTINATION share/icad/icons)
install(FILES examples/advanced.icad examples/robotic_arm.icad examples/sketch_history.icad
        DESTINATION share/icad/examples)
