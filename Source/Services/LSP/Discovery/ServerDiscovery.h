#pragma once

#include <filesystem>
#include <future>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Zenvra::Services::LSP::Discovery
{

struct DiscoveryResult
{
    bool found{false};
    std::filesystem::path executable_path;
    std::string version;
    std::string source; // "configured", "managed", "path"
};

class ServerDiscovery
{
public:
    ServerDiscovery() = default;

    /// Discovers candidate executable synchronously
    [[nodiscard]] DiscoveryResult discover_sync(
        const std::vector<std::string>& candidate_names,
        const std::filesystem::path& custom_path = {},
        const std::filesystem::path& managed_store_root = {}) const;

    /// Discovers candidate executable asynchronously without blocking UI thread (FR-03)
    [[nodiscard]] std::future<DiscoveryResult> discover_async(
        std::vector<std::string> candidate_names,
        std::filesystem::path custom_path = {},
        std::filesystem::path managed_store_root = {}) const;

    /// Probes executable version by running "<exe> --version"
    [[nodiscard]] static std::string probe_version(const std::filesystem::path& exe_path);

    /// Finds executable in system PATH
    [[nodiscard]] static std::optional<std::filesystem::path> find_in_system_path(std::string_view exe_name);

    /// Checks if a command is available on system PATH
    [[nodiscard]] static bool is_command_available(std::string_view cmd);

private:
    [[nodiscard]] static std::optional<std::filesystem::path> search_system_path(std::string_view exe_name);
};

} // namespace Zenvra::Services::LSP::Discovery

