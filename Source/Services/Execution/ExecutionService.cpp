#include "Services/Execution/ExecutionService.h"

namespace Zenvra::Services::Execution
{

bool ExecutionService::run_target_async(
    const Tools::Runner::ProcessExecutionOptions& options,
    std::function<void(std::string_view)> stdout_callback)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_state = ExecutionState::Running;
    }
    const bool ok = m_runner.launch_process(options, stdout_callback);
    if (!ok)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_state = ExecutionState::Idle;
    }
    return ok;
}

void ExecutionService::stop()
{
    m_runner.terminate_active_process();
    std::lock_guard<std::mutex> lock(m_mutex);
    m_state = ExecutionState::Idle;
}

} // namespace Zenvra::Services::Execution
