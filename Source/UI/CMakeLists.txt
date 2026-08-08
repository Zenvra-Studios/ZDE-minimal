add_library(ZDEUI
    Chrome/WindowChromeLayout.cpp
    Chrome/WindowMenuModel.cpp
    Components/EditorFolding.cpp
    Components/MenuModel.cpp
    Components/Button.cpp
    Components/Input.cpp
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
    Editor/SelectionAnimationModel.cpp
    Editor/StudioEditorModel.cpp
    Editor/TextDocumentModel.cpp
    Theme/StudioTheme.cpp
)
add_library(Zenvra::UI ALIAS ZDEUI)

target_include_directories(ZDEUI PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(ZDEUI PUBLIC Zenvra::Core PRIVATE Zenvra::Commands)
zde_enable_warnings(ZDEUI)
