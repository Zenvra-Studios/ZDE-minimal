#include "DependencyGraph.h"

namespace Zenvra::Services::Plugins::Resolver
{

void DependencyGraph::add_package(const Loader::PluginPackage& pkg)
{
    m_nodes[pkg.id] = pkg;
}

ResolutionResult DependencyGraph::resolve() const
{
    ResolutionResult result;
    std::unordered_set<std::string> visited;
    std::unordered_set<std::string> visiting;

    // Check for missing dependencies first
    for (const auto& [id, pkg] : m_nodes)
    {
        for (const auto& [dep_id, dep_ver] : pkg.dependencies)
        {
            if (m_nodes.find(dep_id) == m_nodes.end())
            {
                result.missing_dependencies.push_back(id + " requires missing " + dep_id);
            }
        }
    }

    if (!result.missing_dependencies.empty())
    {
        result.success = false;
        return result;
    }

    for (const auto& [id, pkg] : m_nodes)
    {
        if (visited.find(id) == visited.end())
        {
            if (!visit(id, visited, visiting, result.load_order, result.cyclic_dependencies))
            {
                result.success = false;
                return result;
            }
        }
    }

    result.success = true;
    return result;
}

bool DependencyGraph::visit(
    const std::string& node,
    std::unordered_set<std::string>& visited,
    std::unordered_set<std::string>& visiting,
    std::vector<std::string>& order,
    std::vector<std::string>& cycles) const
{
    visiting.insert(node);

    auto it = m_nodes.find(node);
    if (it != m_nodes.end())
    {
        for (const auto& [dep_id, dep_ver] : it->second.dependencies)
        {
            if (visiting.find(dep_id) != visiting.end())
            {
                cycles.push_back(node + " -> " + dep_id);
                return false;
            }

            if (visited.find(dep_id) == visited.end())
            {
                if (!visit(dep_id, visited, visiting, order, cycles))
                    return false;
            }
        }
    }

    visiting.erase(node);
    visited.insert(node);
    order.push_back(node);
    return true;
}

} // namespace Zenvra::Services::Plugins::Resolver

