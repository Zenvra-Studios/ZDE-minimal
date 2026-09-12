#include "Tools/Runner/ProcessRunner.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace Zenvra::Tools::Runner
{

ProcessRunner::~ProcessRunner()
{
    terminate_active_process();
    if (m_reader_thread.joinable())
    {
        m_reader_thread.join();
    }
}

void ProcessRunner::terminate_active_process() const
{
    std::lock_guard<std::mutex> lock(m_runner_mutex);
#if defined(_WIN32)
    if (m_process_handle != nullptr)
    {
        TerminateProcess(static_cast<HANDLE>(m_process_handle), 1);
        CloseHandle(static_cast<HANDLE>(m_process_handle));
        m_process_handle = nullptr;
    }
#else
    if (m_process_handle != nullptr)
    {
        const pid_t pid = static_cast<pid_t>(reinterpret_cast<std::intptr_t>(m_process_handle));
        kill(pid, SIGTERM);
        m_process_handle = nullptr;
    }
#endif
}

bool ProcessRunner::launch_process(
    const ProcessExecutionOptions& options,
    std::function<void(std::string_view)> stdout_callback) const
{
    std::error_code ec;
    bool executable_valid = std::filesystem::exists(options.executable_path, ec);
    std::filesystem::path resolved_path = options.executable_path;

    if (!executable_valid && !options.executable_path.has_parent_path() && !options.executable_path.empty())
    {
#if defined(_WIN32)
        const std::wstring cmd_name = options.executable_path.wstring();
        wchar_t found_path[MAX_PATH];
        if (SearchPathW(nullptr, cmd_name.c_str(), L".exe", MAX_PATH, found_path, nullptr) > 0 ||
            SearchPathW(nullptr, cmd_name.c_str(), nullptr, MAX_PATH, found_path, nullptr) > 0)
        {
            executable_valid = true;
            resolved_path = std::filesystem::path(found_path);
        }
#else
        executable_valid = true;
#endif
    }

    if (!executable_valid)
    {
        return false;
    }

    std::ostringstream cmd;
    cmd << "\"" << resolved_path.string() << "\"";
    for (const auto& arg : options.arguments)
    {
        cmd << " " << arg;
    }

#if defined(_WIN32)
    HANDLE h_stdout_read = nullptr;
    HANDLE h_stdout_write = nullptr;

    const bool redirect = static_cast<bool>(stdout_callback);
    if (redirect)
    {
        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(SECURITY_ATTRIBUTES);
        sa.bInheritHandle = TRUE;
        sa.lpSecurityDescriptor = nullptr;

        if (!CreatePipe(&h_stdout_read, &h_stdout_write, &sa, 0))
        {
            return false;
        }
        SetHandleInformation(h_stdout_read, HANDLE_FLAG_INHERIT, 0);
    }

    const std::string cmd_str = cmd.str();
    std::wstring wcmd(cmd_str.begin(), cmd_str.end());
    std::vector<wchar_t> buf(wcmd.begin(), wcmd.end());
    buf.push_back(L'\0');

    std::wstring w_cwd;
    const wchar_t* lp_cwd = nullptr;
    if (!options.working_directory.empty())
    {
        w_cwd = options.working_directory.wstring();
        lp_cwd = w_cwd.c_str();
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    if (redirect)
    {
        si.dwFlags |= STARTF_USESTDHANDLES;
        si.hStdOutput = h_stdout_write;
        si.hStdError = h_stdout_write;
    }

    PROCESS_INFORMATION pi{};
    const DWORD creation_flags = redirect ? CREATE_NO_WINDOW : (options.run_in_background ? DETACHED_PROCESS | CREATE_NO_WINDOW : 0);

    const BOOL ok = CreateProcessW(
        nullptr, buf.data(), nullptr, nullptr, redirect ? TRUE : FALSE,
        creation_flags, nullptr, lp_cwd, &si, &pi);

    if (redirect)
    {
        CloseHandle(h_stdout_write);
    }

    if (!ok)
    {
        if (redirect)
        {
            CloseHandle(h_stdout_read);
        }
        return false;
    }

    CloseHandle(pi.hThread);

    {
        std::lock_guard<std::mutex> lock(m_runner_mutex);
        m_process_handle = pi.hProcess;
    }

    if (redirect)
    {
        auto reader = [h_read = h_stdout_read, cb = std::move(stdout_callback), proc = pi.hProcess]() {
            std::array<char, 1024> buffer{};
            DWORD bytes_read = 0;
            while (ReadFile(h_read, buffer.data(), static_cast<DWORD>(buffer.size()), &bytes_read, nullptr) && bytes_read > 0)
            {
                cb(std::string_view(buffer.data(), bytes_read));
            }
            CloseHandle(h_read);
            WaitForSingleObject(proc, INFINITE);
            CloseHandle(proc);
        };

        if (options.run_in_background)
        {
            std::thread(std::move(reader)).detach();
        }
        else
        {
            reader();
        }
        return true;
    }

    if (!options.run_in_background)
    {
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD exit_code = 0;
        GetExitCodeProcess(pi.hProcess, &exit_code);
        CloseHandle(pi.hProcess);
        {
            std::lock_guard<std::mutex> lock(m_runner_mutex);
            m_process_handle = nullptr;
        }
        return (exit_code == 0);
    }

    return true;
#else
    if (options.run_in_background)
    {
        cmd << " &";
    }
    const int res = std::system(cmd.str().c_str());
    return (res == 0);
#endif
}

} // namespace Zenvra::Tools::Runner
