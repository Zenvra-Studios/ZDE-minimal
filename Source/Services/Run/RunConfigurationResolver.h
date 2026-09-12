#pragma once

#include "Services/Process/ProcessSpec.h"
#include "Services/Run/RunConfiguration.h"

#include <filesystem>
#include <string>

namespace Zenvra::Services::Run
{

struct ResolvedRun
{
    Process::ProcessSpec process_spec;
    std::filesystem::path resolved_target;
    bool needs_build = false;
    std::string error_message;
};

class RunConfigurationResolver
{
public:
    [[nodiscard]] static ResolvedRun resolve(
        const RunConfiguration& config,
        const std::filesystem::path& root,
        const std::string& preset = "windows-clang-ninja-release");
};

} // namespace Zenvra::Services::Run
