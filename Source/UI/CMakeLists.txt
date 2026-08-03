add_library(ZDEUI STATIC
    Chrome/WindowChromeLayout.cpp
    Chrome/WindowMenuModel.cpp
    Theme/StudioTheme.cpp
)
add_library(Zenvra::UI ALIAS ZDEUI)

target_include_directories(ZDEUI PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(ZDEUI PUBLIC Zenvra::Core PRIVATE Zenvra::Commands)
zde_enable_warnings(ZDEUI)
