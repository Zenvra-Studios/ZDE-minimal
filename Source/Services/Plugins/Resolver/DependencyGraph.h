#pragma once

#include "Source/Services/Plugins/Loader/PackageLoader.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace Zenvra::Services::Plugins::Resolver
{

struct ResolutionResult
{
    bool success{false};
    std::vector<std::string> load_order;
    std::vector<std::string> missing_dependencies;
    std::vector<std::string> cyclic_dependencies;
};

class DependencyGraph
{
public:
    DependencyGraph() = default;

    void add_package(const Loader::PluginPackage& pkg);

    [[nodiscard]] ResolutionResult resolve() const;

private:
    bool visit(
        const std::string& node,
        std::unordered_set<std::string>& visited,
        std::unordered_set<std::string>& visiting,
        std::vector<std::string>& order,
        std::vector<std::string>& cycles) const;

    std::unordered_map<std::string, Loader::PluginPackage> m_nodes;
};

} // namespace Zenvra::Services::Plugins::Resolver

