if(NOT APPLE)
    return()
endif()

add_library(ZDEPlatformCocoa
    CocoaWindow.mm
    Runtime/CocoaContext.mm
    Runtime/CocoaMenuBridge.mm
    Components/ActivitySidebar.mm
    Components/CocoaChromeRenderer.mm
    Components/EditorMinimap.mm
    Components/EditorScrollbar.mm
    Components/ExplorerHeader.mm
    Components/FileDropTarget.mm
    Components/FooterToolbar.mm
    Components/StudioWorkspaceRenderer.mm
    Components/TerminalPanel.mm
    Components/TextEditor.mm
    Components/ToolSidebar.mm
    Components/ZenvraContentView.mm
)

target_include_directories(ZDEPlatformCocoa PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_link_libraries(ZDEPlatformCocoa PUBLIC Zenvra::Core PRIVATE Zenvra::UI Zenvra::Terminal lunasvg ZDEUtility "-framework Cocoa" "-framework CoreGraphics" "-framework CoreText")
zde_enable_warnings(ZDEPlatformCocoa)
