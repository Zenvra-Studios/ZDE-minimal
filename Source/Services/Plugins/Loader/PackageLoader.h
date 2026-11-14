#pragma once

#include "Source/Services/Plugins/Permissions/PermissionSet.h"
#include "Source/Services/Language/Definitions/LanguageDefinition.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Zenvra::Services::Plugins::Loader
{

struct PluginContribution
{
    std::vector<std::filesystem::path> language_definitions;
    std::vector<std::filesystem::path> grammar_files;
    std::vector<std::filesystem::path> theme_files;
    std::vector<std::string> lsp_servers;
};

struct PluginPackage
{
    std::string id;
    std::string name;
    std::string version;
    std::string publisher;
    std::string description;
    std::filesystem::path root_directory;
    std::filesystem::path entry_point;
    Permissions::PermissionSet declared_permissions;
    std::vector<std::pair<std::string, std::string>> dependencies; // id -> version
    PluginContribution contributions;
    bool is_builtin{false};
};

class PackageLoader
{
public:
    PackageLoader() = default;

    [[nodiscard]] std::optional<PluginPackage> load_from_directory(const std::filesystem::path& plugin_directory);

    [[nodiscard]] std::vector<PluginPackage> scan_plugins_directory(const std::filesystem::path& plugins_root);
};

} // namespace Zenvra::Services::Plugins::Loader

