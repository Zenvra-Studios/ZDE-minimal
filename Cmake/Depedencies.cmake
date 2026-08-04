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

# # nlohmann/json - JSON for Modern C++
# CPMAddPackage(
#     NAME nlohmann_json
#     GITHUB_REPOSITORY nlohmann/json
#     GIT_TAG v3.11.3
#     OPTIONS
#         "JSON_BuildTests OFF"
# )

# gtest - Google Testing and Mocking Framework
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
CPMAddPackage(
    NAME googletest
    GITHUB_REPOSITORY google/googletest
    GIT_TAG v1.14.0
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
