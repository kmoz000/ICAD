if(NOT DEFINED ICAD_EXECUTABLE OR NOT DEFINED ICAD_SOURCE OR NOT DEFINED OUTPUT_ROOT)
    message(FATAL_ERROR "advanced benchmark is missing required paths")
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
    message(FATAL_ERROR "advanced.icad did not compile: ${inspect_error}")
endif()
foreach(expected IN ITEMS "PARAMETERS 6" "MATERIALS 3" "BODIES 5" "FEATURES 35"
                          "PROPERTIES 206" "SCENES 1" "ANIMATION_TRACKS 2" "KEYFRAMES 5")
    if(NOT inspect_output MATCHES "${expected}")
        message(FATAL_ERROR "advanced model is missing metric ${expected}: ${inspect_output}")
    endif()
endforeach()

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" build "${ICAD_SOURCE}" --output-dir "${OUTPUT_ROOT}"
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_output
    ERROR_VARIABLE build_error
)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "advanced export failed: ${build_error}")
endif()

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" topology-json "${ICAD_SOURCE}"
    RESULT_VARIABLE topology_result
    OUTPUT_VARIABLE topology_output
    ERROR_VARIABLE topology_error
)
if(NOT topology_result EQUAL 0 OR NOT topology_output MATCHES
   "\\\"counts\\\":\\{\\\"solids\\\":35,\\\"vertices\\\":250,\\\"edges\\\":375,\\\"wires\\\":195,\\\"faces\\\":195\\}")
    message(FATAL_ERROR "advanced exact topology mismatch: ${topology_error}${topology_output}")
endif()

set(obj "${OUTPUT_ROOT}/advanced.obj")
set(step "${OUTPUT_ROOT}/advanced.step")
set(assembly_step "${OUTPUT_ROOT}/advanced.assembly.step")
set(stl "${OUTPUT_ROOT}/advanced.stl")
set(gltf "${OUTPUT_ROOT}/advanced.gltf")
set(glb "${OUTPUT_ROOT}/advanced.glb")
set(three_mf "${OUTPUT_ROOT}/advanced.3mf")
set(scene "${OUTPUT_ROOT}/advanced.scene.json")
set(viewer "${OUTPUT_ROOT}/advanced.html")
set(bom "${OUTPUT_ROOT}/advanced.bom.json")
set(manufacturing "${OUTPUT_ROOT}/advanced.manufacturing.json")
set(drawing "${OUTPUT_ROOT}/advanced.drawing.svg")
set(drawing_dxf "${OUTPUT_ROOT}/advanced.drawing.dxf")
set(topology "${OUTPUT_ROOT}/advanced.topology.json")
if(NOT EXISTS "${obj}" OR NOT EXISTS "${step}" OR NOT EXISTS "${assembly_step}" OR
   NOT EXISTS "${stl}" OR NOT EXISTS "${scene}" OR NOT EXISTS "${viewer}" OR
   NOT EXISTS "${gltf}" OR NOT EXISTS "${glb}" OR NOT EXISTS "${three_mf}" OR
   NOT EXISTS "${bom}" OR NOT EXISTS "${manufacturing}" OR NOT EXISTS "${drawing}" OR
   NOT EXISTS "${drawing_dxf}" OR NOT EXISTS "${topology}")
    message(FATAL_ERROR "advanced build did not produce the complete geometry and engineering package")
endif()

file(STRINGS "${obj}" obj_objects REGEX "^o ")
file(STRINGS "${obj}" obj_vertices REGEX "^v ")
file(STRINGS "${obj}" obj_faces REGEX "^f ")
list(LENGTH obj_objects object_count)
list(LENGTH obj_vertices vertex_count)
list(LENGTH obj_faces face_count)
if(NOT object_count EQUAL 35 OR vertex_count LESS 35 OR face_count LESS 35)
    message(FATAL_ERROR
        "OBJ topology mismatch: objects=${object_count} vertices=${vertex_count} faces=${face_count}")
endif()

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" inspect-step "${step}"
    RESULT_VARIABLE step_result
    OUTPUT_VARIABLE step_output
    ERROR_VARIABLE step_error
)
if(NOT step_result EQUAL 0)
    message(FATAL_ERROR "ICAD could not structurally validate generated STEP: ${step_error}")
