# The full engine fixture owns process-wide singletons. Run each reset mode in
# a fresh process, then compare the captured command stream and frame CRC.
if("$ENV{SAGE_NAVIGATION_TEST_SAVE}" STREQUAL "")
    message("SKIP: Set SAGE_NAVIGATION_TEST_SAVE to an external save fixture.")
    return()
endif()
if(NOT EXISTS "$ENV{SAGE_NAVIGATION_TEST_SAVE}")
    message(FATAL_ERROR "Save fixture does not exist: $ENV{SAGE_NAVIGATION_TEST_SAVE}")
endif()
set(TEST_NAME save_oracle_many_unit_commands_reproduce_deterministically)
set(RESET_MODES retained rebuilt repeated startup_seed)

foreach(mode IN LISTS RESET_MODES)
    if(mode STREQUAL "retained")
        execute_process(COMMAND "${TEST_EXE}" "--run_test=${TEST_NAME}" --log_level=message
            WORKING_DIRECTORY "${TEST_DIR}" RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    elseif(mode STREQUAL "rebuilt")
        execute_process(COMMAND "${CMAKE_COMMAND}" -E env SAGE_NAVIGATION_TEST_RESET_PLANNER=1
            "${TEST_EXE}" "--run_test=${TEST_NAME}" --log_level=message
            WORKING_DIRECTORY "${TEST_DIR}" RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    elseif(mode STREQUAL "startup_seed")
        execute_process(COMMAND "${CMAKE_COMMAND}" -E env SAGE_NAVIGATION_TEST_STARTUP_SEED=1
            "${TEST_EXE}" "--run_test=${TEST_NAME}" --log_level=message
            WORKING_DIRECTORY "${TEST_DIR}" RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    else()
        execute_process(COMMAND "${CMAKE_COMMAND}" -E env SAGE_NAVIGATION_TEST_RESET_PLANNER=1
            "${TEST_EXE}" "--run_test=${TEST_NAME}" --log_level=message
            WORKING_DIRECTORY "${TEST_DIR}" RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    endif()
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Save oracle ${mode} failed (${result}):\n${output}\n${error}")
    endif()
    string(REGEX MATCH "command_digest=([0-9]+) frame_digest=([0-9]+)" digest "${output}\n${error}")
    if(NOT digest)
        message(FATAL_ERROR "Save oracle ${mode} did not report command/frame digests:\n${output}\n${error}")
    endif()
    set(value "${CMAKE_MATCH_1};${CMAKE_MATCH_2}")
    if(mode STREQUAL "retained")
        set(reference "${value}")
    elseif(NOT value STREQUAL reference)
        message(FATAL_ERROR "Save oracle ${mode} diverged: ${value}; expected ${reference}")
    endif()
endforeach()
