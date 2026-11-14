#include "Services/LSP/Discovery/ServerDiscovery.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <sstream>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace Zenvra::Services::LSP::Discovery
{

std::optional<std::filesystem::path> ServerDiscovery::search_system_path(std::string_view /*exe_name*/)
{
    // Strict isolation: Language servers must be installed in ZDE software/plugin directories,
    // host OS PATH is never scanned.
    return std::nullopt;
}

std::optional<std::filesystem::path> ServerDiscovery::find_in_system_path(std::string_view exe_name)
{
    return search_system_path(exe_name);
}

bool ServerDiscovery::is_command_available(std::string_view cmd)
{
    return search_system_path(cmd).has_value();
}

std::string ServerDiscovery::probe_version(const std::filesystem::path& exe_path)
{
    if (exe_path.empty()) return {};

    std::string cmd = "\"" + exe_path.string() + "\" --version";
#if defined(_WIN32)
    FILE* pipe = _popen(cmd.c_str(), "r");
#else
    FILE* pipe = popen(cmd.c_str(), "r");
#endif
    if (!pipe) return {};

    std::array<char, 256> buffer{};
    std::string result;
    if (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
    {
        result = buffer.data();
    }
#if defined(_WIN32)
    _pclose(pipe);
#else
    pclose(pipe);
#endif

    // Trim trailing whitespace
    while (!result.empty() && (result.back() == '\r' || result.back() == '\n' || result.back() == ' '))
    {
        result.pop_back();
    }
    return result;
}

DiscoveryResult ServerDiscovery::discover_sync(
    const std::vector<std::string>& candidate_names,
    const std::filesystem::path& custom_path,
    const std::filesystem::path& managed_store_root) const
{
    std::error_code ec;

    // 1. Explicit configured custom path
    if (!custom_path.empty())
    {
        if (std::filesystem::exists(custom_path, ec) && std::filesystem::is_regular_file(custom_path, ec))
        {
            return DiscoveryResult{
                .found = true,
                .executable_path = custom_path,
                .version = probe_version(custom_path),
                .source = "configured"
            };
        }
    }

    // 2. Managed installation store (<store>/<server>/... or <plugins>/lsp/...)
    if (!managed_store_root.empty() && std::filesystem::exists(managed_store_root, ec))
    {
        for (const auto& name : candidate_names)
        {
            std::string name_with_ext = name;
#if defined(_WIN32)
            if (!name_with_ext.ends_with(".exe")) name_with_ext += ".exe";
#endif
            const std::filesystem::path candidates[] = {
                managed_store_root / name_with_ext,
                managed_store_root / name / name_with_ext,
                managed_store_root / name / "bin" / name_with_ext,
            };

            for (const auto& cand : candidates)
            {
                if (std::filesystem::exists(cand, ec) && std::filesystem::is_regular_file(cand, ec))
                {
                    return DiscoveryResult{
                        .found = true,
                        .executable_path = cand,
                        .version = probe_version(cand),
                        .source = "managed"
                    };
                }
            }
        }
    }

    // 3. Application / Workspace plugins directory
    const std::filesystem::path local_plugins_root = std::filesystem::current_path() / "plugins" / "lsp";
    if (std::filesystem::exists(local_plugins_root, ec))
    {
        for (const auto& name : candidate_names)
        {
            std::string name_with_ext = name;
#if defined(_WIN32)
            if (!name_with_ext.ends_with(".exe")) name_with_ext += ".exe";
#endif
            const std::filesystem::path candidates[] = {
                local_plugins_root / name_with_ext,
                local_plugins_root / name / name_with_ext,
                local_plugins_root / name / "bin" / name_with_ext,
            };

            for (const auto& cand : candidates)
            {
                if (std::filesystem::exists(cand, ec) && std::filesystem::is_regular_file(cand, ec))
                {
                    return DiscoveryResult{
                        .found = true,
                        .executable_path = cand,
                        .version = probe_version(cand),
                        .source = "managed"
                    };
                }
            }
        }
    }

    return DiscoveryResult{.found = false};
}

std::future<DiscoveryResult> ServerDiscovery::discover_async(
    std::vector<std::string> candidate_names,
    std::filesystem::path custom_path,
    std::filesystem::path managed_store_root) const
{
    return std::async(std::launch::async, [this, candidate_names = std::move(candidate_names), custom_path = std::move(custom_path), managed_store_root = std::move(managed_store_root)]() {
        return discover_sync(candidate_names, custom_path, managed_store_root);
    });
}

} // namespace Zenvra::Services::LSP::Discovery

