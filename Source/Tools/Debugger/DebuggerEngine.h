#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace Zenvra::Tools::Debugger
{

enum class DebuggerBackend
{
    LLDB,
    GDB,
    DAP
};

struct DebugSessionOptions
{
    std::filesystem::path target_binary;
    std::vector<std::string> target_arguments;
    std::filesystem::path working_directory;
    DebuggerBackend backend = DebuggerBackend::LLDB;
};

class DebuggerEngine
{
public:
    DebuggerEngine() = default;

    [[nodiscard]] bool start_session(
        const DebugSessionOptions& options,
        std::function<void(std::string_view)> event_callback = {});

    void stop_session();
    void pause();
    void resume();
    void step_over();
    void step_into();
    void step_out();

    [[nodiscard]] bool is_running() const noexcept { return m_active; }

private:
    bool m_active = false;
};

} // namespace Zenvra::Tools::Debugger
