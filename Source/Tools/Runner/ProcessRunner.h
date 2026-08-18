#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace Zenvra::Tools::Runner
{

struct ProcessExecutionOptions
{
    std::filesystem::path executable_path{};
    std::vector<std::string> arguments{};
    std::filesystem::path working_directory{};
    bool run_in_background = true;
};

class ProcessRunner
{
public:
    ProcessRunner() = default;

    [[nodiscard]] bool launch_process(
        const ProcessExecutionOptions& options,
        std::function<void(std::string_view)> stdout_callback = {}) const;

    void terminate_active_process() const;
};

} // namespace Zenvra::Tools::Runner
