#include "Plugins/PluginRegistry.h"

#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace Zenvra::Plugins
{

void PluginRegistry::register_plugin(std::shared_ptr<Plugin> plugin)
{
    if (!plugin) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_plugins[plugin->get_id()] = std::move(plugin);
}

void PluginRegistry::unregister_plugin(std::string_view id)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_plugins.erase(std::string(id));
}

bool PluginRegistry::enable_plugin(std::string_view id)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_plugins.find(std::string(id));
    if (it != m_plugins.end() && it->second)
    {
        it->second->set_enabled(true);
        return true;
    }
    return false;
}

bool PluginRegistry::disable_plugin(std::string_view id)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_plugins.find(std::string(id));
    if (it != m_plugins.end() && it->second)
    {
        it->second->set_enabled(false);
        return true;
    }
    return false;
}

std::shared_ptr<Plugin> PluginRegistry::get_plugin(std::string_view id) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_plugins.find(std::string(id));
    if (it != m_plugins.end())
    {
        return it->second;
    }
    return nullptr;
}

bool PluginRegistry::has_plugin(std::string_view id) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_plugins.find(std::string(id)) != m_plugins.end();
}

std::vector<std::shared_ptr<Plugin>> PluginRegistry::get_all_plugins() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::shared_ptr<Plugin>> result;
    result.reserve(m_plugins.size());
    for (const auto& [_, p] : m_plugins)
    {
        result.push_back(p);
    }
    return result;
}

std::vector<std::shared_ptr<Plugin>> PluginRegistry::get_enabled_plugins() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::shared_ptr<Plugin>> result;
    for (const auto& [_, p] : m_plugins)
    {
        if (p && p->is_enabled())
        {
            result.push_back(p);
        }
    }
    return result;
}

void PluginRegistry::clear() noexcept
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_plugins.clear();
}

bool PluginRegistry::load_from_disk(const std::filesystem::path& registry_file)
{
    std::ifstream stream(registry_file);
    if (!stream.is_open())
    {
        return false;
    }

    try
    {
        nlohmann::json root;
        stream >> root;

        if (root.contains("plugins") && root["plugins"].is_object())
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (const auto& [key, val] : root["plugins"].items())
            {
                if (!val.is_object()) continue;

                std::string install_path_str;
                if (val.contains("installPath") && val["installPath"].is_string())
                {
                    install_path_str = val["installPath"].get<std::string>();
                }
                std::filesystem::path install_path(install_path_str);
                std::error_code ec;

                // Only load if installPath is valid and actually exists on disk
                if (!std::filesystem::exists(install_path, ec) ||
                    (!std::filesystem::exists(install_path / "plugin.json", ec) &&
                     !std::filesystem::exists(install_path / "package.json", ec)))
                {
                    continue; // Skip non-existent plugin on disk
                }

                std::optional<PluginManifest> manifest;
                if (std::filesystem::exists(install_path / "plugin.json", ec))
                {
                    manifest = PluginManifest::from_file(install_path / "plugin.json");
                }
                else if (std::filesystem::exists(install_path / "package.json", ec))
                {
                    manifest = PluginManifest::from_file(install_path / "package.json");
                }

                if (manifest.has_value())
                {
                    PluginSource src = PluginSource::Local;
                    if (val.contains("source") && val["source"].is_string())
                    {
                        src = plugin_source_from_string(val["source"].get<std::string>());
                    }

                    auto plugin = std::make_shared<Plugin>(*manifest, install_path, src);
                    bool enabled = true;
                    if (val.contains("enabled") && val["enabled"].is_boolean())
                    {
                        enabled = val["enabled"].get<bool>();
                    }
                    plugin->set_enabled(enabled);
                    m_plugins[key] = plugin;
                }
            }
            return true;
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "[PluginRegistry] Error loading " << registry_file << ": " << e.what() << '\n';
    }
    return false;
}

bool PluginRegistry::save_to_disk(const std::filesystem::path& registry_file) const
{
    std::error_code ec;
    std::filesystem::create_directories(registry_file.parent_path(), ec);

    std::ofstream stream(registry_file);
    if (!stream.is_open())
    {
        return false;
    }

    nlohmann::json root = nlohmann::json::object();
    nlohmann::json plugins_obj = nlohmann::json::object();

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& [id, plugin] : m_plugins)
        {
            if (!plugin) continue;
            nlohmann::json item = nlohmann::json::object();
            item["id"] = plugin->get_id();
            item["name"] = plugin->get_name();
            item["version"] = plugin->get_version();
            item["installPath"] = plugin->get_install_path().string();
            item["source"] = plugin_source_to_string(plugin->get_source());
            item["enabled"] = plugin->is_enabled();
            item["state"] = plugin_state_to_string(plugin->get_state());
            item["manifest"] = plugin->get_manifest().to_json();
            plugins_obj[id] = item;
        }
    }

    root["version"] = 1;
    root["plugins"] = plugins_obj;

    stream << root.dump(2);
    return true;
}

} // namespace Zenvra::Plugins
