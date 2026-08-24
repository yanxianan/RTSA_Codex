if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR
        "RTSA_TARGET_PETALINUX requires a Linux target toolchain. "
        "Source the PetaLinux SDK environment before configuring.")
endif()

if(NOT CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR
        "RTSA_TARGET_PETALINUX requires cross-compilation. "
        "Do not use the Windows/native compiler for the board build.")
endif()

string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" RTSA_SYSTEM_PROCESSOR_LOWER)
if(NOT RTSA_SYSTEM_PROCESSOR_LOWER MATCHES "^(aarch64|arm64)$")
    message(FATAL_ERROR
        "RTSA_TARGET_PETALINUX expects AArch64, but CMAKE_SYSTEM_PROCESSOR is "
        "'${CMAKE_SYSTEM_PROCESSOR}'. Check the selected PetaLinux SDK.")
endif()

if(NOT QT_VERSION_MAJOR EQUAL 5)
    message(FATAL_ERROR
        "The PetaLinux 2023.2 deployment baseline requires target Qt 5.x. "
        "Found Qt major version ${QT_VERSION_MAJOR} in the target sysroot.")
endif()

set(RTSA_TARGET_QT_VERSION "${Qt5Core_VERSION}")
if(RTSA_TARGET_QT_VERSION STREQUAL "")
    set(RTSA_TARGET_QT_VERSION "${Qt5Core_VERSION_STRING}")
endif()
if(RTSA_TARGET_QT_VERSION STREQUAL "")
    set(RTSA_TARGET_QT_VERSION "${QT_VERSION}")
endif()
if(RTSA_TARGET_QT_VERSION STREQUAL ""
   OR RTSA_TARGET_QT_VERSION VERSION_LESS "5.15")
    message(FATAL_ERROR
        "RTSA requires Qt 5.15 or newer on the target. "
        "Detected version '${RTSA_TARGET_QT_VERSION}'.")
endif()

message(STATUS "RTSA PetaLinux target checks passed")
message(STATUS "  Target system: ${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "  Target Qt: ${RTSA_TARGET_QT_VERSION}")
message(STATUS "  C++ compiler: ${CMAKE_CXX_COMPILER}")
