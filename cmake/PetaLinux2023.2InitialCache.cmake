# Use with:
# cmake -C cmake/PetaLinux2023.2InitialCache.cmake -S . -B build/petalinux-aarch64 ...
set(CMAKE_BUILD_TYPE "Release" CACHE STRING "PetaLinux release build")
set(RTSA_BUILD_TESTS OFF CACHE BOOL "Qt Test is not required in the target SDK")
set(RTSA_ENABLE_WARNINGS_AS_ERRORS ON CACHE BOOL "Keep target portability warnings clean")
set(RTSA_TARGET_PETALINUX ON CACHE BOOL "Enable Linux/AArch64/Qt5 target checks")
