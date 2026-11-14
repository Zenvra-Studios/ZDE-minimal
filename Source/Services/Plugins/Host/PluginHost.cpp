#include "PluginHost.h"

namespace Zenvra::Services::Plugins::Host
{

PluginHost::PluginHost(std::shared_ptr<Language::Registry::LanguageRegistry> language_registry)
    : m_language_registry(std::move(language_registry)),
      m_permission_validator(std::make_shared<Permissions::PermissionValidator>())
{
}

PluginHost::~PluginHost() = default;

void PluginHost::add_search_path(const std::filesystem::path& directory)
{
    m_search_paths.push_back(directory);
}

std::size_t PluginHost::discover_plugins()
{
    Loader::PackageLoader loader;
    std::size_t found = 0;

    for (const auto& path : m_search_paths)
    {
        auto packages = loader.scan_plugins_directory(path);
        for (auto& pkg : packages)
        {
            m_discovered[pkg.id] = std::move(pkg);
            ++found;
        }
    }

    return found;
}

bool PluginHost::load_and_activate_all()
{
    Resolver::DependencyGraph graph;
    for (const auto& [id, pkg] : m_discovered)
    {
        graph.add_package(pkg);
    }

    auto res = graph.resolve();
    if (!res.success)
        return false;

    bool all_ok = true;
    for (const auto& id : res.load_order)
    {
        if (!activate_plugin(id))
            all_ok = false;
    }

    return all_ok;
}

bool PluginHost::activate_plugin(const std::string& plugin_id)
{
    auto it = m_discovered.find(plugin_id);
    if (it == m_discovered.end())
        return false;

    const auto& pkg = it->second;
    auto ctx = std::make_unique<Runtime::PluginRuntimeContext>(pkg, m_permission_validator);
    if (!ctx->activate())
        return false;

    // Register contributed languages into LanguageRegistry
    if (m_language_registry)
    {
        for (const auto& lang_def_file : pkg.contributions.language_definitions)
        {
            m_language_registry->load_from_file(lang_def_file);
        }
    }

    m_contexts[plugin_id] = std::move(ctx);
    return true;
}

bool PluginHost::deactivate_plugin(const std::string& plugin_id)
{
    auto it = m_contexts.find(plugin_id);
    if (it == m_contexts.end())
        return false;

    it->second->deactivate();
    m_contexts.erase(it);
    return true;
}

std::vector<Loader::PluginPackage> PluginHost::list_installed_plugins() const
{
    std::vector<Loader::PluginPackage> list;
    list.reserve(m_discovered.size());
    for (const auto& [id, pkg] : m_discovered)
    {
        list.push_back(pkg);
    }
    return list;
}

} // namespace Zenvra::Services::Plugins::Host

