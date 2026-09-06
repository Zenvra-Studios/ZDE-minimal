#include "Terminal/Win32PipeBackend.h"

#if defined(_WIN32)
#include <algorithm>
#include <vector>

namespace Zenvra::Terminal {

namespace {

std::wstring quote_arg(const std::wstring& arg) {
    if (arg.empty()) {
        return L"\"\"";
    }
    if (arg.front() == L'"' && arg.back() == L'"') {
        return arg;
    }
    return L'"' + arg + L'"';
}

} // namespace

Win32PipeBackend::Win32PipeBackend() = default;

Win32PipeBackend::~Win32PipeBackend() {
    stop();
}

bool Win32PipeBackend::start(
    const std::filesystem::path& executable,
    const std::wstring& arguments,
    const std::filesystem::path& working_directory,
    [[maybe_unused]] std::size_t columns,
    [[maybe_unused]] std::size_t rows) {
    stop();

    HANDLE pipe_in_read = nullptr;
    HANDLE pipe_out_write = nullptr;

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    if (CreatePipe(&pipe_in_read, &m_input_write, &sa, 0) == FALSE ||
        CreatePipe(&m_output_read, &pipe_out_write, &sa, 0) == FALSE) {
        if (pipe_in_read != nullptr) CloseHandle(pipe_in_read);
        if (pipe_out_write != nullptr) CloseHandle(pipe_out_write);
        terminal_debug_log("[Win32PipeBackend] Failed to create pipes.");
        return false;
    }

    SetHandleInformation(m_input_write, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(m_output_read, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(pipe_in_read, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    SetHandleInformation(pipe_out_write, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);

    STARTUPINFOW startup_info{};
    startup_info.cb = sizeof(startup_info);
    startup_info.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup_info.wShowWindow = SW_HIDE;
    startup_info.hStdInput = pipe_in_read;
    startup_info.hStdOutput = pipe_out_write;
    startup_info.hStdError = pipe_out_write;

    std::wstring full_command = quote_arg(executable.wstring()) + arguments;
    std::vector<wchar_t> mutable_command(full_command.begin(), full_command.end());
    mutable_command.push_back(L'\0');

    std::wstring dir_str = working_directory.wstring();
    const wchar_t* dir_ptr = dir_str.empty() ? nullptr : dir_str.c_str();

    PROCESS_INFORMATION process_info{};
    BOOL created = CreateProcessW(
        nullptr,
        mutable_command.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        dir_ptr,
        &startup_info,
        &process_info);

    if (created == FALSE && dir_ptr != nullptr) {
        created = CreateProcessW(
            nullptr,
            mutable_command.data(),
            nullptr,
            nullptr,
            TRUE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &startup_info,
            &process_info);
    }

    CloseHandle(pipe_in_read);
    CloseHandle(pipe_out_write);

    if (created == FALSE) {
        const DWORD err = GetLastError();
        terminal_debug_log("[Win32PipeBackend] CreateProcessW failed (Win32 error " +
                           std::to_string(err) + ")");
        stop();
        return false;
    }

    CloseHandle(process_info.hThread);
    m_process = process_info.hProcess;
    m_process_id = process_info.dwProcessId;

    terminal_debug_log("[Win32PipeBackend] CreateProcessW OK (PID " + std::to_string(m_process_id) + ")");

    if (WaitForSingleObject(m_process, 50) == WAIT_OBJECT_0) {
        DWORD early_exit_code = 0;
        GetExitCodeProcess(m_process, &early_exit_code);
        terminal_debug_log("[Win32PipeBackend] Pipe shell exited early at launch (exit code " +
                           std::to_string(early_exit_code) + ")");
        stop();
        return false;
    }

    m_start_time = std::chrono::steady_clock::now();
    m_running = true;
    return true;
}

void Win32PipeBackend::stop() noexcept {
    if (m_input_write != nullptr) {
        CloseHandle(m_input_write);
        m_input_write = nullptr;
    }
    if (m_output_read != nullptr) {
        CloseHandle(m_output_read);
        m_output_read = nullptr;
    }
    if (m_process != nullptr) {
        if (WaitForSingleObject(m_process, 20) == WAIT_TIMEOUT) {
            TerminateProcess(m_process, 0);
            WaitForSingleObject(m_process, 100);
        }
        CloseHandle(m_process);
        m_process = nullptr;
    }
    m_process_id = 0;
    m_running = false;
}

bool Win32PipeBackend::write_input(std::string_view text) {
    if (!m_running || m_input_write == nullptr || text.empty()) {
        return false;
    }
    DWORD bytes_written = 0;
    const BOOL ok = WriteFile(
        m_input_write,
        text.data(),
        static_cast<DWORD>(text.size()),
        &bytes_written,
        nullptr);
    return ok != FALSE && bytes_written == text.size();
}

std::size_t Win32PipeBackend::read_output(std::span<char> buffer) {
    if (!m_running || m_output_read == nullptr || buffer.empty()) {
        return 0;
    }
    DWORD available = 0;
    if (PeekNamedPipe(m_output_read, nullptr, 0, nullptr, &available, nullptr) == FALSE || available == 0) {
        return 0;
    }
    DWORD bytes_read = 0;
    const DWORD requested = std::min<DWORD>(available, static_cast<DWORD>(buffer.size()));
    if (ReadFile(m_output_read, buffer.data(), requested, &bytes_read, nullptr) == FALSE || bytes_read == 0) {
        return 0;
    }
    return static_cast<std::size_t>(bytes_read);
}

void Win32PipeBackend::resize([[maybe_unused]] std::size_t columns, [[maybe_unused]] std::size_t rows) noexcept {
    // Non-PTY pipes have no console window size buffer
}

bool Win32PipeBackend::is_running() const noexcept {
    return m_running;
}

bool Win32PipeBackend::check_exit(uint32_t& out_exit_code) {
    if (m_process == nullptr) {
        out_exit_code = 0;
        return true;
    }
    if (WaitForSingleObject(m_process, 0) == WAIT_OBJECT_0) {
        DWORD exit_code = 0;
        GetExitCodeProcess(m_process, &exit_code);
        out_exit_code = exit_code;
        m_running = false;
        return true;
    }
    return false;
}

} // namespace Zenvra::Terminal
#endif