endif()
foreach(expected IN ITEMS "STEP_ROOTS 35" "STEP_SOLIDS 35")
    if(NOT step_output MATCHES "${expected}")
        message(FATAL_ERROR "STEP read-back mismatch for ${expected}: ${step_output}")
    endif()
endforeach()
foreach(entity IN ITEMS "ADVANCED_FACE" "EDGE_CURVE" "CYLINDRICAL_SURFACE" "PLANE")
    file(STRINGS "${step}" step_entities REGEX "${entity}")
    if(NOT step_entities)
        message(FATAL_ERROR "analytic STEP output is missing ${entity}")
    endif()
endforeach()

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" inspect-step "${assembly_step}"
    RESULT_VARIABLE assembly_result
    OUTPUT_VARIABLE assembly_output
    ERROR_VARIABLE assembly_error
)
if(NOT assembly_result EQUAL 0 OR NOT assembly_output MATCHES "STEP_ASSEMBLY_COMPONENTS 5")
    message(FATAL_ERROR "bridge STEP assembly mismatch: ${assembly_error}${assembly_output}")
endif()

foreach(mesh_package IN ITEMS "${gltf}" "${glb}")
    execute_process(
        COMMAND "${ICAD_EXECUTABLE}" inspect-gltf "${mesh_package}"
        RESULT_VARIABLE gltf_result
        OUTPUT_VARIABLE gltf_output
        ERROR_VARIABLE gltf_error
    )
    if(NOT gltf_result EQUAL 0 OR NOT gltf_output MATCHES "GLTF_OBJECTS 35")
        message(FATAL_ERROR "glTF package mismatch: ${gltf_error}${gltf_output}")
    endif()
endforeach()
execute_process(
    COMMAND "${ICAD_EXECUTABLE}" inspect-3mf "${three_mf}"
    RESULT_VARIABLE three_mf_result
    OUTPUT_VARIABLE three_mf_output
    ERROR_VARIABLE three_mf_error
)
if(NOT three_mf_result EQUAL 0 OR NOT three_mf_output MATCHES "THREEMF_OBJECTS 35")
    message(FATAL_ERROR "3MF package mismatch: ${three_mf_error}${three_mf_output}")
endif()
execute_process(
    COMMAND "${ICAD_EXECUTABLE}" inspect-dxf "${drawing_dxf}"
    RESULT_VARIABLE dxf_result
    OUTPUT_VARIABLE dxf_output
    ERROR_VARIABLE dxf_error
)
if(NOT dxf_result EQUAL 0 OR NOT dxf_output MATCHES "DXF_VALID 1")
    message(FATAL_ERROR "DXF package mismatch: ${dxf_error}${dxf_output}")
endif()
execute_process(
    COMMAND "${ICAD_EXECUTABLE}" inspect-stl "${stl}"
    RESULT_VARIABLE stl_result
    OUTPUT_VARIABLE stl_output
    ERROR_VARIABLE stl_error
)
if(NOT stl_result EQUAL 0 OR NOT stl_output MATCHES "STL_SOLIDS 35" OR
   NOT stl_output MATCHES "STL_FACETS 1000")
    message(FATAL_ERROR "bridge STL mismatch: ${stl_error}${stl_output}")
endif()

file(READ "${scene}" scene_content)
foreach(expected IN ITEMS "\"preset\":\"CONCRETE\"" "\"preset\":\"STRUCTURAL_STEEL\""
                          "\"preset\":\"ASPHALT\"" "data:image/bmp;base64,"
                          "\"textureScaleMm\":800" "\"easing\":\"EASE_IN_OUT\""
                          "\"name\":\"service_apex\"" "\"name\":\"inspection_flythrough\"")
    if(NOT scene_content MATCHES "${expected}")
        message(FATAL_ERROR "scene output is missing ${expected}")
    endif()
endforeach()

message(STATUS
    "advanced bridge benchmark passed: 35 native solids, analytic STEP, STL/OBJ/glTF/GLB/3MF, DXF, and animated web scene")
