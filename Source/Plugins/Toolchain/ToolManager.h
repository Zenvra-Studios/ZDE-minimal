#pragma once

#include "Plugins/Toolchain/ToolInfo.h"
#include "Plugins/Toolchain/ToolRegistry.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Zenvra::Plugins::Toolchain
{

class ToolManager
{
public:
    static ToolManager& instance() noexcept;

    void initialize(const std::filesystem::path& zde_home_dir);

    [[nodiscard]] ToolRegistry& get_registry() noexcept { return m_registry; }
    [[nodiscard]] const ToolRegistry& get_registry() const noexcept { return m_registry; }

    /// 4-Tier resolution: Project > Managed > System PATH > Recipe
    [[nodiscard]] std::optional<ToolInfo> resolve_tool(
        std::string_view tool_id,
        const std::filesystem::path& workspace_root = {});

    /// Searches system PATH and known installation locations for an executable
    [[nodiscard]] std::optional<std::filesystem::path> find_in_system(std::string_view executable_name) const;

    /// Searches project-local paths
    [[nodiscard]] std::optional<std::filesystem::path> find_in_project(
        std::string_view executable_name,
        const std::filesystem::path& workspace_root) const;

    /// Searches ZDE managed directory (~/.zde/tools/)
    [[nodiscard]] std::optional<std::filesystem::path> find_in_managed(std::string_view tool_id) const;

    /// Queries executable version string by running `<exe> --version`
    [[nodiscard]] std::string query_version(const std::filesystem::path& executable_path) const;

    /// Scans common tools (clangd, rustc, cargo, rust-analyzer, bun, node, ninja, cmake) and registers them
    void scan_and_register_known_tools(const std::filesystem::path& workspace_root = {});

    [[nodiscard]] const std::filesystem::path& get_managed_tools_dir() const noexcept { return m_managed_tools_dir; }

private:
    ToolManager();
    ~ToolManager() = default;

    ToolRegistry m_registry;
    std::filesystem::path m_managed_tools_dir;
    std::filesystem::path m_registry_file;
};

} // namespace Zenvra::Plugins::Toolchain
