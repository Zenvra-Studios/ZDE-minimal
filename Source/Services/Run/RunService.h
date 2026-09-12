#pragma once

#include "Services/Process/ProcessRunner.h"
#include "Services/Run/RunConfigurationStore.h"
#include "Services/Run/RunConfigurationResolver.h"
#include "Services/Output/OutputLogManager.h"

#include <atomic>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace Zenvra::Services::Run
{

class RunService
{
public:
    RunService() = default;
    ~RunService();

    RunService(const RunService&) = delete;
    RunService& operator=(const RunService&) = delete;

    [[nodiscard]] RunConfigurationStore& store() noexcept { return m_store; }
    [[nodiscard]] const RunConfigurationStore& store() const noexcept { return m_store; }

    void set_workspace_root(std::filesystem::path root) { m_workspace_root = std::move(root); }
    [[nodiscard]] const std::filesystem::path& workspace_root() const noexcept { return m_workspace_root; }

    void set_preset(std::string preset) { m_preset = std::move(preset); }
    [[nodiscard]] const std::string& preset() const noexcept { return m_preset; }

    bool run_async(
        const RunConfiguration& config,
        std::function<void(std::string_view)> output_callback = {},
        std::function<void(Process::ProcessResult)> completion_callback = {});

    bool run_active_async(
        std::function<void(std::string_view)> output_callback = {},
        std::function<void(Process::ProcessResult)> completion_callback = {});

    void stop();

    [[nodiscard]] bool is_running() const noexcept { return m_is_running.load(); }
    [[nodiscard]] const Process::ProcessResult& last_result() const noexcept { return m_last_result; }

private:
    RunConfigurationStore m_store;
    std::filesystem::path m_workspace_root;
    std::string m_preset = "windows-clang-ninja-release";

    Process::ProcessRunner m_runner;
    Process::ProcessHandle m_active_handle;
    std::atomic<bool> m_is_running{false};
    std::thread m_worker_thread;
    std::mutex m_mutex;
    Process::ProcessResult m_last_result;
};

} // namespace Zenvra::Services::Run
