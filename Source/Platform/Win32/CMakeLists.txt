# CMakeLists.txt for Win32 Platform

if(NOT WIN32)
    return()
endif()

# Standard Win32 compile definitions. Applied to this directory and its
# subdirectories (including the WinRT runtime).
add_compile_definitions(
    WIN32_LEAN_AND_MEAN      # Keep windows.h slim; omit rarely used APIs
    NOMINMAX                 # Do not define min/max macros (std::min/std::max safe)
    UNICODE
    _UNICODE                 # Force wide-character Windows API (W-suffix)
    _WIN32_WINNT=0x0A00      # Target Windows 10 APIs
    WINVER=0x0A00
    _WINDOWS                 # Windowed application build
    _CRT_SECURE_NO_WARNINGS  # Silence CRT deprecation warnings
    _CRT_NONSTDC_NO_DEPRECATE
)

file(GLOB_RECURSE PLATFORM_WIN32_SOURCES CONFIGURE_DEPENDS "*.h" "*.cpp")
# WinRT is a separate runtime — exclude it here, it is only compiled under MSVC.
list(FILTER PLATFORM_WIN32_SOURCES EXCLUDE REGEX "[\\/]WinRT[\\/]")

# C++/WinRT ships with the MSVC toolchain and with clang-cl (which simulates
# MSVC), but NOT with MinGW/GCC or standalone Clang on MinGW — those do not
# ship the winrt/ headers, so the WinRT runtime is only compiled when the
# toolchain is MSVC-compatible.
if(MSVC OR (WIN32 AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"))
    file(GLOB_RECURSE PLATFORM_WINRT_SOURCES CONFIGURE_DEPENDS "WinRT/*.h" "WinRT/*.cpp")
    list(APPEND PLATFORM_WIN32_SOURCES ${PLATFORM_WINRT_SOURCES})
endif()

# Single static library: Win32 + WinRT runtime merged into one.
add_library(PlatformWin32 STATIC ${PLATFORM_WIN32_SOURCES})
target_include_directories(PlatformWin32 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(WIN32)
    target_link_libraries(PlatformWin32 PRIVATE user32 gdi32 shell32)
    # C++/WinRT requires the Windows App SDK runtime library (MSVC / clang-cl only)
    if(MSVC OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        target_link_libraries(PlatformWin32 PRIVATE windowsapp)
    endif()
endif()
