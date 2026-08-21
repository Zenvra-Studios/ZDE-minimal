#include "Language/Transport/StdioProcessTransport.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>

#if defined(_WIN32)
#include <windows.h>
#else
#include <csignal>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace Zenvra::Language::Transport
{

namespace
{
void transport_debug_log(const std::string& msg)
{
    std::error_code ec;
    const char* tmp = std::getenv("TEMP");
    const std::filesystem::path log_path =
        (tmp != nullptr ? std::filesystem::path(tmp)
                        : std::filesystem::current_path(ec)) /
        "zde-lsp.log";
    std::ofstream out(log_path, std::ios::app);
    if (out)
    {
        out << msg << '\n';
    }
}
} // namespace

struct StdioProcessTransport::ProcessHandle
{
#if defined(_WIN32)
    HANDLE job = nullptr;
    HANDLE process = nullptr;
    HANDLE thread = nullptr;
    HANDLE stdin_write = nullptr;
    HANDLE stdout_read = nullptr;
    HANDLE stderr_read = nullptr;
#else
    pid_t pid = -1;
    int stdin_write = -1;
    int stdout_read = -1;
    int stderr_read = -1;
#endif
};

StdioProcessTransport::StdioProcessTransport(
    std::filesystem::path executable_path,
    std::vector<std::string> arguments,
    std::filesystem::path working_directory)
    : m_executable_path(std::move(executable_path))
    , m_arguments(std::move(arguments))
    , m_working_directory(std::move(working_directory))
    , m_process(std::make_unique<ProcessHandle>())
{
}

StdioProcessTransport::~StdioProcessTransport()
{
    stop();
}

bool StdioProcessTransport::start()
{
    if (m_running.load())
    {
        return true;
    }

#if defined(_WIN32)
    SECURITY_ATTRIBUTES sa_attr{};
    sa_attr.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa_attr.bInheritHandle = TRUE;
    sa_attr.lpSecurityDescriptor = nullptr;

    HANDLE stdout_read_pipe = nullptr;
    HANDLE stdout_write_pipe = nullptr;
    if (!CreatePipe(&stdout_read_pipe, &stdout_write_pipe, &sa_attr, 0))
    {
        return false;
    }
    SetHandleInformation(stdout_read_pipe, HANDLE_FLAG_INHERIT, 0);

    HANDLE stdin_read_pipe = nullptr;
    HANDLE stdin_write_pipe = nullptr;
    if (!CreatePipe(&stdin_read_pipe, &stdin_write_pipe, &sa_attr, 0))
    {
        CloseHandle(stdout_read_pipe);
        CloseHandle(stdout_write_pipe);
        return false;
    }
    SetHandleInformation(stdin_write_pipe, HANDLE_FLAG_INHERIT, 0);

    HANDLE stderr_read_pipe = nullptr;
    HANDLE stderr_write_pipe = nullptr;
    if (!CreatePipe(&stderr_read_pipe, &stderr_write_pipe, &sa_attr, 0))
    {
        CloseHandle(stdout_read_pipe);
        CloseHandle(stdout_write_pipe);
        CloseHandle(stdin_read_pipe);
        CloseHandle(stdin_write_pipe);
        return false;
    }
    SetHandleInformation(stderr_read_pipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(STARTUPINFOW);
    si.hStdError = stderr_write_pipe;
    si.hStdOutput = stdout_write_pipe;
    si.hStdInput = stdin_read_pipe;
    si.dwFlags |= STARTF_USESTDHANDLES;

    PROCESS_INFORMATION pi{};

    std::wstring ext = m_executable_path.extension().wstring();
    for (auto& c : ext) c = towlower(c);
    const bool is_batch = (ext == L".cmd" || ext == L".bat");

    std::wstring cmd_line;
    if (is_batch)
    {
        cmd_line = L"cmd.exe /d /c \"\"" + m_executable_path.wstring() + L"\"";
        for (const auto& arg : m_arguments)
        {
            cmd_line += L" \"" + std::wstring(arg.begin(), arg.end()) + L"\"";
        }
        cmd_line += L"\"";
    }
    else
    {
        cmd_line = L"\"" + m_executable_path.wstring() + L"\"";
        for (const auto& arg : m_arguments)
        {
            cmd_line += L" \"" + std::wstring(arg.begin(), arg.end()) + L"\"";
        }
    }

    const wchar_t* work_dir = m_working_directory.empty() ? nullptr : m_working_directory.c_str();

    std::vector<wchar_t> cmd_buffer(cmd_line.begin(), cmd_line.end());
    cmd_buffer.push_back(L'\0');

    // Create Job Object with KILL_ON_JOB_CLOSE to ensure zero orphaned child processes
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job != nullptr)
    {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli{};
        jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
    }

    const BOOL success = CreateProcessW(
        nullptr,
        cmd_buffer.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        work_dir,
        &si,
        &pi
    );

    CloseHandle(stdout_write_pipe);
    CloseHandle(stdin_read_pipe);
    CloseHandle(stderr_write_pipe);

    if (!success)
    {
        const DWORD err = GetLastError();
        transport_debug_log("[zde-lsp] CreateProcessW FAILED err=" +
                            std::to_string(err) + " cmd=" +
                            std::string(cmd_line.begin(), cmd_line.end()));
        if (job != nullptr)
        {
            CloseHandle(job);
        }
        CloseHandle(stdout_read_pipe);
        CloseHandle(stdin_write_pipe);
        CloseHandle(stderr_read_pipe);
        return false;
    }

    transport_debug_log("[zde-lsp] CreateProcessW OK pid=" +
                        std::to_string(pi.dwProcessId));

    if (job != nullptr)
    {
        AssignProcessToJobObject(job, pi.hProcess);
        m_process->job = job;
    }

    m_process->process = pi.hProcess;
    m_process->thread = pi.hThread;
    m_process->stdin_write = stdin_write_pipe;
    m_process->stdout_read = stdout_read_pipe;
    m_process->stderr_read = stderr_read_pipe;

#else
    int stdin_pipes[2];
    int stdout_pipes[2];
    int stderr_pipes[2];

    if (pipe(stdin_pipes) < 0 || pipe(stdout_pipes) < 0 || pipe(stderr_pipes) < 0)
    {
        return false;
    }

    pid_t pid = fork();
    if (pid < 0)
    {
        return false;
    }

    if (pid == 0)
    {
        // Set new process group so entire process tree can be killed cleanly
        setpgid(0, 0);

        // Unblock signals in child
        sigset_t set;
        sigemptyset(&set);
        sigprocmask(SIG_SETMASK, &set, nullptr);

        // Child process
        dup2(stdin_pipes[0], STDIN_FILENO);
        dup2(stdout_pipes[1], STDOUT_FILENO);
        dup2(stderr_pipes[1], STDERR_FILENO);

        close(stdin_pipes[0]);
        close(stdin_pipes[1]);
        close(stdout_pipes[0]);
        close(stdout_pipes[1]);
        close(stderr_pipes[0]);
        close(stderr_pipes[1]);

        // Close any inherited file descriptors > 2
        const int max_fd = static_cast<int>(sysconf(_SC_OPEN_MAX));
        for (int fd = 3; fd < max_fd && fd < 256; ++fd)
        {
            close(fd);
        }

        if (!m_working_directory.empty())
        {
            chdir(m_working_directory.c_str());
        }

        std::vector<char*> argv;
        std::string exec_str = m_executable_path.string();
        argv.push_back(exec_str.data());
        for (auto& arg : m_arguments)
        {
            argv.push_back(arg.data());
        }
        argv.push_back(nullptr);

        execvp(exec_str.c_str(), argv.data());
        _exit(1);
    }

    close(stdin_pipes[0]);
    close(stdout_pipes[1]);
    close(stderr_pipes[1]);

    m_process->pid = pid;
    m_process->stdin_write = stdin_pipes[1];
    m_process->stdout_read = stdout_pipes[0];
    m_process->stderr_read = stderr_pipes[0];
#endif

    m_running.store(true);
    m_reader_thread = std::thread(&StdioProcessTransport::reader_thread_loop, this);
    m_stderr_thread = std::thread(&StdioProcessTransport::stderr_thread_loop, this);

    return true;
}

void StdioProcessTransport::stop()
{
    m_running.store(false);

#if defined(_WIN32)
    // 1. Close stdin to notify server of EOF
    if (m_process->stdin_write != nullptr)
    {
        CloseHandle(m_process->stdin_write);
        m_process->stdin_write = nullptr;
    }

    // 2. Kill the entire process tree immediately via Job Object and TerminateProcess
    if (m_process->job != nullptr)
    {
        TerminateJobObject(m_process->job, 0);
    }
    if (m_process->process != nullptr)
    {
        TerminateProcess(m_process->process, 0);
        WaitForSingleObject(m_process->process, 200);
    }

    // 3. Join reader and stderr threads before closing stdout/stderr pipe handles
    if (m_reader_thread.joinable())
    {
        if (m_reader_thread.get_id() != std::this_thread::get_id())
        {
            m_reader_thread.join();
        }
        else
        {
            m_reader_thread.detach();
        }
    }
    if (m_stderr_thread.joinable())
    {
        if (m_stderr_thread.get_id() != std::this_thread::get_id())
        {
            m_stderr_thread.join();
        }
        else
        {
            m_stderr_thread.detach();
        }
    }

    // 4. Safely close remaining handles
    if (m_process->process != nullptr)
    {
        CloseHandle(m_process->process);
        m_process->process = nullptr;
    }
    if (m_process->job != nullptr)
    {
        CloseHandle(m_process->job);
        m_process->job = nullptr;
    }
    if (m_process->thread != nullptr)
    {
        CloseHandle(m_process->thread);
        m_process->thread = nullptr;
    }
    if (m_process->stdout_read != nullptr)
    {
        CloseHandle(m_process->stdout_read);
        m_process->stdout_read = nullptr;
    }
    if (m_process->stderr_read != nullptr)
    {
        CloseHandle(m_process->stderr_read);
        m_process->stderr_read = nullptr;
    }
#else
    if (m_process->stdin_write >= 0)
    {
        close(m_process->stdin_write);
        m_process->stdin_write = -1;
    }
    if (m_process->pid > 0)
    {
        kill(-m_process->pid, SIGTERM);
        int status = 0;
        for (int i = 0; i < 20; ++i)
        {
            pid_t res = waitpid(m_process->pid, &status, WNOHANG);
            if (res == m_process->pid || res < 0)
            {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        kill(-m_process->pid, SIGKILL);
        waitpid(m_process->pid, nullptr, WNOHANG);
        m_process->pid = -1;
    }

    if (m_reader_thread.joinable())
    {
        if (m_reader_thread.get_id() != std::this_thread::get_id())
        {
            m_reader_thread.join();
        }
        else
        {
            m_reader_thread.detach();
        }
    }
    if (m_stderr_thread.joinable())
    {
        if (m_stderr_thread.get_id() != std::this_thread::get_id())
        {
            m_stderr_thread.join();
        }
        else
        {
            m_stderr_thread.detach();
        }
    }

    if (m_process->stdout_read >= 0)
    {
        close(m_process->stdout_read);
        m_process->stdout_read = -1;
    }
    if (m_process->stderr_read >= 0)
    {
        close(m_process->stderr_read);
        m_process->stderr_read = -1;
    }
#endif
}

bool StdioProcessTransport::is_running() const noexcept
{
    return m_running.load();
}

bool StdioProcessTransport::send(std::string_view payload)
{
    if (!m_running.load())
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_write_mutex);

#if defined(_WIN32)
    if (m_process->stdin_write == nullptr)
    {
        return false;
    }

    DWORD bytes_written = 0;
    DWORD total_written = 0;
    const auto* data_ptr = payload.data();
    const auto total_bytes = static_cast<DWORD>(payload.size());

    while (total_written < total_bytes)
    {
        if (!WriteFile(m_process->stdin_write, data_ptr + total_written, total_bytes - total_written, &bytes_written, nullptr))
        {
            return false;
        }
        total_written += bytes_written;
    }
    return true;
#else
    if (m_process->stdin_write < 0)
    {
        return false;
    }

    std::size_t total_written = 0;
    while (total_written < payload.size())
    {
        ssize_t written = write(m_process->stdin_write, payload.data() + total_written, payload.size() - total_written);
        if (written <= 0)
        {
            return false;
        }
        total_written += static_cast<std::size_t>(written);
    }
    return true;
#endif
}

void StdioProcessTransport::set_message_handler(std::function<void(std::string_view)> handler)
{
    m_message_handler = std::move(handler);
}

void StdioProcessTransport::set_error_handler(std::function<void(std::string_view)> handler)
{
    m_error_handler = std::move(handler);
}

void StdioProcessTransport::reader_thread_loop()
{
    std::string buffer;
    std::array<char, 4096> chunk{};

    while (m_running.load())
    {
#if defined(_WIN32)
        if (m_process->stdout_read == nullptr)
        {
            break;
        }
        DWORD bytes_read = 0;
        const BOOL success = ReadFile(m_process->stdout_read, chunk.data(), static_cast<DWORD>(chunk.size()), &bytes_read, nullptr);
        if (!success || bytes_read == 0)
        {
            break;
        }
        buffer.append(chunk.data(), bytes_read);
#else
        if (m_process->stdout_read < 0)
        {
            break;
        }
        ssize_t bytes_read = read(m_process->stdout_read, chunk.data(), chunk.size());
        if (bytes_read <= 0)
        {
            break;
        }
        buffer.append(chunk.data(), static_cast<std::size_t>(bytes_read));
#endif

        // Parse LSP JSON-RPC packets in the buffer: Content-Length: <n>\r\n\r\n<payload>
        while (true)
        {
            const std::size_t header_end = buffer.find("\r\n\r\n");
            std::size_t header_sep_len = 4;
            std::size_t content_start = 0;

            if (header_end == std::string::npos)
            {
                // Fallback for \n\n
                const std::size_t lf_header_end = buffer.find("\n\n");
                if (lf_header_end == std::string::npos)
                {
                    break;
                }
                header_sep_len = 2;
                content_start = lf_header_end + header_sep_len;
            }
            else
            {
                content_start = header_end + header_sep_len;
            }

            // Extract Content-Length
            const std::string_view header_view(buffer.data(), content_start - header_sep_len);
            const std::string_view cl_key = "Content-Length:";
            const std::size_t cl_pos = header_view.find(cl_key);
            if (cl_pos == std::string_view::npos)
            {
                // Invalid header, discard up to separator
                buffer.erase(0, content_start);
                continue;
            }

            std::size_t num_start = cl_pos + cl_key.size();
            while (num_start < header_view.size() && (header_view[num_start] == ' ' || header_view[num_start] == '\t'))
            {
                ++num_start;
            }

            std::size_t content_length = 0;
            try
            {
                content_length = std::stoull(std::string(header_view.substr(num_start)));
            }
            catch (...)
            {
                buffer.erase(0, content_start);
                continue;
            }

            // Check if full payload is present in buffer
            if (buffer.size() < content_start + content_length)
            {
                // Not enough bytes yet, wait for next read
                break;
            }

            const std::string_view payload(buffer.data() + content_start, content_length);
            if (m_message_handler)
            {
                m_message_handler(payload);
            }

            buffer.erase(0, content_start + content_length);
        }
    }

    m_running.store(false);
}

void StdioProcessTransport::stderr_thread_loop()
{
    std::array<char, 2048> chunk{};
    while (m_running.load())
    {
#if defined(_WIN32)
        if (m_process->stderr_read == nullptr)
        {
            break;
        }
        DWORD bytes_read = 0;
        const BOOL success = ReadFile(m_process->stderr_read, chunk.data(), static_cast<DWORD>(chunk.size()), &bytes_read, nullptr);
        if (!success || bytes_read == 0)
        {
            break;
        }
        const std::string_view err_view(chunk.data(), bytes_read);
        if (m_error_handler)
        {
            m_error_handler(err_view);
        }
#else
        if (m_process->stderr_read < 0)
        {
            break;
        }
        ssize_t bytes_read = read(m_process->stderr_read, chunk.data(), chunk.size());
        if (bytes_read <= 0)
        {
            break;
        }
        const std::string_view err_view(chunk.data(), static_cast<std::size_t>(bytes_read));
        if (m_error_handler)
        {
            m_error_handler(err_view);
        }
#endif
    }
}

} // namespace Zenvra::Language::Transport
