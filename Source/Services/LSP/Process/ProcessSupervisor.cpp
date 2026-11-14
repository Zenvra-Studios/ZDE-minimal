#include "Services/LSP/Process/ProcessSupervisor.h"

#include <array>
#include <iostream>
#include <system_error>

#if defined(_WIN32)
#include <windows.h>
#else
#include <csignal>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace Zenvra::Services::LSP::Process
{

struct ProcessSupervisor::Impl
{
#if defined(_WIN32)
    HANDLE job{nullptr};
    HANDLE process{nullptr};
    HANDLE thread{nullptr};
    HANDLE stdin_write{nullptr};
    HANDLE stdout_read{nullptr};
    HANDLE stderr_read{nullptr};
#else
    pid_t pid{-1};
    int stdin_write{-1};
    int stdout_read{-1};
    int stderr_read{-1};
#endif
    std::thread reader_thread;
    std::thread stderr_thread;
};

ProcessSupervisor::ProcessSupervisor()
    : m_impl(std::make_unique<Impl>())
{
}

ProcessSupervisor::~ProcessSupervisor()
{
    stop();
}

bool ProcessSupervisor::is_running() const noexcept
{
    return m_running.load();
}

void ProcessSupervisor::set_stdout_listener(StdoutCallback cb)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_stdout_listener = std::move(cb);
}

void ProcessSupervisor::set_stderr_listener(StderrCallback cb)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_stderr_listener = std::move(cb);
}

void ProcessSupervisor::set_exit_listener(ExitCallback cb)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_exit_listener = std::move(cb);
}

bool ProcessSupervisor::start(const ProcessSpec& spec)
{
    if (m_running.load())
    {
        stop();
    }

    m_spec = spec;
    if (m_spec.executable_path.empty() && !m_spec.executable.empty())
    {
        m_spec.executable_path = m_spec.executable;
    }
    if (m_spec.executable_path.empty()) return false;

#if defined(_WIN32)
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;

    HANDLE child_stdin_read = nullptr;
    HANDLE child_stdin_write = nullptr;
    HANDLE child_stdout_read = nullptr;
    HANDLE child_stdout_write = nullptr;
    HANDLE child_stderr_read = nullptr;
    HANDLE child_stderr_write = nullptr;

    if (!CreatePipe(&child_stdin_read, &child_stdin_write, &sa, 0)) return false;
    SetHandleInformation(child_stdin_write, HANDLE_FLAG_INHERIT, 0);

    if (!CreatePipe(&child_stdout_read, &child_stdout_write, &sa, 0))
    {
        CloseHandle(child_stdin_read);
        CloseHandle(child_stdin_write);
        return false;
    }
    SetHandleInformation(child_stdout_read, HANDLE_FLAG_INHERIT, 0);

    if (!CreatePipe(&child_stderr_read, &child_stderr_write, &sa, 0))
    {
        CloseHandle(child_stdin_read);
        CloseHandle(child_stdin_write);
        CloseHandle(child_stdout_read);
        CloseHandle(child_stdout_write);
        return false;
    }
    SetHandleInformation(child_stderr_read, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(STARTUPINFOW);
    si.hStdInput = child_stdin_read;
    si.hStdOutput = child_stdout_write;
    si.hStdError = child_stderr_write;
    si.dwFlags |= STARTF_USESTDHANDLES;

    PROCESS_INFORMATION pi{};

    // Build command line
    std::wstring cmd_line = L"\"" + m_spec.executable_path.wstring() + L"\"";
    for (const auto& arg : m_spec.arguments)
    {
        std::wstring warg(arg.begin(), arg.end());
        cmd_line += L" \"" + warg + L"\"";
    }

    std::wstring work_dir = m_spec.working_directory.empty()
        ? m_spec.executable_path.parent_path().wstring()
        : m_spec.working_directory.wstring();

    const DWORD flags = CREATE_NO_WINDOW | CREATE_SUSPENDED;
    std::vector<wchar_t> cmd_buf(cmd_line.begin(), cmd_line.end());
    cmd_buf.push_back(L'\0');

    BOOL ok = CreateProcessW(
        nullptr, cmd_buf.data(), nullptr, nullptr, TRUE, flags, nullptr,
        work_dir.empty() ? nullptr : work_dir.c_str(), &si, &pi);

    CloseHandle(child_stdin_read);
    CloseHandle(child_stdout_write);
    CloseHandle(child_stderr_write);

    if (!ok)
    {
        CloseHandle(child_stdin_write);
        CloseHandle(child_stdout_read);
        CloseHandle(child_stderr_read);
        return false;
    }

    m_impl->process = pi.hProcess;
    m_impl->thread = pi.hThread;
    m_impl->stdin_write = child_stdin_write;
    m_impl->stdout_read = child_stdout_read;
    m_impl->stderr_read = child_stderr_read;

    if (spec.create_job_object)
    {
        m_impl->job = CreateJobObjectW(nullptr, nullptr);
        if (m_impl->job)
        {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli{};
            jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            SetInformationJobObject(m_impl->job, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
            AssignProcessToJobObject(m_impl->job, pi.hProcess);
        }
    }

    ResumeThread(pi.hThread);
#else
    int in_pipe[2];
    int out_pipe[2];
    int err_pipe[2];

    if (pipe(in_pipe) != 0 || pipe(out_pipe) != 0 || pipe(err_pipe) != 0)
    {
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) return false;

    if (pid == 0)
    {
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(err_pipe[0]);

        dup2(in_pipe[0], STDIN_FILENO);
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(err_pipe[1], STDERR_FILENO);

        close(in_pipe[0]);
        close(out_pipe[1]);
        close(err_pipe[1]);

        if (!m_spec.working_directory.empty())
        {
            chdir(m_spec.working_directory.c_str());
        }

        std::vector<std::string> args_str;
        args_str.push_back(m_spec.executable_path.string());
        for (const auto& a : m_spec.arguments) args_str.push_back(a);

        std::vector<char*> argv;
        for (auto& s : args_str) argv.push_back(s.data());
        argv.push_back(nullptr);

        execvp(argv[0], argv.data());
        _exit(127);
    }

    close(in_pipe[0]);
    close(out_pipe[1]);
    close(err_pipe[1]);

    m_impl->pid = pid;
    m_impl->stdin_write = in_pipe[1];
    m_impl->stdout_read = out_pipe[0];
    m_impl->stderr_read = err_pipe[0];
#endif

    m_running.store(true);

    // Stdout reader thread
    m_impl->reader_thread = std::thread([this]() {
        std::array<char, 4096> buffer{};
#if defined(_WIN32)
        DWORD bytes_read = 0;
        while (m_running.load() && m_impl->stdout_read)
        {
            if (!ReadFile(m_impl->stdout_read, buffer.data(), static_cast<DWORD>(buffer.size()), &bytes_read, nullptr) || bytes_read == 0)
            {
                break;
            }
            StdoutCallback cb;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                cb = m_stdout_listener;
            }
            if (cb)
            {
                cb(std::string_view(buffer.data(), bytes_read));
            }
        }
#else
        ssize_t bytes_read = 0;
        while (m_running.load() && m_impl->stdout_read >= 0)
        {
            bytes_read = read(m_impl->stdout_read, buffer.data(), buffer.size());
            if (bytes_read <= 0) break;
            StdoutCallback cb;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                cb = m_stdout_listener;
            }
            if (cb)
            {
                cb(std::string_view(buffer.data(), bytes_read));
            }
        }
#endif
        m_running.store(false);

        ExitCallback exit_cb;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            exit_cb = m_exit_listener;
        }
        if (exit_cb)
        {
            exit_cb(0);
        }
    });

    // Stderr reader thread
    m_impl->stderr_thread = std::thread([this]() {
        std::array<char, 4096> buffer{};
#if defined(_WIN32)
        DWORD bytes_read = 0;
        while (m_running.load() && m_impl->stderr_read)
        {
            if (!ReadFile(m_impl->stderr_read, buffer.data(), static_cast<DWORD>(buffer.size()), &bytes_read, nullptr) || bytes_read == 0)
            {
                break;
            }
            StderrCallback cb;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                cb = m_stderr_listener;
            }
            if (cb)
            {
                cb(std::string_view(buffer.data(), bytes_read));
            }
        }
#else
        ssize_t bytes_read = 0;
        while (m_running.load() && m_impl->stderr_read >= 0)
        {
            bytes_read = read(m_impl->stderr_read, buffer.data(), buffer.size());
            if (bytes_read <= 0) break;
            StderrCallback cb;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                cb = m_stderr_listener;
            }
            if (cb)
            {
                cb(std::string_view(buffer.data(), bytes_read));
            }
        }
