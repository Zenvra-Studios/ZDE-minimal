#pragma once

#include "Source/Services/Plugins/Loader/PackageLoader.h"
#include "Source/Services/Plugins/Permissions/PermissionValidator.h"
#include "Source/Services/Plugins/Resolver/DependencyGraph.h"
#include "Source/Services/Plugins/Runtime/PluginRuntime.h"
#include "Source/Services/Language/Registry/LanguageRegistry.h"

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Zenvra::Services::Plugins::Host
{

class PluginHost
{
public:
    explicit PluginHost(std::shared_ptr<Language::Registry::LanguageRegistry> language_registry = nullptr);
    ~PluginHost();

    void add_search_path(const std::filesystem::path& directory);

    std::size_t discover_plugins();

    bool load_and_activate_all();

    bool activate_plugin(const std::string& plugin_id);

    bool deactivate_plugin(const std::string& plugin_id);

    [[nodiscard]] std::vector<Loader::PluginPackage> list_installed_plugins() const;

    [[nodiscard]] std::shared_ptr<Permissions::PermissionValidator> get_permission_validator() const noexcept
    {
        return m_permission_validator;
    }

private:
    std::shared_ptr<Language::Registry::LanguageRegistry> m_language_registry;
    std::shared_ptr<Permissions::PermissionValidator> m_permission_validator;
    std::vector<std::filesystem::path> m_search_paths;
    std::unordered_map<std::string, Loader::PluginPackage> m_discovered;
    std::unordered_map<std::string, std::unique_ptr<Runtime::PluginRuntimeContext>> m_contexts;
};

} // namespace Zenvra::Services::Plugins::Host

