#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace Zenvra::Plugins::Toolchain
{

enum class ToolSource
{
    System,
    Project,
    Local,
    Managed,
    Bundled
};

enum class ToolStatus
{
    Available,
    Missing,
    Incompatible,
    Error
};

std::string_view tool_source_to_string(ToolSource source) noexcept;
ToolSource tool_source_from_string(std::string_view str) noexcept;

std::string_view tool_status_to_string(ToolStatus status) noexcept;
ToolStatus tool_status_from_string(std::string_view str) noexcept;

struct ToolInfo
{
    std::string id;
    std::string name;
    std::filesystem::path executable_path;
    std::string version;
    ToolSource source{ToolSource::System};
    ToolStatus status{ToolStatus::Missing};
    std::string last_verified;
};

} // namespace Zenvra::Plugins::Toolchain
