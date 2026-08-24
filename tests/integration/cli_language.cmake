if(NOT DEFINED ICAD_EXECUTABLE)
    message(FATAL_ERROR "cli_language.cmake requires ICAD_EXECUTABLE")
endif()

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" language
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)

if(NOT result EQUAL 0)
    message(FATAL_ERROR "icad language failed with ${result}\nstdout: ${output}\nstderr: ${error}")
endif()
if(NOT output MATCHES "ICAD_LANGUAGE 1.0")
    message(FATAL_ERROR "language output omitted the production version: ${output}")
endif()
if(NOT output MATCHES "CAPABILITY CAPABILITY_NEGOTIATION" OR
   NOT output MATCHES "CAPABILITY PARAMETER_EXPRESSIONS_V1" OR
   NOT output MATCHES "CAPABILITY QUALIFIED_VALUE_REFERENCES_V1" OR
   NOT output MATCHES "CAPABILITY MULTI_SHAPE_SKETCH_V1" OR
   NOT output MATCHES "CAPABILITY SKETCH_REGION_ARRANGEMENT_V1" OR
   NOT output MATCHES "CAPABILITY ADVANCED_SKETCH_CONSTRAINTS_V1" OR
   NOT output MATCHES "CAPABILITY SKETCH_LINE_ARC_TANGENCY_V1" OR
   NOT output MATCHES "CAPABILITY SEMANTIC_EDGE_LOOP_SELECTION_V1" OR
   NOT output MATCHES "CAPABILITY TOPOLOGY_QUERY_V1" OR
   NOT output MATCHES "CAPABILITY PERSISTENT_FACE_REFERENCES_V1" OR
   NOT output MATCHES "CAPABILITY BODY_HISTORY")
    message(FATAL_ERROR "language output omitted implemented capabilities: ${output}")
endif()
if(output MATCHES "CAPABILITY MULTI_SHAPE_SKETCH[\r\n]")
    message(FATAL_ERROR "language output advertised proposed syntax: ${output}")
endif()
