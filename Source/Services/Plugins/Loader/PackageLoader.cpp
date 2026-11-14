#include "PackageLoader.h"

#include <fstream>
#include <nlohmann/json.hpp>

namespace Zenvra::Services::Plugins::Loader
{

std::optional<PluginPackage> PackageLoader::load_from_directory(const std::filesystem::path& plugin_directory)
{
    std::filesystem::path manifest_path = plugin_directory / "zenvra-plugin.json";
    if (!std::filesystem::exists(manifest_path))
    {
        manifest_path = plugin_directory / "package.json";
        if (!std::filesystem::exists(manifest_path))
            return std::nullopt;
    }

    std::ifstream file(manifest_path);
    if (!file.is_open())
        return std::nullopt;

    try
    {
        nlohmann::json j;
        file >> j;

        PluginPackage pkg;
        pkg.id = j.value("name", plugin_directory.filename().string());
        pkg.name = j.value("displayName", pkg.id);
        pkg.version = j.value("version", "1.0.0");
        pkg.publisher = j.value("publisher", "zenvra");
        pkg.description = j.value("description", "");
        pkg.root_directory = plugin_directory;

        std::string main_entry = j.value("main", "");
        if (!main_entry.empty())
            pkg.entry_point = plugin_directory / main_entry;

        // Permissions
        if (j.contains("permissions") && j["permissions"].is_array())
        {
            for (const auto& perm_item : j["permissions"])
            {
                if (perm_item.is_string())
                {
                    pkg.declared_permissions.grant(
                        Permissions::PermissionSet::parse_permission(perm_item.get<std::string>()));
                }
            }
        }

        // Dependencies
        if (j.contains("dependencies") && j["dependencies"].is_object())
        {
            for (auto it = j["dependencies"].begin(); it != j["dependencies"].end(); ++it)
            {
                pkg.dependencies.emplace_back(it.key(), it.value().get<std::string>());
            }
        }

        // Contributions
        if (j.contains("contributes") && j["contributes"].is_object())
        {
            const auto& c = j["contributes"];
            if (c.contains("languages") && c["languages"].is_array())
            {
                for (const auto& lang : c["languages"])
                {
                    if (lang.is_string())
                        pkg.contributions.language_definitions.push_back(plugin_directory / lang.get<std::string>());
                    else if (lang.is_object() && lang.contains("definition"))
                        pkg.contributions.language_definitions.push_back(plugin_directory / lang["definition"].get<std::string>());
                }
            }
            if (c.contains("grammars") && c["grammars"].is_array())
            {
                for (const auto& g : c["grammars"])
                {
                    if (g.is_string())
                        pkg.contributions.grammar_files.push_back(plugin_directory / g.get<std::string>());
                    else if (g.is_object() && g.contains("path"))
                        pkg.contributions.grammar_files.push_back(plugin_directory / g["path"].get<std::string>());
                }
            }
            if (c.contains("themes") && c["themes"].is_array())
            {
                for (const auto& t : c["themes"])
                {
                    if (t.is_string())
                        pkg.contributions.theme_files.push_back(plugin_directory / t.get<std::string>());
                    else if (t.is_object() && t.contains("path"))
                        pkg.contributions.theme_files.push_back(plugin_directory / t["path"].get<std::string>());
                }
            }
        }

        return pkg;
    }
    catch (...)
    {
        return std::nullopt;
    }
}

std::vector<PluginPackage> PackageLoader::scan_plugins_directory(const std::filesystem::path& plugins_root)
{
    std::vector<PluginPackage> packages;
    std::error_code ec;

    if (!std::filesystem::exists(plugins_root, ec))
        return packages;

    for (const auto& entry : std::filesystem::directory_iterator(plugins_root, ec))
    {
        if (entry.is_directory(ec))
        {
            auto pkg = load_from_directory(entry.path());
            if (pkg)
                packages.push_back(std::move(*pkg));
        }
    }

    return packages;
}

} // namespace Zenvra::Services::Plugins::Loader

