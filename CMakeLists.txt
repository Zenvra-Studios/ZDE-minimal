cmake_minimum_required(VERSION 3.25)

# --- MSVC CRT Linkage (/MDd in Debug, /MD in Release) -----------------------
# Must be set before project() or adding targets so all dependencies and ZDE
# targets inherit dynamic runtime linkage consistently.
if(NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

set(BUILD_SHARED_LIBS ON CACHE BOOL "Build shared libraries" FORCE)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON CACHE BOOL "Export all symbols on Windows" FORCE)

project("ZDE" LANGUAGES CXX C VERSION 0.1.0)

add_compile_definitions(
    ZDE_VERSION_STRING="${PROJECT_VERSION}"
    ZDE_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    ZDE_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    ZDE_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    ZDE_APP_NAME="Zenvra Development Studio"
    ZDE_EDITION_NAME="Community Edition"
)

include(CTest)

if(WIN32)
    add_compile_definitions(
        _CRT_SECURE_NO_WARNINGS
        _CRT_SECURE_NO_DEPRECATE
        _CRT_NONSTDC_NO_DEPRECATE
        _CRT_NONSTDC_NO_WARNINGS
        _CRT_OBSOLETE_NO_WARNINGS
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
endif()

# Objective-C++ is only used by the Cocoa platform (Apple). Enabling it on
# every compiler breaks MinGW/GCC, which cannot compile Objective-C++.
if(APPLE)
    enable_language(OBJCXX)
    add_compile_definitions(GL_SILENCE_DEPRECATION)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# --- Output layout ---------------------------------------------------------
# Everything the build produces lives OUTSIDE the source tree, under the
# build directory, split by kind and by configuration so Debug and Release
# never mix and never double up:
#   <build>/bin/<config>/  -> executables + dynamic libraries (the runtime)
#   <build>/lib/<config>/  -> static / import libraries (what the app links)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/$<CONFIG>")

# --- RPATH configuration for Linux / macOS ---------------------------------
set(CMAKE_BUILD_RPATH_USE_ORIGIN ON)
if(NOT APPLE)
    set(CMAKE_INSTALL_RPATH "$ORIGIN")
    set(CMAKE_BUILD_RPATH "$ORIGIN")
endif()


# --- Dependencies ---------------------------------------------------------
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Cmake")

# CPM.cmake is CMake's package manager, used to fetch third-party libraries.
# It is vendored in Cmake/CPM.cmake (v0.40.2, see https://github.com/cpm-cmake/CPM.cmake).
include("${CMAKE_CURRENT_SOURCE_DIR}/Cmake/CPM.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/Cmake/Depedencies.cmake")

# --- Compiler Flags per Target & Configuration -----------------------------
function(zde_enable_warnings target_name)
    # Prevent the Windows SDK's min/max macros from breaking calls such as
    # std::min and std::max in any ZDE target that includes windows.h.
    if(WIN32)
        target_compile_definitions(${target_name} PRIVATE
            _CRT_SECURE_NO_WARNINGS
            _CRT_SECURE_NO_DEPRECATE
            _CRT_NONSTDC_NO_DEPRECATE
            _CRT_NONSTDC_NO_WARNINGS
            _CRT_OBSOLETE_NO_WARNINGS
            NOMINMAX
            WIN32_LEAN_AND_MEAN
        )
    endif()

    if(MSVC)
        target_compile_options(${target_name} PRIVATE
            /W4
            /permissive-
            $<$<CONFIG:Debug>:/Zi /Od /RTC1>
            $<$<CONFIG:Release>:/O2 /Ob2>
        )
        target_compile_definitions(${target_name} PRIVATE
            $<$<CONFIG:Debug>:ZDE_DEBUG _DEBUG>
            $<$<CONFIG:Release>:ZDE_RELEASE NDEBUG>
        )
    else()
        target_compile_options(${target_name} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            $<$<CONFIG:Debug>:-g -O0>
            $<$<CONFIG:Release>:-O3>
        )
        target_compile_definitions(${target_name} PRIVATE
            $<$<CONFIG:Debug>:ZDE_DEBUG _DEBUG>
            $<$<CONFIG:Release>:ZDE_RELEASE NDEBUG>
        )
    endif()
endfunction()

# Automatically include every top-level subdirectory that has its own CMakeLists.txt,
# so any new subdirectory gets picked up without touching this file.
set(ZDE_SUBDIRECTORIES
    Cmake
    Config
    Docs
    Drivers
    lib
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

# --- Linux Packaging Configuration (DEB, RPM, TXZ, Arch) --------------------
if(NOT APPLE AND NOT WIN32)
    include("${CMAKE_CURRENT_SOURCE_DIR}/Cmake/Packaging.cmake")
endif()

