if(NOT WIN32)
    return()
endif()

add_library(ZDEPlatformWin32 STATIC
    Win32Window.cpp
    Components/Menubar.cpp
)

target_include_directories(ZDEPlatformWin32 PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_definitions(ZDEPlatformWin32 PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
    _WIN32_WINNT=0x0A00
    WINVER=0x0A00
)
target_link_libraries(ZDEPlatformWin32
    PUBLIC Zenvra::Core
    PRIVATE Zenvra::Commands Zenvra::UI user32 gdi32 shell32 dwmapi uxtheme
)
zde_enable_warnings(ZDEPlatformWin32)
