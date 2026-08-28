#include "Tools/Builder/CMakeBuilder.h"

#include <array>
#include <cstdio>
#include <iostream>
#include <sstream>

namespace Zenvra::Tools::Builder
{

BuildResult CMakeBuilder::build_target(
    const CMakeBuildOptions& options,
    std::function<void(std::string_view)> output_callback) const
{
    std::ostringstream cmd;
    cmd << "cmake --build ";

    if (!options.preset_name.empty())
    {
        cmd << "--preset " << options.preset_name;
    }
    else if (!options.build_directory.empty())
    {
        cmd << options.build_directory;
    }
    else
    {
        cmd << "build";
    }

    if (!options.target_name.empty())
    {
        cmd << " --target " << options.target_name;
    }

    if (options.clean_first)
    {
        cmd << " --clean-first";
    }

    const std::string command_str = cmd.str();
    BuildResult result{};

#if defined(_WIN32)
    // FIX bentrok CMD vs PowerShell: _popen di Windows implisit pakai %COMSPEC% (cmd.exe /c)
    // sedangkan TerminalSession pakai PowerShell. Quoting & redirect 2>&1 beda semantic.
    // Kita eksplisit bungkus dengan cmd.exe /d /c agar deterministik dan tidak campur PS.
    const std::string wrapped = "cmd.exe /d /c \"" + command_str + " 2>&1\"";
    FILE* pipe = _popen(wrapped.c_str(), "r");
#else
    FILE* pipe = popen(command_str.c_str(), "r");
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
    const std::filesystem::path& /*workspace_root*/) const
{
    return {"ZDE", "ZDEUnitTests"};
}

} // namespace Zenvra::Tools::Builder
