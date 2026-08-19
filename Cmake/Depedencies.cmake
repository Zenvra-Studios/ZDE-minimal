# Third-party dependencies managed via CPM.cmake
#
# Dependencies are downloaded as release tarballs (URL) instead of git clones.
# A git clone of a large repository (e.g. nlohmann/json, googletest) stalls or
# hangs on slow/unstable connections to github.com, while a single tarball
# download is far more robust. Run CMake with
#   -DCPM_SOURCE_CACHE=$HOME/.cache/CPM
# to download each dependency only once and reuse it across builds.

# zlib - Compression library
set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
CPMAddPackage(
    NAME zlib
    VERSION 1.3.1
    URL "https://github.com/madler/zlib/archive/refs/tags/v1.3.1.tar.gz"
    OPTIONS
        "ZLIB_BUILD_EXAMPLES OFF"
)

# plutovg - 2D vector graphics library (required by lunasvg)
set(PLUTOVG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
CPMAddPackage(
    NAME plutovg
    VERSION 1.3.3
    URL "https://github.com/sammycage/plutovg/archive/refs/tags/v1.3.3.tar.gz"
    OPTIONS
        "PLUTOVG_BUILD_EXAMPLES OFF"
)

# lunasvg - SVG rendering library
set(LUNASVG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
CPMAddPackage(
    NAME lunasvg
    VERSION 3.0.0
    URL "https://github.com/sammycage/lunasvg/archive/refs/tags/v3.0.0.tar.gz"
    OPTIONS
        "LUNASVG_BUILD_EXAMPLES OFF"
)

# fmt - Formatting library
# Workaround for clang-cl consteval pointer arithmetic bug under C++20
# Defining FMT_CONSTEVAL= prevents FMT_HAS_CONSTEVAL from being defined in core.h
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_definitions(FMT_CONSTEVAL=)
endif()

CPMAddPackage(
    NAME fmt
    VERSION 10.1.1
    URL "https://github.com/fmtlib/fmt/archive/refs/tags/10.1.1.tar.gz"
    OPTIONS
        "FMT_INSTALL OFF"
)

# enet - Reliable UDP networking library
CPMAddPackage(
    NAME enet
    VERSION 1.3.18
    URL "https://github.com/lsalzman/enet/archive/refs/tags/v1.3.18.tar.gz"
)

# nlohmann/json - JSON for Modern C++
CPMAddPackage(
    NAME nlohmann_json
    VERSION 3.11.3
    URL "https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.tar.gz"
    OPTIONS
        "JSON_BuildTests OFF"
)

# gtest - Google Testing and Mocking Framework
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
CPMAddPackage(
    NAME googletest
    VERSION 1.14.0
    URL "https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz"
    OPTIONS
        "INSTALL_GTEST OFF"
)

# libgit2 - Core Git library
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_CLI OFF CACHE BOOL "" FORCE)
set(USE_SSH OFF CACHE BOOL "" FORCE)
set(USE_NTLMCLIENT OFF CACHE BOOL "" FORCE)
set(USE_ICONV OFF CACHE BOOL "" FORCE)
set(REGEX_BACKEND "builtin" CACHE STRING "" FORCE)
set(USE_BUNDLED_ZLIB OFF CACHE BOOL "" FORCE)
set(ZLIB_FOUND TRUE CACHE BOOL "" FORCE)
set(ZLIB_INCLUDE_DIRS "${zlib_SOURCE_DIR}" "${zlib_BINARY_DIR}" CACHE STRING "" FORCE)
set(ZLIB_LIBRARIES zlib CACHE STRING "" FORCE)

CPMAddPackage(
    NAME libgit2
    VERSION 1.8.1
    URL "https://github.com/libgit2/libgit2/archive/refs/tags/v1.8.1.tar.gz"
    OPTIONS
        "BUILD_TESTS OFF"
        "BUILD_CLI OFF"
        "USE_SSH OFF"
        "USE_NTLMCLIENT OFF"
        "USE_ICONV OFF"
        "REGEX_BACKEND builtin"
        "USE_BUNDLED_ZLIB OFF"
)

foreach(gtest_target IN ITEMS gtest gtest_main gmock gmock_main)
    if(TARGET ${gtest_target})
        set_target_properties(${gtest_target} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>"
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>"
            ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/$<CONFIG>"
        )
        if(MSVC AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            target_compile_options(${gtest_target} PRIVATE -Wno-character-conversion)
        endif()
    endif()
endforeach()
