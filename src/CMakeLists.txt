add_library(rtsa_core STATIC
    core/AmplitudeUnits.h
    core/EnvelopeReducer.cpp
    core/EnvelopeReducer.h
    core/FramePool.cpp
    core/FramePool.h
    core/FrequencyMapper.cpp
    core/FrequencyMapper.h
    core/LatestFrameStore.cpp
    core/LatestFrameStore.h
    core/SpectrumFrame.h
    core/SpectrumPipeline.cpp
    core/SpectrumPipeline.h
    core/SpectrumMeasurements.cpp
    core/SpectrumMeasurements.h
    core/TraceProcessor.cpp
    core/TraceProcessor.h
)

target_include_directories(rtsa_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_features(rtsa_core PUBLIC cxx_std_17)

add_executable(rtsa_app
    app/main.cpp
    ui/ApplicationTheme.cpp
    ui/ApplicationTheme.h
    ui/FrequencySpinBox.cpp
    ui/FrequencySpinBox.h
    ui/UnfocusedWheelFilter.h
    plot/Colormap.cpp
    plot/Colormap.h
    plot/RasterSpectrumRenderer.cpp
    plot/RasterSpectrumRenderer.h
    plot/SpectrumPlotWidget.cpp
    plot/SpectrumPlotWidget.h
    plot/WaterfallPlotWidget.cpp
    plot/WaterfallPlotWidget.h
    services/SpectrumExporter.cpp
    services/SpectrumExporter.h
    services/ConfigurationStore.cpp
    services/ConfigurationStore.h
    services/ApplicationLogger.cpp
    services/ApplicationLogger.h
    sources/ISpectrumSource.h
    sources/ISimulationConfigurable.h
    sources/SimulatedSpectrumSource.cpp
    sources/SimulatedSpectrumSource.h
    sources/SimulationScenarioLoader.cpp
    sources/SimulationScenarioLoader.h
    sources/SimulationScenarioWriter.cpp
    sources/SimulationScenarioWriter.h
    sources/SpectrumSourceFactory.cpp
    sources/SpectrumSourceFactory.h
    sources/SourceTypes.h
    ui/MainWindow.cpp
    ui/MainWindow.h
)

target_link_libraries(rtsa_app PRIVATE
    rtsa_core
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Concurrent
)

if(MSVC)
    target_compile_options(rtsa_core PRIVATE /W4)
    target_compile_options(rtsa_app PRIVATE /W4)
    if(RTSA_ENABLE_WARNINGS_AS_ERRORS)
        target_compile_options(rtsa_core PRIVATE /WX)
        target_compile_options(rtsa_app PRIVATE /WX)
    endif()
else()
    target_compile_options(rtsa_core PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(rtsa_app PRIVATE -Wall -Wextra -Wpedantic)
    if(RTSA_ENABLE_WARNINGS_AS_ERRORS)
        target_compile_options(rtsa_core PRIVATE -Werror)
        target_compile_options(rtsa_app PRIVATE -Werror)
    endif()
endif()

set_target_properties(rtsa_app PROPERTIES
    WIN32_EXECUTABLE TRUE
)

install(TARGETS rtsa_app RUNTIME DESTINATION bin)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/config/
        DESTINATION share/rtsa/config
        FILES_MATCHING PATTERN "*.json")

if(WIN32 AND RTSA_DEPLOY_WINDOWS_RUNTIME)
    get_target_property(RTSA_QMAKE_EXECUTABLE
                        Qt${QT_VERSION_MAJOR}::qmake IMPORTED_LOCATION)
    if(NOT RTSA_QMAKE_EXECUTABLE)
        message(FATAL_ERROR "Unable to locate qmake for Windows deployment")
    endif()

    get_filename_component(RTSA_QT_BIN_DIR
                           "${RTSA_QMAKE_EXECUTABLE}" DIRECTORY)
    find_program(RTSA_WINDEPLOYQT_EXECUTABLE
        NAMES windeployqt windeployqt6
        HINTS "${RTSA_QT_BIN_DIR}"
        NO_DEFAULT_PATH
    )
    if(NOT RTSA_WINDEPLOYQT_EXECUTABLE)
        message(FATAL_ERROR
            "RTSA_DEPLOY_WINDOWS_RUNTIME is ON, but windeployqt was not found")
    endif()

    set(RTSA_WINDEPLOYQT_INSTALL_CODE [=[
set(_rtsa_bin_dir "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin")
set(_rtsa_executable "${_rtsa_bin_dir}/rtsa_app.exe")
execute_process(
    COMMAND "@RTSA_WINDEPLOYQT_EXECUTABLE@"
            --dir "${_rtsa_bin_dir}"
            --compiler-runtime
            --no-translations
            "${_rtsa_executable}"
    RESULT_VARIABLE _rtsa_deploy_result
    OUTPUT_VARIABLE _rtsa_deploy_output
    ERROR_VARIABLE _rtsa_deploy_error
)
if(NOT _rtsa_deploy_result EQUAL 0)
    message(FATAL_ERROR
        "windeployqt failed with exit code ${_rtsa_deploy_result}\n"
        "${_rtsa_deploy_output}\n${_rtsa_deploy_error}")
endif()
message(STATUS "Deployed Windows runtime dependencies to ${_rtsa_bin_dir}")
]=])
    string(CONFIGURE "${RTSA_WINDEPLOYQT_INSTALL_CODE}"
                     RTSA_WINDEPLOYQT_INSTALL_CODE @ONLY)
    install(CODE "${RTSA_WINDEPLOYQT_INSTALL_CODE}")
endif()
