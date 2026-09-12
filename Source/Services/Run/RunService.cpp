#include "Services/Run/RunService.h"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace Zenvra::Services::Run
{

RunService::~RunService()
{
    stop();
    if (m_worker_thread.joinable())
    {
        m_worker_thread.join();
    }
}

bool RunService::run_active_async(
    std::function<void(std::string_view)> output_callback,
    std::function<void(Process::ProcessResult)> completion_callback)
{
    const auto* active = m_store.get_active_configuration();
    if (!active)
    {
        return false;
    }
    return run_async(*active, std::move(output_callback), std::move(completion_callback));
}

bool RunService::run_async(
    const RunConfiguration& config,
    std::function<void(std::string_view)> output_callback,
    std::function<void(Process::ProcessResult)> completion_callback)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_is_running.load())
    {
        return false;
    }

    if (m_worker_thread.joinable())
    {
        m_worker_thread.join();
    }

    const auto root = m_workspace_root.empty() ? std::filesystem::current_path() : m_workspace_root;
    const auto resolved = RunConfigurationResolver::resolve(config, root, m_preset);

    if (!resolved.error_message.empty())
    {
        Output::OutputLogManager::instance().append_line(
            Output::OutputCategory::Runner,
            "[Runner Error] " + resolved.error_message);
        return false;
    }

    m_is_running.store(true);

    m_worker_thread = std::thread([this, spec = resolved.process_spec, config_name = config.name,
                                   output_cb = std::move(output_callback),
                                   completion_cb = std::move(completion_callback)]() {
        Output::OutputLogManager::instance().append_line(
            Output::OutputCategory::Runner,
            "[Runner] Starting target: " + config_name);

        auto stdout_handler = [output_cb](std::string_view chunk) {
            Output::OutputLogManager::instance().append_text(Output::OutputCategory::Runner, chunk);
            if (output_cb)
            {
                output_cb(chunk);
            }
        };

        Process::ProcessResult result;
        auto handle = m_runner.start(spec, stdout_handler, stdout_handler);

        {
            std::lock_guard<std::mutex> inner_lock(m_mutex);
            m_active_handle = handle;
        }

        if (handle.is_valid())
        {
#if defined(_WIN32)
            auto* h_proc = static_cast<HANDLE>(handle.native_handle);
            WaitForSingleObject(h_proc, INFINITE);
            DWORD exit_code = 0;
            if (GetExitCodeProcess(h_proc, &exit_code))
            {
                result.exit_code = static_cast<int>(exit_code);
                result.success = (result.exit_code == 0);
            }
            else
            {
                result.exit_code = -1;
                result.success = false;
            }
#else
            result.exit_code = 0;
            result.success = true;
#endif
        }
        else
        {
            result.exit_code = -1;
            result.success = false;
        }

        {
            std::lock_guard<std::mutex> inner_lock(m_mutex);
            m_last_result = result;
            m_active_handle = {};
        }

        Output::OutputLogManager::instance().append_line(
            Output::OutputCategory::Runner,
            result.success
                ? "[Runner] Process exited cleanly (exit code: 0)."
                : "[Runner] Process exited with code: " + std::to_string(result.exit_code));

        m_is_running.store(false);

        if (completion_cb)
        {
            completion_cb(result);
        }
    });

    return true;
}

void RunService::stop()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_active_handle.is_valid())
    {
        m_runner.terminate(m_active_handle);
        m_active_handle = {};
    }
    m_is_running.store(false);
}

} // namespace Zenvra::Services::Run
