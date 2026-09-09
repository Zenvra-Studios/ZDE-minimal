# CMakeLists.txt for Editors module.

add_library(ZDEEditors
    Interaction/DefaultEditorMode.cpp
    Interaction/EditorInputRouter.cpp
    Vim/VimMotions.cpp
    Vim/VimOperators.cpp
    Vim/VimVisualMode.cpp
    Vim/VimEditorMode.cpp
)

add_library(Zenvra::Editors ALIAS ZDEEditors)

target_include_directories(ZDEEditors PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${PROJECT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/Source"
)

target_link_libraries(ZDEEditors PUBLIC
    Zenvra::Core
    Zenvra::Settings
    Zenvra::UI
)

zde_enable_warnings(ZDEEditors)

