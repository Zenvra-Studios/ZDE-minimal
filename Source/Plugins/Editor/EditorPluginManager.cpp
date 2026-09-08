#include "Editor/EditorPluginManager.h"
#include "Settings/SettingsService.h"

namespace Zenvra::Plugins::Editor
{

EditorPluginManager& EditorPluginManager::get_instance()
{
    static EditorPluginManager instance;
    return instance;
}

bool EditorPluginManager::load_plugin(const std::string& path)
{
    // Stub: Load editor plugin
    static_cast<void>(path);
    return false;
}

void EditorPluginManager::unload_all()
{
    // Stub: Unload all
}

void EditorPluginManager::register_setting(Settings::SettingDefinition definition)
{
    Settings::SettingsService::instance().register_setting(std::move(definition));
}

} // namespace Zenvra::Plugins::Editor
