#pragma once

#include "Plugins/Toolchain/ToolInfo.h"

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Zenvra::Plugins::Toolchain
{

class ToolRegistry
{
public:
    ToolRegistry() = default;

    void register_tool(ToolInfo tool);
    void unregister_tool(std::string_view tool_id);

    [[nodiscard]] std::optional<ToolInfo> get_tool(std::string_view tool_id) const;
    [[nodiscard]] bool has_tool(std::string_view tool_id) const;
    [[nodiscard]] std::vector<ToolInfo> get_all_tools() const;

    bool load_from_disk(const std::filesystem::path& registry_file);
    bool save_to_disk(const std::filesystem::path& registry_file) const;

    void clear() noexcept;

private:
    mutable std::mutex m_mutex;
    std::unordered_map<std::string, ToolInfo> m_tools;
};

} // namespace Zenvra::Plugins::Toolchain
