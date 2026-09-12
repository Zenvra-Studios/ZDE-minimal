#include "Services/Process/ProcessRunner.h"

#include <array>
#include <iostream>
#include <sstream>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace Zenvra::Services::Process
{

ProcessRunner::~ProcessRunner()
{
#if defined(_WIN32)
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& [id, handle] : m_active_processes)
    {
        if (handle)
        {
            TerminateProcess(static_cast<HANDLE>(handle), 1);
            CloseHandle(static_cast<HANDLE>(handle));
        }
    }
    m_active_processes.clear();
#endif
}

std::optional<std::filesystem::path> ProcessRunner::resolve_executable(
    const std::filesystem::path& exec,
    const std::filesystem::path& working_dir)
{
    std::error_code ec;
    if (!working_dir.empty())
    {
        const auto in_wd = working_dir / exec;
        if (std::filesystem::exists(in_wd, ec))
        {
            return in_wd;
        }
    }

    if (std::filesystem::exists(exec, ec))
    {
        return exec;
    }

    if (!exec.has_parent_path() && !exec.empty())
    {
#if defined(_WIN32)
        const std::wstring cmd_name = exec.wstring();
        wchar_t found_path[MAX_PATH];
        if (SearchPathW(nullptr, cmd_name.c_str(), L".exe", MAX_PATH, found_path, nullptr) > 0 ||
            SearchPathW(nullptr, cmd_name.c_str(), nullptr, MAX_PATH, found_path, nullptr) > 0)
        {
            return std::filesystem::path(found_path);
        }
#else
        return exec;
#endif
    }

    return std::nullopt;
}

ProcessHandle ProcessRunner::start(
    const ProcessSpec& spec,
    std::function<void(std::string_view)> on_stdout,
    std::function<void(std::string_view)> on_stderr)
{
    const auto resolved = resolve_executable(spec.executable, spec.working_directory);
    if (!resolved)
    {
        return {};
    }

    std::ostringstream cmd_stream;
    cmd_stream << "\"" << resolved->string() << "\"";
    for (const auto& arg : spec.arguments)
    {
        cmd_stream << " " << arg;
    }
    const std::string cmd_str = cmd_stream.str();

#if defined(_WIN32)
    HANDLE h_stdout_read = nullptr;
    HANDLE h_stdout_write = nullptr;
    HANDLE h_stderr_read = nullptr;
    HANDLE h_stderr_write = nullptr;

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    const bool redirect = spec.redirect_stdout && (on_stdout || on_stderr);
    if (redirect)
    {
        CreatePipe(&h_stdout_read, &h_stdout_write, &sa, 0);
        SetHandleInformation(h_stdout_read, HANDLE_FLAG_INHERIT, 0);

        CreatePipe(&h_stderr_read, &h_stderr_write, &sa, 0);
        SetHandleInformation(h_stderr_read, HANDLE_FLAG_INHERIT, 0);
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    if (redirect)
    {
        si.dwFlags |= STARTF_USESTDHANDLES;
        si.hStdOutput = h_stdout_write;
        si.hStdError = h_stderr_write;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    }

    PROCESS_INFORMATION pi{};
    std::wstring wcmd(cmd_str.begin(), cmd_str.end());
    std::vector<wchar_t> buf(wcmd.begin(), wcmd.end());
    buf.push_back(L'\0');

    std::wstring w_cwd;
    const wchar_t* lp_cwd = nullptr;
    if (!spec.working_directory.empty())
    {
        w_cwd = spec.working_directory.wstring();
        lp_cwd = w_cwd.c_str();
    }

    DWORD flags = CREATE_NO_WINDOW;
    if (spec.run_in_background)
    {
        flags |= DETACHED_PROCESS;
    }

    const BOOL ok = CreateProcessW(
        nullptr, buf.data(), nullptr, nullptr, redirect ? TRUE : FALSE,
        flags, nullptr, lp_cwd, &si, &pi);

    if (h_stdout_write) CloseHandle(h_stdout_write);
    if (h_stderr_write) CloseHandle(h_stderr_write);

    if (!ok)
    {
        if (h_stdout_read) CloseHandle(h_stdout_read);
        if (h_stderr_read) CloseHandle(h_stderr_read);
        return {};
    }

    CloseHandle(pi.hThread);

    std::uint64_t handle_id = 0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        handle_id = m_next_id++;
        m_active_processes[handle_id] = pi.hProcess;
    }

    if (redirect)
    {
        std::thread([h_read = h_stdout_read, cb = std::move(on_stdout)] {
            std::array<char, 2048> buffer;
            DWORD bytes_read = 0;
            while (ReadFile(h_read, buffer.data(), static_cast<DWORD>(buffer.size() - 1), &bytes_read, nullptr) && bytes_read > 0)
            {
                buffer[bytes_read] = '\0';
                if (cb) cb(std::string_view(buffer.data(), bytes_read));
            }
            CloseHandle(h_read);
        }).detach();

        std::thread([h_read = h_stderr_read, cb = std::move(on_stderr)] {
            std::array<char, 2048> buffer;
            DWORD bytes_read = 0;
            while (ReadFile(h_read, buffer.data(), static_cast<DWORD>(buffer.size() - 1), &bytes_read, nullptr) && bytes_read > 0)
            {
                buffer[bytes_read] = '\0';
                if (cb) cb(std::string_view(buffer.data(), bytes_read));
            }
            CloseHandle(h_read);
        }).detach();
    }

    return ProcessHandle{handle_id, pi.hProcess};
#else
    return {};
#endif
}

ProcessResult ProcessRunner::run(const ProcessSpec& spec)
{
    ProcessResult result;
    auto cb_out = [&](std::string_view text) {
        result.stdout_output.append(text.data(), text.size());
    };
    auto cb_err = [&](std::string_view text) {
        result.stderr_output.append(text.data(), text.size());
    };

    ProcessSpec run_spec = spec;
    run_spec.run_in_background = false;

    const auto handle = start(run_spec, cb_out, cb_err);
    if (!handle.is_valid())
    {
        result.success = false;
        result.exit_code = -1;
        return result;
    }

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

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_active_processes.erase(handle.id);
    }
    CloseHandle(h_proc);
#endif

    return result;
}

bool ProcessRunner::terminate(ProcessHandle handle)
{
#if defined(_WIN32)
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_active_processes.find(handle.id);
    if (it != m_active_processes.end() && it->second)
    {
        auto* h_proc = static_cast<HANDLE>(it->second);
        const BOOL res = TerminateProcess(h_proc, 1);
        CloseHandle(h_proc);
        m_active_processes.erase(it);
        return (res != FALSE);
    }
#endif
    return false;
}

bool ProcessRunner::is_running(ProcessHandle handle)
{
#if defined(_WIN32)
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_active_processes.find(handle.id);
    if (it != m_active_processes.end() && it->second)
    {
        DWORD code = 0;
        if (GetExitCodeProcess(static_cast<HANDLE>(it->second), &code))
        {
            return (code == STILL_ACTIVE);
        }
    }
#endif
    return false;
}

} // namespace Zenvra::Services::Process
