add_library(icad_compiler
    agent/workflow.cpp
    json/value.cpp
    project/builder.cpp
    mcp/server.cpp
    compiler/compiler.cpp
    compiler/dependency_graph.cpp
    compiler/incremental.cpp
    compiler/importer.cpp
    compiler/language.cpp
    compiler/lexer/lexer.cpp
    compiler/parser/parser.cpp
    compiler/resolver/resolver.cpp
    compiler/semantic/semantic.cpp
    compiler/types/types.cpp
    compiler/units/units.cpp
    materials/library.cpp
    cad/model.cpp
    cad/boolean.cpp
    cad/topology.cpp
    cad/analysis.cpp
    cad/intersection.cpp
    cad/queries.cpp
    constraints/validator.cpp
    constraints/sketch_solver.cpp
    ai/inspector.cpp
    lsp/server.cpp
    document/revision.cpp
    document/source.cpp
    document/exporter.cpp
    manufacturing/validator.cpp
    drawings/exporter.cpp
    scene/exporter.cpp
    scene/evaluator.cpp
    viewer/live_session.cpp
    exchange/exporter.cpp
    exchange/obj_exporter.cpp
    exchange/gltf_exporter.cpp
    exchange/step_exporter.cpp
    exchange/stl_exporter.cpp
    exchange/three_mf_exporter.cpp
)
add_library(ICAD::compiler ALIAS icad_compiler)
set_target_properties(icad_compiler PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(icad_compiler
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_include_directories(icad_compiler PRIVATE "${PROJECT_BINARY_DIR}/generated")
target_link_libraries(icad_compiler PUBLIC icad_project_options)

add_library(icad_engine SHARED engine/session.cpp engine/c_api.cpp)
add_library(ICAD::engine ALIAS icad_engine)
target_compile_definitions(icad_engine PRIVATE ICAD_ENGINE_BUILD)
target_include_directories(icad_engine
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(icad_engine PRIVATE ICAD::compiler icad_project_options)
set_target_properties(icad_engine PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

add_executable(icad cli/main.cpp)
target_link_libraries(icad PRIVATE ICAD::compiler icad_project_options)
install(TARGETS icad icad_engine
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/icad DESTINATION include)

if(ICAD_BUILD_DESKTOP_VIEWER)
    include(FetchContent)
    set(WEBVIEW_BUILD_SHARED_LIBRARY OFF CACHE BOOL "Build webview shared library" FORCE)
    set(WEBVIEW_BUILD_STATIC_LIBRARY ON CACHE BOOL "Build webview static library" FORCE)
    set(WEBVIEW_BUILD_TESTS OFF CACHE BOOL "Build webview tests" FORCE)
    set(WEBVIEW_BUILD_EXAMPLES OFF CACHE BOOL "Build webview examples" FORCE)
    FetchContent_Declare(
        webview
        GIT_REPOSITORY https://github.com/webview/webview.git
        GIT_TAG 0.12.0
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(webview)
    set_target_properties(webview_core_static PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)

    add_executable(icad-viewer desktop_viewer/main.cpp)
    target_include_directories(icad-viewer PRIVATE "${PROJECT_BINARY_DIR}/generated")
    target_link_libraries(icad-viewer PRIVATE ICAD::engine ICAD::compiler webview::core_static)
    set_target_properties(icad-viewer PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
    install(TARGETS icad-viewer RUNTIME DESTINATION bin)
endif()
