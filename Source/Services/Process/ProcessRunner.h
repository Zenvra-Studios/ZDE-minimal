#pragma once

#include "Services/Process/ProcessHandle.h"
#include "Services/Process/ProcessResult.h"
#include "Services/Process/ProcessSpec.h"

#include <functional>
#include <mutex>
#include <optional>
#include <string_view>
#include <unordered_map>

namespace Zenvra::Services::Process
{

class ProcessRunner
{
public:
    ProcessRunner() = default;
    ~ProcessRunner();

    ProcessRunner(const ProcessRunner&) = delete;
    ProcessRunner& operator=(const ProcessRunner&) = delete;

    [[nodiscard]] ProcessHandle start(
        const ProcessSpec& spec,
        std::function<void(std::string_view)> on_stdout = {},
        std::function<void(std::string_view)> on_stderr = {});

    [[nodiscard]] ProcessResult run(const ProcessSpec& spec);

    bool terminate(ProcessHandle handle);

    [[nodiscard]] bool is_running(ProcessHandle handle);

    [[nodiscard]] static std::optional<std::filesystem::path> resolve_executable(
        const std::filesystem::path& exec,
        const std::filesystem::path& working_dir = {});

private:
    std::mutex m_mutex;
    std::uint64_t m_next_id = 1;
    std::unordered_map<std::uint64_t, void*> m_active_processes;
};

} // namespace Zenvra::Services::Process
