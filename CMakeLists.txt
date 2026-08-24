cmake_minimum_required(VERSION 3.21)

project(RTSA
    VERSION 0.1.0
    DESCRIPTION "RFSoC real-time spectrum analyzer PS application"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

option(RTSA_BUILD_TESTS "Build RTSA automated tests" ON)
option(RTSA_ENABLE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(RTSA_TARGET_PETALINUX "Enable strict PetaLinux/AArch64 target checks" OFF)
option(RTSA_DEPLOY_WINDOWS_RUNTIME
       "Deploy Qt plugins and compiler runtime during Windows installation" ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Gui Widgets Concurrent)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui Widgets Concurrent)

if(RTSA_TARGET_PETALINUX)
    include(cmake/PetaLinuxTargetChecks.cmake)
endif()

add_subdirectory(src)

if(RTSA_BUILD_TESTS)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)
    include(CTest)
    enable_testing()
    add_subdirectory(tests)
endif()
