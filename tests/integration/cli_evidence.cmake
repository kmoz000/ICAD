if(NOT DEFINED ICAD_EXECUTABLE OR NOT DEFINED ICAD_SOURCE OR
   NOT DEFINED ICAD_MANIFEST OR NOT DEFINED OUTPUT_ROOT)
    message(FATAL_ERROR "ICAD_EXECUTABLE, ICAD_SOURCE, ICAD_MANIFEST, and OUTPUT_ROOT are required")
endif()

file(MAKE_DIRECTORY "${OUTPUT_ROOT}")

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" evidence-json "${ICAD_SOURCE}"
            --manifest "${ICAD_MANIFEST}"
    RESULT_VARIABLE evidence_result
    OUTPUT_VARIABLE evidence_output
    ERROR_VARIABLE evidence_error
)
if(NOT evidence_result EQUAL 0)
    message(FATAL_ERROR "evidence-json failed: ${evidence_error}")
endif()
foreach(expected IN ITEMS
        "\"schema\":\"icad.evidence.manifest.v1\""
        "\"manifestValid\":true"
        "\"releaseReady\":false"
        "\"lifecycleState\":\"DEVELOPMENT\"")
    string(FIND "${evidence_output}" "${expected}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "evidence-json did not contain ${expected}")
    endif()
endforeach()

execute_process(
    COMMAND "${ICAD_EXECUTABLE}" compliance-json "${ICAD_SOURCE}"
            --manifest "${ICAD_MANIFEST}" --basis EASA-CS-E-AMENDMENT-8
    RESULT_VARIABLE compliance_result
    OUTPUT_VARIABLE compliance_output
    ERROR_VARIABLE compliance_error
)
if(NOT compliance_result EQUAL 0)
    message(FATAL_ERROR "compliance-json failed: ${compliance_error}")
endif()
foreach(expected IN ITEMS
        "\"schema\":\"icad.compliance.v1\""
        "\"blockingHazards\":8"
        "\"openRequirements\":12"
        "\"openApplicableCompliance\":9")
    string(FIND "${compliance_output}" "${expected}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "compliance-json did not contain ${expected}")
    endif()
endforeach()

set(report_path "${OUTPUT_ROOT}/turbojet-compliance.html")
execute_process(
    COMMAND "${ICAD_EXECUTABLE}" compliance-report "${ICAD_SOURCE}"
            --manifest "${ICAD_MANIFEST}" --format html --output "${report_path}"
    RESULT_VARIABLE report_result
    ERROR_VARIABLE report_error
)
if(NOT report_result EQUAL 0)
    message(FATAL_ERROR "compliance-report failed: ${report_error}")
endif()
file(READ "${report_path}" report_output)
string(FIND "${report_output}" "Ground-test release blocked" report_status)
string(FIND "${report_output}" "CS-E applicability and compliance" compliance_table)
string(FIND "${report_output}" "TGD-REQ-ROTOR-001" requirement_table)
if(report_status EQUAL -1 OR compliance_table EQUAL -1 OR requirement_table EQUAL -1)
    message(FATAL_ERROR "compliance report did not preserve its release block and traceability tables")
endif()
