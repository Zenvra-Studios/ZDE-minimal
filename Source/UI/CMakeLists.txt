add_library(ZDEUI
    Chrome/WindowChromeLayout.cpp
    Chrome/WindowMenuModel.cpp
    Components/EditorFolding.cpp
    Components/MenuModel.cpp
    Components/Button.cpp
    Components/Dropdown.cpp
    Components/Input.cpp
    Components/Modal.cpp
    Components/AboutModal.cpp
    Components/PromptModal.cpp
    Components/AddNewItemDialog.cpp
    Components/CompletionPopup.cpp
    Components/HoverTooltip.cpp
    Components/ScrollBar.cpp
    Components/SignatureHelpWidget.cpp
    Components/BreadcrumbBar.cpp
    Editor/ActivityPanelModel.cpp
    Editor/BraceAnimationModel.cpp
    Editor/CaretBlinkModel.cpp
    Editor/CppSymbolLexer.cpp
    Editor/EditorController.cpp
    Editor/EditorDropModel.cpp
    Editor/EditorFileCrud.cpp
    Editor/EditorFileSystem.cpp
    Editor/EditorMinimapModel.cpp
    Editor/EditorSessionModel.cpp
    Editor/EditorScrollModel.cpp
    Editor/MediaPlayerView.cpp
    Editor/SelectionAnimationModel.cpp
    Editor/StudioEditorModel.cpp
    Editor/TextDocumentModel.cpp
    Editor/WorkspaceSearchModel.cpp
    Editor/WorkspaceSourceControlModel.cpp
    "${PROJECT_SOURCE_DIR}/Source/SourceControl/GitRepository.cpp"
    Theme/StudioTheme.cpp
    Theme/ThemeManager.cpp
    Toolbar/ToolbarLayout.cpp
    Toolbar/StudioMainToolbar.cpp
    Toolbar/Widgets/RunConfigurationWidget.cpp
    Toolbar/Widgets/ActionButtonGroup.cpp
    Toolbar/Widgets/ProjectBranchWidget.cpp
    Toolbar/Widgets/QuickSearchWidget.cpp
    Settings/SettingsControl.cpp
    Settings/SettingsWindow.cpp
)
add_library(Zenvra::UI ALIAS ZDEUI)

target_include_directories(ZDEUI PUBLIC 
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${PROJECT_SOURCE_DIR}"
)
target_link_libraries(ZDEUI PUBLIC 
    Zenvra::Core Zenvra::Graphics Zenvra::Language 
    Zenvra::Terminal Zenvra::Media Zenvra::Drivers::Media 
    Zenvra::Settings Zenvra::Plugins lunasvg ZDEUtility PRIVATE Zenvra::Commands)
find_library(ZDE_GIT2_LIBRARY git2)
if(ZDE_GIT2_LIBRARY)
    target_link_libraries(ZDEUI PUBLIC "${ZDE_GIT2_LIBRARY}")
endif()
if(WIN32)
    file(GLOB MSVC_LIB_HINTS
        "C:/Program Files/Microsoft Visual Studio/*/Community/VC/Tools/MSVC/*/lib/x64"
        "C:/Program Files (x86)/Microsoft Visual Studio/*/Community/VC/Tools/MSVC/*/lib/x64"
    )
    find_library(DELAYIMP_LIB delayimp HINTS ${MSVC_LIB_HINTS})
    if(DELAYIMP_LIB)
        target_link_libraries(ZDEUI PRIVATE "${DELAYIMP_LIB}")
    else()
        target_link_libraries(ZDEUI PRIVATE delayimp)
    endif()
    target_link_libraries(ZDEUI PRIVATE gdi32 msimg32 user32 dwmapi shell32)
    target_link_options(ZDEUI PRIVATE
        "-Wl,/DELAYLOAD:avcodec-63.dll"
        "-Wl,/DELAYLOAD:avformat-63.dll"
        "-Wl,/DELAYLOAD:avutil-61.dll"
        "-Wl,/DELAYLOAD:swresample-7.dll"
        "-Wl,/DELAYLOAD:swscale-10.dll"
    )
endif()
zde_enable_warnings(ZDEUI)
