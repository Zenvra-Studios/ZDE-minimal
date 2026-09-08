#pragma once

#include <string>
#include <vector>

namespace Zenvra::Settings
{
struct SettingDefinition;
}

namespace Zenvra::Plugins::Editor
{

class EditorPluginManager
{
public:
    static EditorPluginManager& get_instance();

    bool load_plugin(const std::string& path);
    void unload_all();

    void register_setting(Settings::SettingDefinition definition);

private:
    EditorPluginManager() = default;
    ~EditorPluginManager() = default;
};

} // namespace Zenvra::Plugins::Editor
