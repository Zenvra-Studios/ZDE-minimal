add_library(ZDESettings
    SettingsSchema.cpp
    JsonSettingsStore.cpp
    SettingsService.cpp
)

add_library(Zenvra::Settings ALIAS ZDESettings)

target_include_directories(ZDESettings PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${PROJECT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/Source"
)

target_link_libraries(ZDESettings PUBLIC
    Zenvra::Core
    Zenvra::PlatformHost
    nlohmann_json::nlohmann_json
)

zde_enable_warnings(ZDESettings)
