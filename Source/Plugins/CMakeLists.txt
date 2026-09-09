# CMakeLists.txt for Plugins module.

add_library(ZDEPlugins
    PluginManifest.cpp
    Plugin.cpp
    PluginRegistry.cpp
    Toolchain/ToolRegistry.cpp
    Toolchain/ToolManager.cpp
    Installer/DependencyResolver.cpp
    Installer/PluginInstaller.cpp
    Marketplace/MarketplaceClient.cpp
    PluginManager.cpp
    Editor/EditorPluginManager.cpp
)
add_library(Zenvra::Plugins ALIAS ZDEPlugins)
add_library(Plugins ALIAS ZDEPlugins)

target_include_directories(ZDEPlugins PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${PROJECT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/Source"
)

target_link_libraries(ZDEPlugins PUBLIC
    Zenvra::Core
    Zenvra::Settings
    Zenvra::Language
    nlohmann_json::nlohmann_json
)
if(WIN32)
    target_link_libraries(ZDEPlugins PRIVATE winhttp)
endif()

zde_enable_warnings(ZDEPlugins)
