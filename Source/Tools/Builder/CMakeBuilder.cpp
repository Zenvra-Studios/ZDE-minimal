#include "Tools/Builder/CMakeBuilder.h"
#include "Tools/Classification/ProjectToolClassifier.h"

#include <array>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>

namespace Zenvra::Tools::Builder
{

BuildResult CMakeBuilder::build_target(
    const CMakeBuildOptions& options,
    std::function<void(std::string_view)> output_callback) const
{
    const bool has_presets = !options.workspace_root.empty() &&
        std::filesystem::exists(options.workspace_root / "CMakePresets.json");

    std::ostringstream cmd;

    if (has_presets && !options.preset_name.empty())
    {
        cmd << "cmake --build --preset " << options.preset_name;
        if (!options.target_name.empty())
        {
            cmd << " --target " << options.target_name;
        }
        if (options.clean_first)
        {
            cmd << " --clean-first";
        }
    }
    else
    {
        std::filesystem::path build_dir;
        if (!options.build_directory.empty())
        {
            build_dir = options.build_directory;
        }
        else if (!options.workspace_root.empty())
        {
            const std::string norm_arch = (options.architecture == "x86" || options.architecture == "Win32") ? "x86" :
                                          ((options.architecture == "arm64" || options.architecture == "ARM64") ? "arm64" :
                                          ((options.architecture == "arm32" || options.architecture == "ARM32") ? "arm32" : "x64"));
            const auto default_build = options.workspace_root / "build";
            bool use_arch_dir = (norm_arch != "x64");
            if (std::filesystem::exists(default_build / "CMakeCache.txt"))
            {
                std::ifstream cache_file(default_build / "CMakeCache.txt");
                std::string line;
                while (std::getline(cache_file, line))
                {
                    if (line.find("CMAKE_GENERATOR_PLATFORM") != std::string::npos)
                    {
                        if ((norm_arch == "x86" && line.find("Win32") != std::string::npos) ||
                            (norm_arch == "x64" && line.find("x64") != std::string::npos) ||
                            (norm_arch == "arm64" && line.find("ARM64") != std::string::npos))
                        {
                            use_arch_dir = false;
                        }
                        else
                        {
                            use_arch_dir = true;
                        }
                        break;
                    }
                }
            }

            if (use_arch_dir)
            {
                build_dir = options.workspace_root / ("build_" + norm_arch);
            }
            else
            {
                build_dir = default_build;
            }
        }
        else
        {
            build_dir = "build";
        }

        // Auto-configure build directory if it doesn't exist yet
        std::error_code ec;
        if (!options.workspace_root.empty() && !std::filesystem::exists(build_dir, ec))
        {
            std::ostringstream config_cmd;
            config_cmd << "cmake -B \"" << build_dir.string() << "\" -S \"" << options.workspace_root.string() << "\"";
#if defined(_WIN32)
            if (options.architecture == "x86" || options.architecture == "Win32")
            {
                config_cmd << " -A Win32";
            }
            else if (options.architecture == "arm64" || options.architecture == "ARM64")
            {
                config_cmd << " -A ARM64";
            }
            else if (options.architecture == "arm32" || options.architecture == "ARM32")
            {
                config_cmd << " -A ARM";
            }
            else
            {
                config_cmd << " -A x64";
            }
#endif
            const std::string config_full = config_cmd.str();
            if (output_callback)
            {
                output_callback("[CMake] Configuring build tree: " + config_full + "\n");
            }
#if defined(_WIN32)
            std::string cd_prefix;
            if (!options.workspace_root.empty())
            {
                cd_prefix = "cd /d \"" + options.workspace_root.string() + "\" && ";
            }
            const std::string wrapped_cfg = "cmd.exe /d /c \"" + cd_prefix + config_full + " 2>&1\"";
            FILE* cfg_pipe = _popen(wrapped_cfg.c_str(), "r");
#else
            std::string cd_prefix;
            if (!options.workspace_root.empty())
            {
                cd_prefix = "cd \"" + options.workspace_root.string() + "\" && ";
            }
            const std::string wrapped_cfg = cd_prefix + config_full;
            FILE* cfg_pipe = popen(wrapped_cfg.c_str(), "r");
#endif
            if (cfg_pipe)
            {
                std::array<char, 256> buf{};
                while (fgets(buf.data(), static_cast<int>(buf.size()), cfg_pipe) != nullptr)
                {
                    if (output_callback) output_callback(buf.data());
                }
#if defined(_WIN32)
                _pclose(cfg_pipe);
#else
                pclose(cfg_pipe);
#endif
            }
        }

        cmd << "cmake --build \"" << build_dir.string() << "\"";
        const std::string config = options.configuration.empty() ? "Debug" : options.configuration;
        cmd << " --config " << config;
        if (!options.target_name.empty())
        {
            cmd << " --target " << options.target_name;
        }
        if (options.clean_first)
        {
            cmd << " --clean-first";
        }
    }

    const std::string command_str = cmd.str();
    BuildResult result{};

#if defined(_WIN32)
    std::string cd_prefix;
    if (!options.workspace_root.empty())
    {
        cd_prefix = "cd /d \"" + options.workspace_root.string() + "\" && ";
    }
    const std::string wrapped = "cmd.exe /d /c \"" + cd_prefix + command_str + " 2>&1\"";
    FILE* pipe = _popen(wrapped.c_str(), "r");
#else
    std::string cd_prefix;
    if (!options.workspace_root.empty())
    {
        cd_prefix = "cd \"" + options.workspace_root.string() + "\" && ";
    }
    const std::string wrapped = cd_prefix + command_str;
    FILE* pipe = popen(wrapped.c_str(), "r");
#endif
    if (!pipe)
    {
        result.exit_code = -1;
        result.success = false;
        result.output_log = "Failed to spawn CMake process.";
        return result;
    }

    std::array<char, 256> buffer{};
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
    {
        result.output_log += buffer.data();
        if (output_callback)
        {
            output_callback(buffer.data());
        }
    }

#if defined(_WIN32)
    result.exit_code = _pclose(pipe);
#else
    result.exit_code = pclose(pipe);
#endif
    result.success = (result.exit_code == 0);
    return result;
}

std::vector<std::string> CMakeBuilder::discover_cmake_targets(
    const std::filesystem::path& workspace_root) const
{
    const auto targets = Classification::ProjectToolClassifier::detect_cmake_executable_targets(workspace_root);
    if (!targets.empty())
    {
        return targets;
    }
    return {"ZDE", "ZDEUnitTests"};
}

} // namespace Zenvra::Tools::Builder
