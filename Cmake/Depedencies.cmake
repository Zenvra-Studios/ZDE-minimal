# Third-party dependencies managed via CPM.cmake

# zlib - Compression library
set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
CPMAddPackage(
    NAME zlib
    GITHUB_REPOSITORY madler/zlib
    GIT_TAG v1.3.1
    OPTIONS
        "ZLIB_BUILD_EXAMPLES OFF"
)

# lunasvg - SVG rendering library
set(LUNASVG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
CPMAddPackage(
    NAME lunasvg
    GITHUB_REPOSITORY sammycage/lunasvg
    GIT_TAG v3.0.0
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
    GITHUB_REPOSITORY fmtlib/fmt
    GIT_TAG 10.1.1
    OPTIONS
        "FMT_INSTALL OFF"
)

# enet - Reliable UDP networking library
CPMAddPackage(
    NAME enet
    GITHUB_REPOSITORY lsalzman/enet
    GIT_TAG v1.3.18
)

# gtest - Google Testing and Mocking Framework
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
CPMAddPackage(
    NAME googletest
    GITHUB_REPOSITORY google/googletest
    GIT_TAG v1.14.0
    OPTIONS
        "INSTALL_GTEST OFF"
)
