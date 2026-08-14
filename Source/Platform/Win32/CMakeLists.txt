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
    Utility/Win32Dialogs.cpp
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
    PUBLIC Zenvra::Core
    PRIVATE Zenvra::Commands Zenvra::UI Zenvra::Terminal lunasvg ZDEUtility
            user32 gdi32 shell32 dwmapi uxtheme usp10 ole32 uuid
)
zde_enable_warnings(ZDEPlatformWin32)