#endif
    });

    return true;
}

bool ProcessSupervisor::write_input(std::string_view data)
{
    if (!m_running.load() || data.empty()) return false;

#if defined(_WIN32)
    if (!m_impl->stdin_write) return false;
    DWORD written = 0;
    return WriteFile(m_impl->stdin_write, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) && (written == data.size());
#else
    if (m_impl->stdin_write < 0) return false;
    ssize_t written = write(m_impl->stdin_write, data.data(), data.size());
    return written == static_cast<ssize_t>(data.size());
#endif
}

void ProcessSupervisor::stop(std::chrono::milliseconds /*timeout*/)
{
    m_running.store(false);

#if defined(_WIN32)
    if (m_impl->stdin_write) { CloseHandle(m_impl->stdin_write); m_impl->stdin_write = nullptr; }
    if (m_impl->stdout_read) { CloseHandle(m_impl->stdout_read); m_impl->stdout_read = nullptr; }
    if (m_impl->stderr_read) { CloseHandle(m_impl->stderr_read); m_impl->stderr_read = nullptr; }

    if (m_impl->process)
    {
        WaitForSingleObject(m_impl->process, 500);
        TerminateProcess(m_impl->process, 0);
        CloseHandle(m_impl->process);
        m_impl->process = nullptr;
    }
    if (m_impl->thread) { CloseHandle(m_impl->thread); m_impl->thread = nullptr; }
    if (m_impl->job) { CloseHandle(m_impl->job); m_impl->job = nullptr; }
#else
    if (m_impl->stdin_write >= 0) { close(m_impl->stdin_write); m_impl->stdin_write = -1; }
    if (m_impl->stdout_read >= 0) { close(m_impl->stdout_read); m_impl->stdout_read = -1; }
    if (m_impl->stderr_read >= 0) { close(m_impl->stderr_read); m_impl->stderr_read = -1; }

    if (m_impl->pid > 0)
    {
        kill(m_impl->pid, SIGTERM);
        waitpid(m_impl->pid, nullptr, WNOHANG);
        m_impl->pid = -1;
    }
#endif

    if (m_impl->reader_thread.joinable()) m_impl->reader_thread.join();
    if (m_impl->stderr_thread.joinable()) m_impl->stderr_thread.join();
}

int ProcessSupervisor::wait_for_exit()
{
#if defined(_WIN32)
    if (!m_impl->process) return -1;
    WaitForSingleObject(m_impl->process, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(m_impl->process, &code);
    m_running.store(false);
    return static_cast<int>(code);
#else
    if (m_impl->pid <= 0) return -1;
    int status = 0;
    waitpid(m_impl->pid, &status, 0);
    m_running.store(false);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

void ProcessSupervisor::kill()
{
    stop();
}

} // namespace Zenvra::Services::LSP::Process

