cmake_minimum_required(VERSION 3.25)
project("ZDE" LANGUAGES CXX C VERSION 0.1.0)

# Objective-C++ is only used by the Cocoa platform (Apple). Enabling it on
# every compiler breaks MinGW/GCC, which cannot compile Objective-C++.
if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# --- Dependencies ---------------------------------------------------------
# CPM.cmake is CMake's package manager, used to fetch third-party libraries.
# It is vendored in Cmake/CPM.cmake (v0.40.2, see https://github.com/cpm-cmake/CPM.cmake).
include("${CMAKE_CURRENT_SOURCE_DIR}/Cmake/CPM.cmake")

# --- Output layout ---------------------------------------------------------
# Everything the build produces lives OUTSIDE the source tree, under the
# build directory, split by kind and by configuration so Debug and Release
# never mix and never double up (no extra <config> subfolder from the
# Visual Studio generator, because $<CONFIG> is already in the path):
#   <build>/bin/<config>/  -> executables + dynamic libraries (the runtime)
#   <build>/lib/<config>/  -> static / import libraries (what the app links)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/$<CONFIG>")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/$<CONFIG>")

# Automatically include every top-level subdirectory that has its own CMakeLists.txt,
# so any new subdirectory gets picked up without touching this file.
set(ZDE_SUBDIRECTORIES
    Cmake
    Config
    Docs
    Drivers
    Scripts
    Source
    Tests
    ThirdParty
)

foreach(subdir IN LISTS ZDE_SUBDIRECTORIES)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${subdir}/CMakeLists.txt")
        add_subdirectory("${subdir}")
    endif()
endforeach()
