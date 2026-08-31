# Keep the benchmark reproducible when a developer intentionally removes the
# large tracked reference models from the working tree. Release/source archives
# still use the checked-out files; a Git worktree can stage the exact HEAD blobs
# in the disposable benchmark directory without modifying developer changes.
file(GLOB _robotic_reference_stls "${REFERENCE_ROOT}/*.STL")
list(LENGTH _robotic_reference_stls _robotic_reference_count)
if(NOT _robotic_reference_count EQUAL 10)
    find_package(Git QUIET)
    if(NOT GIT_FOUND OR NOT EXISTS "${PROJECT_ROOT}/.git")
        message(FATAL_ERROR
            "expected 10 reference STL component files, found ${_robotic_reference_count}")
    endif()

    set(_robotic_reference_stage "${OUTPUT_ROOT}/reference-fixture")
    file(REMOVE_RECURSE "${_robotic_reference_stage}")
    file(MAKE_DIRECTORY "${_robotic_reference_stage}")
    set(_robotic_reference_files
        "Arm 01.STL"
        "Arm 02 v3.STL"
        "Arm 03.STL"
        "Base.STL"
        "Gripper 1.STL"
        "Gripper base.STL"
        "Waist.STL"
        "gear1.STL"
        "gear2.STL"
        "grip link 1.STL"
        "Robotic Arm 3D Model.STEP")
    foreach(_reference_file IN LISTS _robotic_reference_files)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" show
                    "HEAD:examples/Robotic_Arm_3D_Model/${_reference_file}"
            WORKING_DIRECTORY "${PROJECT_ROOT}"
            OUTPUT_FILE "${_robotic_reference_stage}/${_reference_file}"
            RESULT_VARIABLE _reference_result
            ERROR_VARIABLE _reference_error)
        if(NOT _reference_result EQUAL 0)
            message(FATAL_ERROR
                "could not stage robotic reference ${_reference_file}: ${_reference_error}")
        endif()
    endforeach()
    set(REFERENCE_ROOT "${_robotic_reference_stage}")
endif()
