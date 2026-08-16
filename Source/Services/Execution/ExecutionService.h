#pragma once

#include "Tools/Runner/ProcessRunner.h"

#include <functional>
#include <mutex>
#include <string_view>

namespace Zenvra::Services::Execution
{

enum class ExecutionState
{
    Idle,
    Running,
    Terminated
};

class ExecutionService
{
public:
    ExecutionService() = default;

    bool run_target_async(
        const Tools::Runner::ProcessExecutionOptions& options,
        std::function<void(std::string_view)> stdout_callback = {});

    void stop();

    [[nodiscard]] ExecutionState get_state() const noexcept { return m_state; }

private:
    ExecutionState m_state = ExecutionState::Idle;
    Tools::Runner::ProcessRunner m_runner;
    std::mutex m_mutex;
};

} // namespace Zenvra::Services::Execution
