add_executable(rtsa_core_tests
    tst_Core.cpp
)

target_link_libraries(rtsa_core_tests PRIVATE
    rtsa_core
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Test
)

add_test(NAME rtsa_core_tests COMMAND rtsa_core_tests)
set_tests_properties(rtsa_core_tests PROPERTIES TIMEOUT 10)

add_executable(rtsa_source_tests
    tst_SimulatedSource.cpp
    ../src/sources/SimulatedSpectrumSource.cpp
    ../src/sources/SimulatedSpectrumSource.h
    ../src/sources/SpectrumSourceFactory.cpp
    ../src/sources/SpectrumSourceFactory.h
    ../src/sources/ISpectrumSource.h
    ../src/sources/ISimulationConfigurable.h
    ../src/sources/SourceTypes.h
)

target_include_directories(rtsa_source_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(rtsa_source_tests PRIVATE
    rtsa_core
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Test
)

add_test(NAME rtsa_source_tests COMMAND rtsa_source_tests)
set_tests_properties(rtsa_source_tests PROPERTIES TIMEOUT 15)

add_executable(rtsa_plot_tests
    tst_Plot.cpp
    ../src/plot/RasterSpectrumRenderer.cpp
    ../src/plot/RasterSpectrumRenderer.h
    ../src/plot/SpectrumPlotWidget.cpp
    ../src/plot/SpectrumPlotWidget.h
)

target_include_directories(rtsa_plot_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(rtsa_plot_tests PRIVATE
    rtsa_core
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Test
)

add_test(NAME rtsa_plot_tests COMMAND rtsa_plot_tests)
set_tests_properties(rtsa_plot_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 10
)

add_executable(rtsa_export_tests
    tst_Export.cpp
    ../src/services/SpectrumExporter.cpp
    ../src/services/SpectrumExporter.h
)

target_include_directories(rtsa_export_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(rtsa_export_tests PRIVATE
    rtsa_core
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Test
)

add_test(NAME rtsa_export_tests COMMAND rtsa_export_tests)
set_tests_properties(rtsa_export_tests PROPERTIES TIMEOUT 10)

add_executable(rtsa_settings_tests
    tst_Settings.cpp
    ../src/services/ConfigurationStore.cpp
    ../src/services/ConfigurationStore.h
)

target_include_directories(rtsa_settings_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(rtsa_settings_tests PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Test
)

add_test(NAME rtsa_settings_tests COMMAND rtsa_settings_tests)
set_tests_properties(rtsa_settings_tests PROPERTIES TIMEOUT 10)

add_executable(rtsa_logging_tests
    tst_Logging.cpp
    ../src/services/ApplicationLogger.cpp
    ../src/services/ApplicationLogger.h
)

target_include_directories(rtsa_logging_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(rtsa_logging_tests PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Test
)

add_test(NAME rtsa_logging_tests COMMAND rtsa_logging_tests)
set_tests_properties(rtsa_logging_tests PROPERTIES TIMEOUT 10)

add_executable(rtsa_scenario_tests
    tst_SimulationScenario.cpp
    ../src/sources/SimulationScenarioLoader.cpp
    ../src/sources/SimulationScenarioLoader.h
    ../src/sources/SimulationScenarioWriter.cpp
    ../src/sources/SimulationScenarioWriter.h
    ../src/sources/SourceTypes.h
)

target_include_directories(rtsa_scenario_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(rtsa_scenario_tests PRIVATE
    RTSA_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

target_link_libraries(rtsa_scenario_tests PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Test
)

add_test(NAME rtsa_scenario_tests COMMAND rtsa_scenario_tests)
set_tests_properties(rtsa_scenario_tests PROPERTIES TIMEOUT 10)

add_executable(rtsa_gui_tests
    tst_MainWindow.cpp
    ../src/ui/ApplicationTheme.cpp
    ../src/ui/ApplicationTheme.h
    ../src/plot/RasterSpectrumRenderer.cpp
    ../src/plot/RasterSpectrumRenderer.h
    ../src/plot/SpectrumPlotWidget.cpp
    ../src/plot/SpectrumPlotWidget.h
    ../src/services/ConfigurationStore.cpp
    ../src/services/ConfigurationStore.h
    ../src/services/SpectrumExporter.cpp
    ../src/services/SpectrumExporter.h
    ../src/sources/ISpectrumSource.h
    ../src/sources/SimulatedSpectrumSource.cpp
    ../src/sources/SimulatedSpectrumSource.h
    ../src/sources/SimulationScenarioLoader.cpp
    ../src/sources/SimulationScenarioLoader.h
    ../src/sources/SimulationScenarioWriter.cpp
    ../src/sources/SimulationScenarioWriter.h
    ../src/sources/SourceTypes.h
    ../src/ui/MainWindow.cpp
    ../src/ui/MainWindow.h
)

target_include_directories(rtsa_gui_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(rtsa_gui_tests PRIVATE
    rtsa_core
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Concurrent
    Qt${QT_VERSION_MAJOR}::Test
)

add_test(NAME rtsa_gui_tests COMMAND rtsa_gui_tests)
set_tests_properties(rtsa_gui_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 15
)

add_test(NAME rtsa_app_smoke
    COMMAND $<TARGET_FILE:rtsa_app> --source simulated --no-file-log --smoke-test-ms 750
)

add_test(NAME rtsa_app_scenario_smoke
    COMMAND $<TARGET_FILE:rtsa_app>
        --source simulated
        --scenario ${CMAKE_SOURCE_DIR}/config/default-simulation.json
        --no-file-log
        --smoke-test-ms 750
)
set_tests_properties(rtsa_app_scenario_smoke PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 10
)

add_test(NAME rtsa_app_fault_scenario_smoke
    COMMAND $<TARGET_FILE:rtsa_app>
        --source simulated
        --scenario ${CMAKE_SOURCE_DIR}/config/fault-injection.json
        --no-file-log
        --smoke-test-ms 1250
)
set_tests_properties(rtsa_app_fault_scenario_smoke PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 10
)

add_test(NAME rtsa_app_invalid_source
    COMMAND ${CMAKE_COMMAND}
        -DAPP=$<TARGET_FILE:rtsa_app>
        -DMODE=invalid-source
        -DEXPECTED_EXIT=3
        -P ${CMAKE_CURRENT_SOURCE_DIR}/VerifyAppExit.cmake
)
add_test(NAME rtsa_app_scenario_source_mismatch
    COMMAND ${CMAKE_COMMAND}
        -DAPP=$<TARGET_FILE:rtsa_app>
        -DMODE=scenario-source-mismatch
        -DEXPECTED_EXIT=5
        -DSCENARIO=${CMAKE_SOURCE_DIR}/config/default-simulation.json
        -P ${CMAKE_CURRENT_SOURCE_DIR}/VerifyAppExit.cmake
)
add_test(NAME rtsa_app_missing_scenario
    COMMAND ${CMAKE_COMMAND}
        -DAPP=$<TARGET_FILE:rtsa_app>
        -DMODE=missing-scenario
        -DEXPECTED_EXIT=6
        -P ${CMAKE_CURRENT_SOURCE_DIR}/VerifyAppExit.cmake
)
set_tests_properties(
    rtsa_app_invalid_source
    rtsa_app_scenario_source_mismatch
    rtsa_app_missing_scenario
    PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 10
)
set_tests_properties(rtsa_app_smoke PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 10
)

# A developer should be able to run CTest from a clean Windows shell.  Unlike
# Qt Creator, CTest does not automatically add the Qt and MinGW runtime
# directories to PATH, so every executable would otherwise fail before its
# test main() is entered.  Capture the configure-time system PATH and prepend
# only the two runtime locations selected by this CMake configuration.
if(WIN32)
    get_filename_component(RTSA_COMPILER_RUNTIME_DIR
        "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(RTSA_TEST_RUNTIME_PATH
        "$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>;${RTSA_COMPILER_RUNTIME_DIR};$ENV{PATH}")
    string(REPLACE ";" "\\;" RTSA_TEST_RUNTIME_PATH
        "${RTSA_TEST_RUNTIME_PATH}")

    set_property(TEST
        rtsa_core_tests
        rtsa_source_tests
        rtsa_plot_tests
        rtsa_export_tests
        rtsa_settings_tests
        rtsa_logging_tests
        rtsa_scenario_tests
        rtsa_gui_tests
        rtsa_app_smoke
        rtsa_app_scenario_smoke
        rtsa_app_fault_scenario_smoke
        rtsa_app_invalid_source
        rtsa_app_scenario_source_mismatch
        rtsa_app_missing_scenario
        APPEND PROPERTY ENVIRONMENT "PATH=${RTSA_TEST_RUNTIME_PATH}"
    )
endif()
