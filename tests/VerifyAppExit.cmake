if(NOT DEFINED APP OR NOT DEFINED MODE OR NOT DEFINED EXPECTED_EXIT)
    message(FATAL_ERROR "APP, MODE and EXPECTED_EXIT are required")
endif()

if(MODE STREQUAL "invalid-source")
    execute_process(
        COMMAND "${APP}" --source invalid --no-file-log
        RESULT_VARIABLE actual_exit
    )
elseif(MODE STREQUAL "scenario-source-mismatch")
    if(NOT DEFINED SCENARIO)
        message(FATAL_ERROR "SCENARIO is required for scenario-source-mismatch")
    endif()
    execute_process(
        COMMAND "${APP}" --source dma --scenario "${SCENARIO}" --no-file-log
        RESULT_VARIABLE actual_exit
    )
elseif(MODE STREQUAL "missing-scenario")
    execute_process(
        COMMAND "${APP}" --source simulated
                --scenario "${CMAKE_CURRENT_BINARY_DIR}/missing-scenario.json"
                --no-file-log
        RESULT_VARIABLE actual_exit
    )
else()
    message(FATAL_ERROR "Unknown MODE: ${MODE}")
endif()

if(NOT "${actual_exit}" STREQUAL "${EXPECTED_EXIT}")
    message(FATAL_ERROR
        "Mode ${MODE} returned ${actual_exit}; expected ${EXPECTED_EXIT}")
endif()
