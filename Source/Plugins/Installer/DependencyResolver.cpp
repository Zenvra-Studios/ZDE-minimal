#include "Plugins/Installer/DependencyResolver.h"

#include <queue>
#include <sstream>

namespace Zenvra::Plugins::Installer
{

ResolutionResult DependencyResolver::resolve(
    const PluginManifest& target,
    const std::unordered_map<std::string, PluginManifest>& available_plugins,
    Toolchain::ToolManager& tool_manager,
    const std::filesystem::path& workspace_root)
{
    ResolutionResult result;
    result.success = true;

    // Check tool dependencies
    for (const auto& tool_dep : target.get_tool_dependencies())
    {
        auto resolved = tool_manager.resolve_tool(tool_dep.id, workspace_root);
        if (!resolved.has_value())
        {
            if (!tool_dep.optional)
            {
                result.missing_tools.push_back(tool_dep.id);
                result.success = false;
            }
        }
    }

    // Check plugin dependencies
    for (const auto& plugin_dep : target.get_plugin_dependencies())
    {
        auto it = available_plugins.find(plugin_dep.id);
        if (it == available_plugins.end())
        {
            if (!plugin_dep.optional)
            {
                result.missing_plugins.push_back(plugin_dep.id);
                result.success = false;
            }
        }
    }

    if (!result.success)
    {
        std::ostringstream oss;
        oss << "Unsatisfied dependencies for plugin '" << target.get_id() << "':";
        if (!result.missing_plugins.empty())
        {
            oss << " Missing plugins [";
            for (std::size_t i = 0; i < result.missing_plugins.size(); ++i)
            {
                if (i > 0) oss << ", ";
                oss << result.missing_plugins[i];
            }
            oss << "].";
        }
        if (!result.missing_tools.empty())
        {
            oss << " Missing tools [";
            for (std::size_t i = 0; i < result.missing_tools.size(); ++i)
            {
                if (i > 0) oss << ", ";
                oss << result.missing_tools[i];
            }
            oss << "].";
        }
        result.error_message = oss.str();
    }

    return result;
}

ResolutionResult DependencyResolver::sort_topological(
    const std::unordered_map<std::string, PluginManifest>& plugins)
{
    ResolutionResult result;
    result.success = true;

    std::unordered_map<std::string, std::vector<std::string>> adj;
    std::unordered_map<std::string, int> in_degree;

    for (const auto& [id, _] : plugins)
    {
        in_degree[id] = 0;
        adj[id] = {};
    }

    for (const auto& [id, manifest] : plugins)
    {
        for (const auto& dep : manifest.get_plugin_dependencies())
        {
            if (plugins.contains(dep.id))
            {
                adj[dep.id].push_back(id);
                in_degree[id]++;
            }
        }
    }

    std::queue<std::string> q;
    for (const auto& [id, deg] : in_degree)
    {
        if (deg == 0) q.push(id);
    }

    while (!q.empty())
    {
        std::string u = q.front();
        q.pop();
        result.install_order.push_back(u);

        for (const auto& v : adj[u])
        {
            in_degree[v]--;
            if (in_degree[v] == 0)
            {
                q.push(v);
            }
        }
    }

    if (result.install_order.size() != plugins.size())
    {
        result.success = false;
        result.error_message = "Circular dependency detected among installed plugins.";
    }

    return result;
}

} // namespace Zenvra::Plugins::Installer
