#pragma once

#include "Plugins/PluginManifest.h"
#include "Plugins/Toolchain/ToolManager.h"

#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace Zenvra::Plugins::Installer
{

struct ResolutionResult
{
    bool success{true};
    std::string error_message;
    std::vector<std::string> install_order;
    std::vector<std::string> missing_tools;
    std::vector<std::string> missing_plugins;
};

class DependencyResolver
{
public:
    DependencyResolver() = default;

    /// Validates dependencies for a target manifest against installed plugins and available tools
    [[nodiscard]] ResolutionResult resolve(
        const PluginManifest& target,
        const std::unordered_map<std::string, PluginManifest>& available_plugins,
        Toolchain::ToolManager& tool_manager,
        const std::filesystem::path& workspace_root = {});

    /// Topologically sorts a set of plugins according to inter-plugin dependencies
    [[nodiscard]] ResolutionResult sort_topological(
        const std::unordered_map<std::string, PluginManifest>& plugins);
};

} // namespace Zenvra::Plugins::Installer
