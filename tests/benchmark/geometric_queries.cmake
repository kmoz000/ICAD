if(NOT DEFINED ICAD_EXECUTABLE OR NOT DEFINED ICAD_SOURCE)
    message(FATAL_ERROR "geometric-query benchmark is missing required paths")
endif()
execute_process(
    COMMAND "${ICAD_EXECUTABLE}" distance-json "${ICAD_SOURCE}" first_block second_block
    RESULT_VARIABLE distance_result OUTPUT_VARIABLE distance_output ERROR_VARIABLE distance_error
)
if(NOT distance_result EQUAL 0 OR
   NOT distance_output MATCHES "\"representation\":\"exactPolyhedral\"" OR
   NOT distance_output MATCHES "\"distanceMm\":10")
    message(FATAL_ERROR "distance query mismatch: ${distance_error}${distance_output}")
endif()
execute_process(
    COMMAND "${ICAD_EXECUTABLE}" section-json "${ICAD_SOURCE}" 5 0 0 1 0 0 first_block
    RESULT_VARIABLE section_result OUTPUT_VARIABLE section_output ERROR_VARIABLE section_error
)
if(NOT section_result EQUAL 0 OR
   NOT section_output MATCHES "\"representation\":\"polyhedralBoundary\"" OR
   NOT section_output MATCHES "\"toleranceMm\":0.001" OR
   NOT section_output MATCHES "\"body\":\"first_block\"")
    message(FATAL_ERROR "section query mismatch: ${section_error}${section_output}")
endif()
message(STATUS "geometric queries passed: 10 mm exact polyhedral distance and tolerance-aware section")
