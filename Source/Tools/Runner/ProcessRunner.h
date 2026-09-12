#pragma once

#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
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
    ~ProcessRunner();

    ProcessRunner(const ProcessRunner&) = delete;
    ProcessRunner& operator=(const ProcessRunner&) = delete;

    [[nodiscard]] bool launch_process(
        const ProcessExecutionOptions& options,
        std::function<void(std::string_view)> stdout_callback = {}) const;

    void terminate_active_process() const;

private:
    mutable void* m_process_handle = nullptr;
    mutable std::thread m_reader_thread;
    mutable std::mutex m_runner_mutex;
};

} // namespace Zenvra::Tools::Runner
