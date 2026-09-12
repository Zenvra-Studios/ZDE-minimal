#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Zenvra::Tools::Builder
{

struct CMakeBuildOptions
{
    std::filesystem::path workspace_root{};
#if defined(__APPLE__)
    std::string preset_name = "macos-debug";
#elif defined(_WIN32)
    std::string preset_name = "windows-ninja-debug";
#else
    std::string preset_name = "linux-debug";
#endif
    std::string target_name = "ZDE";
    std::string build_directory = "";
    bool clean_first = false;
};

struct BuildResult
{
    int exit_code = 0;
    bool success = false;
    std::string output_log;
};

class CMakeBuilder
{
public:
    CMakeBuilder() = default;

    [[nodiscard]] BuildResult build_target(
        const CMakeBuildOptions& options,
        std::function<void(std::string_view)> output_callback = {}) const;

    [[nodiscard]] std::vector<std::string> discover_cmake_targets(
        const std::filesystem::path& workspace_root) const;
};

} // namespace Zenvra::Tools::Builder
