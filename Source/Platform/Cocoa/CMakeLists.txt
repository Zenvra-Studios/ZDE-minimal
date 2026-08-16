if(NOT APPLE)
    return()
endif()

add_library(ZDEPlatformCocoa
    CocoaWindow.mm
    Runtime/CocoaContext.mm
    Runtime/CocoaMenuBridge.mm
    Components/ActivitySidebar.mm
    Components/CocoaChromeRenderer.mm
    Components/CocoaPromptDialog.mm
    Components/EditorMinimap.mm
    Components/EditorScrollbar.mm
    Components/ExplorerHeader.mm
    Components/FileDropTarget.mm
    Components/FooterToolbar.mm
    Components/ShaderSandboxPanel.mm
    Components/StudioWorkspaceRenderer.mm
    Components/TerminalPanel.mm
    Components/TextEditor.mm
    Components/ToolSidebar.mm
    Components/ZenvraContentView.mm
)

target_include_directories(ZDEPlatformCocoa PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_link_libraries(ZDEPlatformCocoa PUBLIC Zenvra::Core Zenvra::PlatformHost PRIVATE Zenvra::UI Zenvra::Terminal Zenvra::Services Zenvra::Language lunasvg ZDEUtility "-framework Cocoa" "-framework CoreGraphics" "-framework CoreText")
zde_enable_warnings(ZDEPlatformCocoa)
