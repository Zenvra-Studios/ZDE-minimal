#include "Editor/EditorPluginManager.h"

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

} // namespace Zenvra::Plugins::Editor
