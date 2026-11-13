if(NOT WIN32)
    return()
endif()

add_library(ZDEPlatformWin32
    Win32Window.cpp
    Components/ActivitySidebar.cpp
    Components/EditorMinimap.cpp
    Components/EditorScrollbar.cpp
    Components/ExplorerHeader.cpp
    Components/FileDropTarget.cpp
    Components/FooterToolbar.cpp
    Components/Menubar.cpp
    Components/StudioWorkspaceRenderer.cpp
    Components/TerminalPanel.cpp
    Components/TextEditor.cpp
    Components/ToolSidebar.cpp
    Components/ToolSwitcherPopup.cpp
    Components/Win32PromptDialog.cpp
    Components/ShaderSandboxPanel.cpp
    Utility/Win32Dialogs.cpp
    Utility/Tray.cpp
    WinRT/WinRTContext.cpp
    WinRT/WinRTNotification.cpp
)

target_include_directories(ZDEPlatformWin32 PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_definitions(ZDEPlatformWin32 PRIVATE
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE
    _WIN32_WINNT=0x0A00
    WINVER=0x0A00
)
target_link_libraries(ZDEPlatformWin32
    PUBLIC Zenvra::Core Zenvra::PlatformHost Zenvra::Settings
    PRIVATE Zenvra::Commands Zenvra::UI Zenvra::Editors Zenvra::Language Zenvra::Terminal Zenvra::Services Zenvra::Plugins Zenvra::Tools lunasvg ZDEUtility
            user32 gdi32 msimg32 shell32 dwmapi uxtheme usp10 dwrite ole32 uuid runtimeobject comctl32
)
if(WIN32 AND NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    # WinRT is hosted by combase.dll (Windows 8+); cppwinrt resolves its entry
    # points through the api-ms-win-core-winrt-* API sets. Delay-loading all of
    # them removes the static imports so Windows 7 does not abort in the
    # loader; the delayed resolution only happens after the runtime OS check
    # in WinRTContext (Windows 7 never resolves them). MSVC and clang (MSVC
    # ABI) both drive an MSVC-style linker here; MinGW (GNU) is excluded.
    target_link_libraries(ZDEPlatformWin32 PRIVATE delayimp)
    target_link_options(ZDEPlatformWin32 PRIVATE
        "LINKER:/DELAYLOAD:combase.dll"
        "LINKER:/DELAYLOAD:api-ms-win-core-winrt-l1-1-0.dll"
        "LINKER:/DELAYLOAD:api-ms-win-core-winrt-error-l1-1-1.dll"
    )
endif()
zde_enable_warnings(ZDEPlatformWin32)
