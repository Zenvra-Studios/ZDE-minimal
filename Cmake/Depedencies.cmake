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

# lunasvg - SVG rendering library
set(PLUTOVG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
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

# gtest 1.14.0 enables /WX (warnings as errors) under MSVC, which clang-cl turns
# into -Werror. Newer clang warns about the char8_t -> char32_t conversion in
# gtest-printers.h (-Wcharacter-conversion), so suppress it for the gtest targets.
if(MSVC AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    foreach(gtest_target IN ITEMS gtest gtest_main gmock gmock_main)
        if(TARGET ${gtest_target})
            target_compile_options(${gtest_target} PRIVATE -Wno-character-conversion)
        endif()
    endforeach()
endif()
