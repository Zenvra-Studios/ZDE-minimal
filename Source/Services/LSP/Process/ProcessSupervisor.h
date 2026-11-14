#pragma once

#include "Services/LSP/Process/ProcessSpec.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

namespace Zenvra::Services::LSP::Process
{

class ProcessSupervisor
{
public:
    ProcessSupervisor();
    ~ProcessSupervisor();

    using StdoutCallback = std::function<void(std::string_view)>;
    using StderrCallback = std::function<void(std::string_view)>;
    using ExitCallback = std::function<void(int exit_code)>;

    bool start(const ProcessSpec& spec);
    void stop(std::chrono::milliseconds timeout = std::chrono::milliseconds(2000));
    void kill();
    [[nodiscard]] int wait_for_exit();

    [[nodiscard]] bool is_running() const noexcept;
    bool write_input(std::string_view data);

    void set_stdout_listener(StdoutCallback cb);
    void set_stdout_handler(StdoutCallback cb) { set_stdout_listener(std::move(cb)); }
    void set_stderr_listener(StderrCallback cb);
    void set_exit_listener(ExitCallback cb);

    [[nodiscard]] const ProcessSpec& get_spec() const noexcept { return m_spec; }

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    ProcessSpec m_spec;
    std::atomic<bool> m_running{false};
    StdoutCallback m_stdout_listener;
    StderrCallback m_stderr_listener;
    ExitCallback m_exit_listener;
    mutable std::mutex m_mutex;
};

} // namespace Zenvra::Services::LSP::Process

