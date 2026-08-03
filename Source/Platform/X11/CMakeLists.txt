if(NOT UNIX OR APPLE)
    return()
endif()

find_package(X11 REQUIRED)

if(NOT TARGET X11::Xft)
    message(FATAL_ERROR "The X11 backend requires Xft development files (libxft-dev).")
endif()

add_library(ZDEPlatformX11 STATIC
    X11Window.cpp
    Components/ActivitySidebar.cpp
    Components/EditorMinimap.cpp
    Components/EditorScrollbar.cpp
    Components/FileDropTarget.cpp
    Components/FooterToolbar.cpp
    Components/StudioWorkspaceRenderer.cpp
    Components/TerminalPanel.cpp
    Components/TextEditor.cpp
    Components/ToolSidebar.cpp
    Components/X11ChromeRenderer.cpp
    Runtime/X11Context.cpp
)

target_include_directories(ZDEPlatformX11 PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_link_libraries(ZDEPlatformX11
    PUBLIC Zenvra::Core X11::X11
    PRIVATE Zenvra::UI Zenvra::Terminal X11::Xft lunasvg
)
zde_enable_warnings(ZDEPlatformX11)
