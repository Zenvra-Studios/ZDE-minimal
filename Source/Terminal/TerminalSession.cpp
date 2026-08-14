#include "Terminal/TerminalSession.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <string_view>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#else
#include <csignal>
#include <fcntl.h>
#if defined(__APPLE__)
#include <util.h>
#else
#include <pty.h>
#endif
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace Zenvra::Terminal
{

namespace
{

constexpr std::size_t maximum_scrollback_lines = 4000;

bool is_printable_input(std::string_view text) noexcept
{
    return !text.empty() && static_cast<unsigned char>(text.front()) >= 0x20U &&
        text.front() != '\x7F';
}

void remove_last_utf8_code_point(std::string& text) noexcept
{
    if (text.empty())
    {
        return;
    }
    text.pop_back();
    while (!text.empty() &&
           (static_cast<unsigned char>(text.back()) & 0xC0U) == 0x80U)
    {
        text.pop_back();
    }
}

bool is_clear_command(std::string_view command) noexcept
{
    while (!command.empty() &&
           (command.front() == ' ' || command.front() == '\t'))
    {
        command.remove_prefix(1);
    }
    while (!command.empty() &&
           (command.back() == ' ' || command.back() == '\t'))
    {
        command.remove_suffix(1);
    }
    const auto equals_ignore_case = [](std::string_view left, std::string_view right) {
        if (left.size() != right.size())
        {
            return false;
        }
        for (std::size_t index = 0; index < left.size(); ++index)
        {
            if (std::tolower(static_cast<unsigned char>(left[index])) !=
                std::tolower(static_cast<unsigned char>(right[index])))
            {
                return false;
            }
        }
        return true;
    };
    return equals_ignore_case(command, "clear") || equals_ignore_case(command, "cls");
}

#if defined(_WIN32)
std::wstring quote_windows_argument(const std::wstring& argument)
{
    return L'"' + argument + L'"';
}

std::filesystem::path find_windows_executable(const wchar_t* executable)
{
    std::array<wchar_t, 32768> resolved{};
    const DWORD length = SearchPathW(
        nullptr, executable, nullptr, static_cast<DWORD>(resolved.size()), resolved.data(), nullptr);
    return length > 0 && length < resolved.size()
        ? std::filesystem::path{resolved.data()}
        : std::filesystem::path{};
}

bool is_windows_shell(const std::filesystem::path& path, const wchar_t* filename)
{
    const std::wstring actual = path.filename().wstring();
    return CompareStringOrdinal(actual.c_str(), -1, filename, -1, TRUE) == CSTR_EQUAL;
}

std::wstring windows_shell_arguments(const std::filesystem::path& shell_path)
{
    if (is_windows_shell(shell_path, L"bash.exe"))
    {
        return L" --noprofile --norc -i";
    }
    if (is_windows_shell(shell_path, L"cmd.exe"))
    {
        return L" /D /Q /K";
    }
    return L" -NoLogo -NoProfile -NoExit -Command -";
}

#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE 0x00020016
#endif

using PFN_CreatePseudoConsole = HRESULT(WINAPI*)(COORD, HANDLE, HANDLE, DWORD, HPCON*);
using PFN_ResizePseudoConsole = HRESULT(WINAPI*)(HPCON, COORD);
using PFN_ClosePseudoConsole = VOID(WINAPI*)(HPCON);

static PFN_CreatePseudoConsole s_fn_CreatePseudoConsole = nullptr;
static PFN_ResizePseudoConsole s_fn_ResizePseudoConsole = nullptr;
static PFN_ClosePseudoConsole s_fn_ClosePseudoConsole = nullptr;
static bool s_conpty_initialized = false;

static void load_conpty_api()
{
    if (s_conpty_initialized)
    {
        return;
    }
    s_conpty_initialized = true;
    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    if (kernel32 != nullptr)
    {
        s_fn_CreatePseudoConsole = reinterpret_cast<PFN_CreatePseudoConsole>(
            GetProcAddress(kernel32, "CreatePseudoConsole"));
        s_fn_ResizePseudoConsole = reinterpret_cast<PFN_ResizePseudoConsole>(
            GetProcAddress(kernel32, "ResizePseudoConsole"));
        s_fn_ClosePseudoConsole = reinterpret_cast<PFN_ClosePseudoConsole>(
            GetProcAddress(kernel32, "ClosePseudoConsole"));
    }
}
#endif

} // namespace

struct TerminalSession::Implementation
{
#if defined(_WIN32)
    HANDLE process = nullptr;
    HANDLE input_write = nullptr;
    HANDLE output_read = nullptr;
    HPCON pseudo_console = nullptr;
    LPPROC_THREAD_ATTRIBUTE_LIST attribute_list = nullptr;
    std::vector<BYTE> attribute_buffer;
#else
    int master_fd = -1;
    pid_t process_id = -1;
#endif
};

TerminalSession::TerminalSession()
    : m_implementation(std::make_unique<Implementation>())
{
}

TerminalSession::~TerminalSession()
{
    stop();
}

std::filesystem::path TerminalSession::resolve_host_shell()
{
#if defined(_WIN32)
    // Command Prompt behaves interactively over the redirected pipes used by
    // this lightweight terminal host. PowerShell and Bash remain fallbacks.
    std::array<wchar_t, 32768> comspec{};
    const DWORD comspec_length = GetEnvironmentVariableW(
        L"COMSPEC", comspec.data(), static_cast<DWORD>(comspec.size()));
    if (comspec_length > 0 && comspec_length < comspec.size() &&
        GetFileAttributesW(comspec.data()) != INVALID_FILE_ATTRIBUTES)
    {
        return std::filesystem::path{comspec.data()};
    }
    if (const std::filesystem::path command_prompt = find_windows_executable(L"cmd.exe");
        !command_prompt.empty())
    {
        return command_prompt;
    }

    for (const wchar_t* executable : {L"pwsh.exe", L"powershell.exe"})
    {
        if (const std::filesystem::path resolved = find_windows_executable(executable);
            !resolved.empty())
        {
            return resolved;
        }
    }

    constexpr std::array<std::wstring_view, 3> bash_candidates{
        L"C:\\msys64\\usr\\bin\\bash.exe",
        L"C:\\Program Files\\Git\\bin\\bash.exe",
        L"C:\\Program Files\\Git\\usr\\bin\\bash.exe",
    };
    for (const std::wstring_view candidate : bash_candidates)
    {
        if (GetFileAttributesW(std::wstring{candidate}.c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            return std::filesystem::path{candidate};
        }
    }
    return find_windows_executable(L"bash.exe");
#else
    constexpr std::array<std::string_view, 3> candidates{
        "/usr/bash",
        "/usr/bin/bash",
        "/bin/bash",
    };
    for (const std::string_view candidate : candidates)
    {
        if (::access(std::string{candidate}.c_str(), X_OK) == 0)
        {
            return std::filesystem::path{candidate};
        }
    }
    return {};
#endif
}

bool TerminalSession::start(const std::filesystem::path& working_directory)
{
    stop();
    m_lines.assign(1, std::string{});
    m_cursor_line = 0;
    m_cursor_column = 0;
    m_saved_cursor_line = 0;
    m_saved_cursor_column = 0;
    m_input_start_column = 0;
    m_pending_input.clear();
    m_command_history.clear();
    m_history_index.reset();
    m_saved_pending_input.clear();
    m_parser_state = ParserState::Text;
    m_control_sequence.clear();
    m_columns = 100;
    m_rows = 24;
    m_shell_path = resolve_host_shell();
    if (m_shell_path.empty())
    {
        append_status("[Unable to find a local shell executable]");
        return false;
    }

#if defined(_WIN32)
    load_conpty_api();

    HANDLE input_read = nullptr;
    HANDLE output_write = nullptr;

    if (CreatePipe(&input_read, &m_implementation->input_write, nullptr, 0) == FALSE ||
        CreatePipe(&m_implementation->output_read, &output_write, nullptr, 0) == FALSE)
    {
        if (input_read != nullptr) CloseHandle(input_read);
        if (output_write != nullptr) CloseHandle(output_write);
        stop();
        append_status("[Unable to create terminal pipes]");
        return false;
    }

    std::wstring command = quote_windows_argument(m_shell_path.wstring()) +
        windows_shell_arguments(m_shell_path);
    const std::wstring directory = working_directory.empty()
        ? std::wstring{}
        : working_directory.wstring();
    const wchar_t* directory_pointer = directory.empty() ? nullptr : directory.c_str();

    bool conpty_started = false;
    if (s_fn_CreatePseudoConsole != nullptr && s_fn_ResizePseudoConsole != nullptr && s_fn_ClosePseudoConsole != nullptr)
    {
        COORD console_size{
            static_cast<SHORT>(std::clamp<std::size_t>(m_columns, 1, 32767)),
            static_cast<SHORT>(std::clamp<std::size_t>(m_rows, 1, 32767))
        };
        HPCON hPC = nullptr;
        if (SUCCEEDED(s_fn_CreatePseudoConsole(console_size, input_read, output_write, 0, &hPC)))
        {
            m_implementation->pseudo_console = hPC;
            CloseHandle(input_read);
            input_read = nullptr;
            CloseHandle(output_write);
            output_write = nullptr;

            SIZE_T attr_list_size = 0;
            InitializeProcThreadAttributeList(nullptr, 1, 0, &attr_list_size);
            if (attr_list_size > 0)
            {
                m_implementation->attribute_buffer.resize(attr_list_size);
                m_implementation->attribute_list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(
                    m_implementation->attribute_buffer.data());
                if (InitializeProcThreadAttributeList(m_implementation->attribute_list, 1, 0, &attr_list_size) != FALSE)
                {
                    if (UpdateProcThreadAttribute(
                            m_implementation->attribute_list, 0,
                            PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                            m_implementation->pseudo_console,
                            sizeof(HPCON), nullptr, nullptr) != FALSE)
                    {
                        STARTUPINFOEXW startup_ex{};
                        startup_ex.StartupInfo.cb = sizeof(STARTUPINFOEXW);
                        startup_ex.lpAttributeList = m_implementation->attribute_list;

                        PROCESS_INFORMATION process_info{};
                        std::vector<wchar_t> mutable_command(command.begin(), command.end());
                        mutable_command.push_back(L'\0');

                        if (CreateProcessW(
                                nullptr, mutable_command.data(), nullptr, nullptr, FALSE,
                                EXTENDED_STARTUPINFO_PRESENT, nullptr,
                                directory_pointer, &startup_ex.StartupInfo, &process_info) != FALSE)
                        {
                            CloseHandle(process_info.hThread);
                            m_implementation->process = process_info.hProcess;
                            conpty_started = true;
                        }
                    }
                }
            }
        }
    }

    if (!conpty_started)
    {
        if (input_read == nullptr || output_write == nullptr)
        {
            if (m_implementation->input_write != nullptr) { CloseHandle(m_implementation->input_write); m_implementation->input_write = nullptr; }
            if (m_implementation->output_read != nullptr) { CloseHandle(m_implementation->output_read); m_implementation->output_read = nullptr; }
            SECURITY_ATTRIBUTES security_attributes{};
            security_attributes.nLength = sizeof(security_attributes);
            security_attributes.bInheritHandle = TRUE;
            CreatePipe(&input_read, &m_implementation->input_write, &security_attributes, 0);
            CreatePipe(&m_implementation->output_read, &output_write, &security_attributes, 0);
        }
        SetHandleInformation(m_implementation->input_write, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(m_implementation->output_read, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(input_read, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
        SetHandleInformation(output_write, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_HIDE;
        startup.hStdInput = input_read;
        startup.hStdOutput = output_write;
        startup.hStdError = output_write;

        PROCESS_INFORMATION process_info{};
        std::vector<wchar_t> mutable_command(command.begin(), command.end());
        mutable_command.push_back(L'\0');

        const BOOL created = CreateProcessW(
            nullptr, mutable_command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
            nullptr, directory_pointer, &startup, &process_info);

        if (input_read != nullptr) CloseHandle(input_read);
        if (output_write != nullptr) CloseHandle(output_write);

        if (created == FALSE)
        {
            stop();
            append_status("[Unable to start the local shell]");
            return false;
        }
        CloseHandle(process_info.hThread);
        m_implementation->process = process_info.hProcess;
    }
#else
    winsize terminal_size{};
    terminal_size.ws_col = static_cast<unsigned short>(m_columns);
    terminal_size.ws_row = static_cast<unsigned short>(m_rows);
    const pid_t process_id = ::forkpty(
        &m_implementation->master_fd, nullptr, nullptr, &terminal_size);
    if (process_id < 0)
    {
        m_implementation->master_fd = -1;
        append_status("[Unable to create the local terminal PTY]");
        return false;
    }
    if (process_id == 0)
    {
        if (!working_directory.empty())
        {
            static_cast<void>(::chdir(working_directory.c_str()));
        }
        ::setenv("TERM", "xterm-256color", 1);
        ::setenv("COLORTERM", "truecolor", 1);
        ::setenv("PS1", "\\u@\\h:\\w\\$ ", 1);
        const std::string executable = m_shell_path.string();
        ::execl(executable.c_str(), executable.c_str(), "--noprofile", "--norc", "-i", nullptr);
        ::_exit(127);
    }
    m_implementation->process_id = process_id;
    const int current_flags = ::fcntl(m_implementation->master_fd, F_GETFL, 0);
    if (current_flags >= 0)
    {
        static_cast<void>(::fcntl(
            m_implementation->master_fd, F_SETFL, current_flags | O_NONBLOCK));
    }
#endif
    m_running = true;
    return true;
}

void TerminalSession::stop() noexcept
{
    if (!m_implementation)
    {
        return;
    }
#if defined(_WIN32)
    if (m_implementation->pseudo_console != nullptr)
    {
        if (s_fn_ClosePseudoConsole != nullptr)
        {
            s_fn_ClosePseudoConsole(m_implementation->pseudo_console);
        }
        m_implementation->pseudo_console = nullptr;
    }
    if (m_implementation->attribute_list != nullptr)
    {
        DeleteProcThreadAttributeList(m_implementation->attribute_list);
        m_implementation->attribute_list = nullptr;
        m_implementation->attribute_buffer.clear();
    }
    if (m_implementation->input_write != nullptr)
    {
        CloseHandle(m_implementation->input_write);
        m_implementation->input_write = nullptr;
    }
    if (m_implementation->output_read != nullptr)
    {
        CloseHandle(m_implementation->output_read);
        m_implementation->output_read = nullptr;
    }
    if (m_implementation->process != nullptr)
    {
        if (WaitForSingleObject(m_implementation->process, 20) == WAIT_TIMEOUT)
        {
            TerminateProcess(m_implementation->process, 0);
            WaitForSingleObject(m_implementation->process, 100);
        }
        CloseHandle(m_implementation->process);
        m_implementation->process = nullptr;
    }
#else
    if (m_implementation->master_fd >= 0)
    {
        ::close(m_implementation->master_fd);
        m_implementation->master_fd = -1;
    }
    if (m_implementation->process_id > 0)
    {
        int status = 0;
        if (::waitpid(m_implementation->process_id, &status, WNOHANG) == 0)
        {
            static_cast<void>(::kill(m_implementation->process_id, SIGHUP));
            pid_t wait_result = 0;
            for (int attempt = 0; attempt < 20 && wait_result == 0; ++attempt)
            {
                wait_result = ::waitpid(m_implementation->process_id, &status, WNOHANG);
                if (wait_result == 0)
                {
                    ::usleep(1000);
                }
            }
            if (wait_result == 0)
            {
                static_cast<void>(::kill(m_implementation->process_id, SIGKILL));
                static_cast<void>(::waitpid(m_implementation->process_id, &status, 0));
            }
        }
        m_implementation->process_id = -1;
    }
#endif
    m_running = false;
}

bool TerminalSession::write_input(std::string_view text)
{
    if (!m_running || text.empty())
    {
        return false;
    }

#if defined(_WIN32)
    if (m_implementation->pseudo_console != nullptr)
    {
        DWORD written = 0;
        const bool succeeded = m_implementation->input_write != nullptr &&
            WriteFile(m_implementation->input_write, text.data(),
                static_cast<DWORD>(text.size()), &written, nullptr) != FALSE &&
            written == text.size();
        return succeeded;
    }
#endif
    const bool is_enter = text == "\r" || text == "\n";
    const bool is_backspace = text == "\x7F" || text == "\b";
    const bool printable = is_printable_input(text);
    const bool clear_requested = is_enter && is_clear_command(m_pending_input);

#if defined(_WIN32)
    if (text == "\x1B[A")
    {
        return navigate_history(true);
    }
    if (text == "\x1B[B")
    {
        return navigate_history(false);
    }
#endif

    const auto track_input = [this, text, is_enter, is_backspace, printable]() {
        if (printable)
        {
            if (m_pending_input.empty() && m_input_start_column == 0)
            {
                m_input_start_column = m_cursor_column;
            }
            m_pending_input.append(text);
        }
        else if (is_backspace)
        {
            if (m_pending_input.empty() && m_input_start_column == 0 &&
                m_cursor_column > 0)
            {
                // The first backspace tells us where the shell prompt ends.
                m_input_start_column = m_cursor_column;
            }
            remove_last_utf8_code_point(m_pending_input);
        }
        else if (is_enter)
        {
            m_pending_input.clear();
            m_input_start_column = 0;
        }
        else if (!text.empty() && text.front() == '\x1B')
        {
            // Cursor movement makes the simple command mirror unreliable. The
            // prompt boundary remains valid, so backspace is still protected.
            m_pending_input.clear();
        }
    };
#if defined(_WIN32)
    std::string windows_input;
    bool send_now = false;

    if (is_enter)
    {
        windows_input = m_pending_input;
        windows_input.push_back('\r');
        windows_input.push_back('\n');
        send_now = true;
    }
    else if (printable && (text.find('\n') != std::string_view::npos || text.find('\r') != std::string_view::npos))
    {
        windows_input = m_pending_input;
        windows_input.append(text);
        send_now = true;
    }
    else if (!printable && !is_backspace && !is_enter && text != "\t")
    {
        windows_input = std::string(text);
        send_now = true;
    }

    if (send_now)
    {
        std::string formatted;
        formatted.reserve(windows_input.size() + 1);
        for (std::size_t index = 0; index < windows_input.size(); ++index)
        {
            formatted.push_back(windows_input[index]);
            if (windows_input[index] == '\r' && (index + 1 == windows_input.size() || windows_input[index + 1] != '\n'))
            {
                formatted.push_back('\n');
            }
        }

        DWORD written = 0;
        const bool succeeded = m_implementation->input_write != nullptr &&
            WriteFile(m_implementation->input_write, formatted.data(),
                static_cast<DWORD>(formatted.size()), &written, nullptr) != FALSE &&
            written == formatted.size();

        if (succeeded)
        {
            if (is_enter)
            {
                if (!m_pending_input.empty())
                {
                    if (m_command_history.empty() || m_command_history.back() != m_pending_input)
                    {
                        m_command_history.push_back(m_pending_input);
                    }
                }
                m_history_index.reset();
                m_saved_pending_input.clear();
                append_line();
                track_input();
                if (clear_requested)
                {
                    clear_screen();
                }
            }
            else if (printable)
            {
                track_input();
                consume_output(text);
                m_pending_input.clear();
                m_input_start_column = m_cursor_column;
            }
            else
            {
                track_input();
            }
        }
        return succeeded;
    }
    else
    {
        if (is_backspace)
        {
            track_input();
            if (!m_lines.empty() && m_cursor_column > 0)
            {
                if (m_cursor_column > m_input_start_column)
                {
                    --m_cursor_column;
                    if (m_cursor_column < m_lines.back().size())
                    {
                        m_lines.back().erase(m_cursor_column, 1);
                    }
                }
            }
        }
        else if (printable || text == "\t")
        {
            if (text == "\t")
            {
                if (m_pending_input.empty() && m_input_start_column == 0)
                {
                    m_input_start_column = m_cursor_column;
                }
                m_pending_input.append("\t");
            }
            else
            {
                track_input();
            }
            consume_output(text);
        }
        else
        {
            track_input();
        }
        return true;
    }
#else
    std::size_t total_written = 0;
    while (total_written < text.size())
    {
        const ssize_t written = ::write(m_implementation->master_fd,
            text.data() + total_written, text.size() - total_written);
        if (written > 0)
        {
            total_written += static_cast<std::size_t>(written);
            continue;
        }
        if (written < 0 && errno == EINTR)
        {
            continue;
        }
        break;
    }
    const bool succeeded = total_written == text.size();
    if (succeeded)
    {
        track_input();
        if (clear_requested)
        {
            clear_screen();
        }
    }
    return succeeded;
#endif
}

bool TerminalSession::poll()
{
    if (!m_running)
    {
        return false;
    }
    bool changed = false;
    std::array<char, 8192> buffer{};
#if defined(_WIN32)
    for (;;)
    {
        DWORD available = 0;
        if (m_implementation->output_read == nullptr ||
            PeekNamedPipe(m_implementation->output_read, nullptr, 0, nullptr, &available, nullptr) == FALSE ||
            available == 0)
        {
            break;
        }
        DWORD bytes_read = 0;
        const DWORD requested = std::min<DWORD>(available, static_cast<DWORD>(buffer.size()));
        if (ReadFile(m_implementation->output_read, buffer.data(), requested, &bytes_read, nullptr) == FALSE ||
            bytes_read == 0)
        {
            break;
        }
        consume_output(std::string_view{buffer.data(), bytes_read});
        changed = true;
    }
    if (m_implementation->process != nullptr &&
        WaitForSingleObject(m_implementation->process, 0) == WAIT_OBJECT_0)
    {
        DWORD exit_code = 0;
        GetExitCodeProcess(m_implementation->process, &exit_code);
        m_running = false;
        append_status("[Process exited with code " + std::to_string(exit_code) + "]");
        changed = true;
    }
#else
    for (;;)
    {
        const ssize_t bytes_read = ::read(m_implementation->master_fd, buffer.data(), buffer.size());
        if (bytes_read > 0)
        {
            consume_output(std::string_view{buffer.data(), static_cast<std::size_t>(bytes_read)});
            changed = true;
            continue;
        }
        if (bytes_read < 0 && errno == EINTR)
        {
            continue;
        }
        break;
    }
    int status = 0;
    const pid_t wait_result = ::waitpid(m_implementation->process_id, &status, WNOHANG);
    if (wait_result == m_implementation->process_id)
    {
        const int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        m_running = false;
        m_implementation->process_id = -1;
        append_status("[Process exited with code " + std::to_string(exit_code) + "]");
        changed = true;
    }
#endif
    return changed;
}

void TerminalSession::resize(std::size_t columns, std::size_t rows) noexcept
{
    columns = std::clamp<std::size_t>(columns, 1, 65535);
    rows = std::clamp<std::size_t>(rows, 1, 65535);
    if (columns == m_columns && rows == m_rows)
    {
        return;
    }
    m_columns = columns;
    m_rows = rows;
    if (m_in_alternate_screen)
    {
        m_lines.resize(m_rows);
        if (m_cursor_line >= m_rows)
        {
            m_cursor_line = m_rows > 0 ? m_rows - 1 : 0;
        }
    }
#if !defined(_WIN32)
    if (m_implementation->master_fd >= 0)
    {
        winsize size{};
        size.ws_col = static_cast<unsigned short>(columns);
        size.ws_row = static_cast<unsigned short>(rows);
        static_cast<void>(::ioctl(m_implementation->master_fd, TIOCSWINSZ, &size));
    }
#else
    if (m_implementation->pseudo_console != nullptr && s_fn_ResizePseudoConsole != nullptr)
    {
        COORD console_size{
            static_cast<SHORT>(std::clamp<std::size_t>(columns, 1, 32767)),
            static_cast<SHORT>(std::clamp<std::size_t>(rows, 1, 32767))
        };
        s_fn_ResizePseudoConsole(m_implementation->pseudo_console, console_size);
    }
    else if (m_implementation->process != nullptr)
    {
        const DWORD process_id = GetProcessId(m_implementation->process);
        if (process_id > 0)
        {
            FreeConsole();
            if (AttachConsole(process_id))
            {
                HANDLE console_output = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
                if (console_output != INVALID_HANDLE_VALUE)
                {
                    COORD size;
                    size.X = static_cast<SHORT>(columns);
                    size.Y = static_cast<SHORT>(rows);
                    
                    SMALL_RECT rect;
                    rect.Left = 0;
                    rect.Top = 0;
                    rect.Right = size.X - 1;
                    rect.Bottom = size.Y - 1;
                    
                    SMALL_RECT min_rect = {0, 0, 1, 1};
                    SetConsoleWindowInfo(console_output, TRUE, &min_rect);
                    SetConsoleScreenBufferSize(console_output, size);
                    SetConsoleWindowInfo(console_output, TRUE, &rect);
                    
                    CloseHandle(console_output);
                }
                FreeConsole();
            }
        }
    }
#endif
}

static std::vector<std::string> split_utf8_codepoints(std::string_view s)
{
    std::vector<std::string> cps;
    for (std::size_t i = 0; i < s.size(); )
    {
        unsigned char c = static_cast<unsigned char>(s[i]);
        std::size_t len = 1;
        if ((c & 0x80) == 0) len = 1;
        else if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;

        len = std::min(len, s.size() - i);
        cps.emplace_back(s.substr(i, len));
        i += len;
    }
    return cps;
}

static void set_utf8_cell(std::string& line, std::size_t target_col, std::string_view utf8_char)
{
    std::size_t current_col = 0;
    std::size_t byte_pos = 0;
    std::size_t replace_byte_start = std::string::npos;
    std::size_t replace_byte_len = 0;

    while (byte_pos < line.size())
    {
        if (current_col == target_col)
        {
            replace_byte_start = byte_pos;
            const unsigned char c = static_cast<unsigned char>(line[byte_pos]);
            if ((c & 0x80) == 0) replace_byte_len = 1;
            else if ((c & 0xE0) == 0xC0) replace_byte_len = 2;
            else if ((c & 0xF0) == 0xE0) replace_byte_len = 3;
            else if ((c & 0xF8) == 0xF0) replace_byte_len = 4;
            else replace_byte_len = 1;
            replace_byte_len = std::min(replace_byte_len, line.size() - byte_pos);
            break;
        }

        const unsigned char c = static_cast<unsigned char>(line[byte_pos]);
        std::size_t char_len = 1;
        if ((c & 0x80) == 0) char_len = 1;
        else if ((c & 0xE0) == 0xC0) char_len = 2;
        else if ((c & 0xF0) == 0xE0) char_len = 3;
        else if ((c & 0xF8) == 0xF0) char_len = 4;
        char_len = std::min(char_len, line.size() - byte_pos);
        byte_pos += char_len;
        ++current_col;
    }

    if (replace_byte_start != std::string::npos)
    {
        line.replace(replace_byte_start, replace_byte_len, utf8_char);
    }
    else
    {
        if (target_col > current_col)
        {
            line.append(target_col - current_col, ' ');
        }
        line.append(utf8_char);
    }
}

static void erase_utf8_from(std::string& line, std::size_t target_col)
{
    std::size_t current_col = 0;
    std::size_t byte_pos = 0;
    while (byte_pos < line.size())
    {
        if (current_col == target_col)
        {
            line.erase(byte_pos);
            return;
        }
        const unsigned char c = static_cast<unsigned char>(line[byte_pos]);
        std::size_t char_len = 1;
        if ((c & 0x80) == 0) char_len = 1;
        else if ((c & 0xE0) == 0xC0) char_len = 2;
        else if ((c & 0xF0) == 0xE0) char_len = 3;
        else if ((c & 0xF8) == 0xF0) char_len = 4;
        char_len = std::min(char_len, line.size() - byte_pos);
        byte_pos += char_len;
        ++current_col;
    }
}

bool TerminalSession::is_running() const noexcept { return m_running; }

const std::filesystem::path& TerminalSession::get_shell_path() const noexcept { return m_shell_path; }

std::span<const std::string> TerminalSession::get_lines() const noexcept { return m_lines; }

void TerminalSession::consume_output(std::string_view output)
{
    for (std::size_t i = 0; i < output.size(); ++i)
    {
        const char character = output[i];

        if (m_parser_state == ParserState::Text)
        {
            if (m_utf8_expected > 0)
            {
                if ((static_cast<unsigned char>(character) & 0xC0) == 0x80)
                {
                    m_utf8_sequence.push_back(character);
                    --m_utf8_expected;
                    if (m_utf8_expected == 0)
                    {
                        append_codepoint(m_utf8_sequence);
                        m_utf8_sequence.clear();
                    }
                }
                else
                {
                    m_utf8_sequence.clear();
                    m_utf8_expected = 0;
                }
                continue;
            }

            if (static_cast<unsigned char>(character) >= 0x80U)
            {
                const unsigned char uc = static_cast<unsigned char>(character);
                if ((uc & 0xE0) == 0xC0)
                {
                    m_utf8_sequence = character;
                    m_utf8_expected = 1;
                }
                else if ((uc & 0xF0) == 0xE0)
                {
                    m_utf8_sequence = character;
                    m_utf8_expected = 2;
                }
                else if ((uc & 0xF8) == 0xF0)
                {
                    m_utf8_sequence = character;
                    m_utf8_expected = 3;
                }
                continue;
            }
        }

        switch (m_parser_state)
        {
        case ParserState::Text:
            if (character == '\x1B')
            {
                m_utf8_sequence.clear();
                m_utf8_expected = 0;
                m_parser_state = ParserState::Escape;
            }
            else if (character == '\n')
            {
                if (m_in_alternate_screen)
                {
                    if (m_cursor_line + 1 < m_rows)
                    {
                        ++m_cursor_line;
                        while (m_cursor_line >= m_lines.size())
                        {
                            m_lines.emplace_back();
                        }
                    }
                }
                else
                {
                    if (m_cursor_line + 1 < m_lines.size())
                    {
                        ++m_cursor_line;
                    }
                    else
                    {
                        m_lines.emplace_back();
                        m_cursor_line = m_lines.size() - 1;
                    }
                    trim_scrollback();
                }
            }
            else if (character == '\r')
            {
                m_cursor_column = 0;
            }
            else if (character == '\b')
            {
                if (m_cursor_column > 0)
                {
                    --m_cursor_column;
                }
            }
            else if (character == '\t')
            {
                const std::size_t count = 4 - (m_cursor_column % 4);
                for (std::size_t index = 0; index < count; ++index)
                {
                    append_codepoint(" ");
                }
            }
            else if (static_cast<unsigned char>(character) >= 0x20U && character != '\x7F')
            {
                append_codepoint(std::string_view(&character, 1));
            }
            break;

        case ParserState::Escape:
            if (character == '[')
            {
                m_control_sequence.clear();
                m_parser_state = ParserState::ControlSequence;
            }
            else if (character == ']')
            {
                m_parser_state = ParserState::OperatingSystemCommand;
            }
            else if (character == '7')
            {
                m_saved_cursor_line = m_cursor_line;
                m_saved_cursor_column = m_cursor_column;
                m_parser_state = ParserState::Text;
            }
            else if (character == '8')
            {
                m_cursor_line = std::min(m_saved_cursor_line, m_lines.empty() ? 0 : m_lines.size() - 1);
                m_cursor_column = m_saved_cursor_column;
                m_parser_state = ParserState::Text;
            }
            else
            {
                m_parser_state = ParserState::Text;
            }
            break;

        case ParserState::ControlSequence:
            if (character >= '@' && character <= '~')
            {
                apply_control_sequence(character);
                m_parser_state = ParserState::Text;
            }
            else if (m_control_sequence.size() < 32)
            {
                m_control_sequence.push_back(character);
            }
            break;

        case ParserState::OperatingSystemCommand:
            if (character == '\a')
            {
                m_parser_state = ParserState::Text;
            }
            else if (character == '\x1B')
            {
                m_parser_state = ParserState::OperatingSystemCommandEscape;
            }
            break;

        case ParserState::OperatingSystemCommandEscape:
            m_parser_state = character == '\\'
                ? ParserState::Text
                : ParserState::OperatingSystemCommand;
            break;
        }
    }
    trim_scrollback();
}

void TerminalSession::apply_control_sequence(char command)
{
    std::size_t param1 = 1;
    std::size_t param2 = 1;
    bool has_param1 = false;
    bool has_param2 = false;

    if (!m_control_sequence.empty() && m_control_sequence.front() != '?')
    {
        const std::size_t sep = m_control_sequence.find(';');
        const std::string_view p1 = std::string_view{m_control_sequence}.substr(0, sep);
        if (!p1.empty())
        {
            std::size_t v = 0;
            bool valid = true;
            for (const char c : p1)
            {
                if (c >= '0' && c <= '9')
                {
                    v = v * 10 + static_cast<std::size_t>(c - '0');
                }
                else
                {
                    valid = false;
                    break;
                }
            }
            if (valid)
            {
                param1 = v;
                has_param1 = true;
            }
        }
        if (sep != std::string_view::npos)
        {
            const std::string_view p2 = std::string_view{m_control_sequence}.substr(sep + 1);
            if (!p2.empty())
            {
                std::size_t v = 0;
                bool valid = true;
                for (const char c : p2)
                {
                    if (c >= '0' && c <= '9')
                    {
                        v = v * 10 + static_cast<std::size_t>(c - '0');
                    }
                    else
                    {
                        valid = false;
                        break;
                    }
                }
                if (valid)
                {
                    param2 = v;
                    has_param2 = true;
                }
            }
        }
    }

    if (m_lines.empty())
    {
        m_lines.emplace_back();
        m_cursor_line = 0;
    }
    if (m_cursor_line >= m_lines.size())
    {
        m_cursor_line = m_lines.size() - 1;
    }

    switch (command)
    {
    case 'A':
    {
        const std::size_t count = (has_param1 && param1 > 0) ? param1 : 1;
        m_cursor_line = (count <= m_cursor_line) ? (m_cursor_line - count) : 0;
        break;
    }
    case 'B':
    {
        const std::size_t count = (has_param1 && param1 > 0) ? param1 : 1;
        m_cursor_line += count;
        while (m_cursor_line >= m_lines.size())
        {
            m_lines.emplace_back();
        }
        break;
    }
    case 'C':
    {
        const std::size_t count = (has_param1 && param1 > 0) ? param1 : 1;
        m_cursor_column += count;
        break;
    }
    case 'D':
    {
        const std::size_t count = (has_param1 && param1 > 0) ? param1 : 1;
        m_cursor_column = (count <= m_cursor_column) ? (m_cursor_column - count) : 0;
        break;
    }
    case 'G':
    {
        m_cursor_column = (has_param1 && param1 > 0) ? (param1 - 1) : 0;
        break;
    }
    case 'd':
    {
        const std::size_t row = (has_param1 && param1 > 0) ? (param1 - 1) : 0;
        while (row >= m_lines.size())
        {
            m_lines.emplace_back();
        }
        m_cursor_line = row;
        break;
    }
    case 'H':
    case 'f':
    {
        const std::size_t row = (has_param1 && param1 > 0) ? (param1 - 1) : 0;
        const std::size_t col = (has_param2 && param2 > 0) ? (param2 - 1) : 0;
        std::size_t target_line = row;
        if (!m_in_alternate_screen && m_rows > 0 && m_lines.size() >= m_rows)
        {
            target_line = (m_lines.size() - m_rows) + row;
        }
        while (target_line >= m_lines.size())
        {
            m_lines.emplace_back();
        }
        m_cursor_line = target_line;
        m_cursor_column = col;
        break;
    }
    case 'h':
    {
        if (!m_control_sequence.empty() && m_control_sequence.front() == '?')
        {
            std::string_view seq = std::string_view{m_control_sequence}.substr(1);
            while (!seq.empty())
            {
                const std::size_t sep = seq.find(';');
                const std::string_view token = (sep != std::string_view::npos) ? seq.substr(0, sep) : seq;
                if (token == "1049" || token == "47" || token == "1047")
                {
                    if (!m_in_alternate_screen)
                    {
                        m_in_alternate_screen = true;
                        m_main_screen_lines = m_lines;
                        m_main_cursor_line = m_cursor_line;
                        m_main_cursor_column = m_cursor_column;
                        const std::size_t count = std::max<std::size_t>(m_rows, 1);
                        m_lines.assign(count, std::string{});
                        m_cursor_line = 0;
                        m_cursor_column = 0;
                    }
                }
                else if (token == "1000")
                {
                    m_mouse_tracking = MouseTracking::X10;
                }
                else if (token == "1002")
                {
                    m_mouse_tracking = MouseTracking::ButtonEvent;
                }
                else if (token == "1003")
                {
                    m_mouse_tracking = MouseTracking::AnyEvent;
                }
                else if (token == "1006")
                {
                    m_sgr_mouse = true;
                }
                else if (token == "1007")
                {
                    m_alternate_scroll = true;
                }
                else if (token == "1")
                {
                    m_application_cursor_keys = true;
                }

                if (sep == std::string_view::npos)
                {
                    break;
                }
                seq = seq.substr(sep + 1);
            }
        }
        break;
    }
    case 'l':
    {
        if (!m_control_sequence.empty() && m_control_sequence.front() == '?')
        {
            std::string_view seq = std::string_view{m_control_sequence}.substr(1);
            while (!seq.empty())
            {
                const std::size_t sep = seq.find(';');
                const std::string_view token = (sep != std::string_view::npos) ? seq.substr(0, sep) : seq;
                if (token == "1049" || token == "47" || token == "1047")
                {
                    if (m_in_alternate_screen)
                    {
                        m_in_alternate_screen = false;
                        m_lines = std::move(m_main_screen_lines);
                        m_cursor_line = std::min(m_main_cursor_line, m_lines.empty() ? 0 : m_lines.size() - 1);
                        m_cursor_column = m_main_cursor_column;
                    }
                }
                else if (token == "1000" || token == "1002" || token == "1003")
                {
                    m_mouse_tracking = MouseTracking::Off;
                }
                else if (token == "1006")
                {
                    m_sgr_mouse = false;
                }
                else if (token == "1007")
                {
                    m_alternate_scroll = false;
                }
                else if (token == "1")
                {
                    m_application_cursor_keys = false;
                }

                if (sep == std::string_view::npos)
                {
                    break;
                }
                seq = seq.substr(sep + 1);
            }
        }
        break;
    }
    case 's':
    {
        m_saved_cursor_line = m_cursor_line;
        m_saved_cursor_column = m_cursor_column;
        break;
    }
    case 'u':
    {
        m_cursor_line = std::min(m_saved_cursor_line, m_lines.empty() ? 0 : m_lines.size() - 1);
        m_cursor_column = m_saved_cursor_column;
        break;
    }
    case 'K':
    {
        std::string& line = m_lines[m_cursor_line];
        if (m_control_sequence == "2")
        {
            line.clear();
            m_cursor_column = 0;
        }
        else if (m_control_sequence == "1")
        {
            std::vector<std::string> cps = split_utf8_codepoints(line);
            const std::size_t end = std::min(m_cursor_column + 1, cps.size());
            for (std::size_t i = 0; i < end; ++i)
            {
                cps[i] = " ";
            }
            line.clear();
            for (const auto& cp : cps) line.append(cp);
        }
        else
        {
            erase_utf8_from(line, m_cursor_column);
        }
        break;
    }
    case 'X':
    {
        const std::size_t count = (has_param1 && param1 > 0) ? param1 : 1;
        std::string& line = m_lines[m_cursor_line];
        std::vector<std::string> cps = split_utf8_codepoints(line);
        if (m_cursor_column < cps.size())
        {
            const std::size_t erase_len = std::min(count, cps.size() - m_cursor_column);
            for (std::size_t i = m_cursor_column; i < m_cursor_column + erase_len; ++i)
            {
                cps[i] = " ";
            }
            line.clear();
            for (const auto& cp : cps) line.append(cp);
        }
        break;
    }
    case 'P':
    {
        const std::size_t count = (has_param1 && param1 > 0) ? param1 : 1;
        std::string& line = m_lines[m_cursor_line];
        std::vector<std::string> cps = split_utf8_codepoints(line);
        if (m_cursor_column < cps.size())
        {
            const std::size_t del_len = std::min(count, cps.size() - m_cursor_column);
            cps.erase(cps.begin() + static_cast<std::ptrdiff_t>(m_cursor_column),
                      cps.begin() + static_cast<std::ptrdiff_t>(m_cursor_column + del_len));
            line.clear();
            for (const auto& cp : cps) line.append(cp);
        }
        break;
    }
    case '@':
    {
        const std::size_t count = (has_param1 && param1 > 0) ? param1 : 1;
        std::string& line = m_lines[m_cursor_line];
        std::vector<std::string> cps = split_utf8_codepoints(line);
        if (m_cursor_column <= cps.size())
        {
            cps.insert(cps.begin() + static_cast<std::ptrdiff_t>(m_cursor_column), count, " ");
            line.clear();
            for (const auto& cp : cps) line.append(cp);
        }
        break;
    }
    case 'M':
    {
        const std::size_t count = (has_param1 && param1 > 0) ? param1 : 1;
        if (m_cursor_line < m_lines.size())
        {
            const std::size_t del_count = std::min(count, m_lines.size() - m_cursor_line);
            m_lines.erase(m_lines.begin() + static_cast<std::ptrdiff_t>(m_cursor_line),
                          m_lines.begin() + static_cast<std::ptrdiff_t>(m_cursor_line + del_count));
            if (m_in_alternate_screen)
            {
                m_lines.resize(std::max<std::size_t>(m_rows, 1));
            }
        }
        break;
    }
    case 'L':
    {
        const std::size_t count = (has_param1 && param1 > 0) ? param1 : 1;
        if (m_cursor_line < m_lines.size())
        {
            m_lines.insert(m_lines.begin() + static_cast<std::ptrdiff_t>(m_cursor_line), count, std::string{});
            if (m_in_alternate_screen && m_lines.size() > m_rows)
            {
                m_lines.resize(m_rows);
            }
        }
        break;
    }
    case 'J':
    {
        if (m_control_sequence == "2" || m_control_sequence == "3")
        {
            clear_screen();
        }
        else if (m_control_sequence.empty() || m_control_sequence == "0")
        {
            if (m_cursor_line < m_lines.size())
            {
                erase_utf8_from(m_lines[m_cursor_line], m_cursor_column);
                if (!m_in_alternate_screen && m_cursor_line + 1 < m_lines.size())
                {
                    m_lines.erase(m_lines.begin() + static_cast<std::ptrdiff_t>(m_cursor_line + 1), m_lines.end());
                }
            }
        }
        break;
    }
    default:
        break;
    }
}

void TerminalSession::append_codepoint(std::string_view utf8_char)
{
    if (m_lines.empty())
    {
        m_lines.emplace_back();
        m_cursor_line = 0;
    }
    while (m_cursor_line >= m_lines.size())
    {
        m_lines.emplace_back();
    }
    if (m_cursor_column >= m_columns)
    {
        if (m_in_alternate_screen)
        {
            if (m_cursor_line + 1 < m_rows)
            {
                ++m_cursor_line;
                while (m_cursor_line >= m_lines.size())
                {
                    m_lines.emplace_back();
                }
                m_cursor_column = 0;
            }
            else
            {
                m_cursor_column = m_columns - 1;
            }
        }
        else
        {
            if (m_cursor_line + 1 < m_lines.size())
            {
                ++m_cursor_line;
            }
            else
            {
                m_lines.emplace_back();
                m_cursor_line = m_lines.size() - 1;
            }
            m_cursor_column = 0;
            trim_scrollback();
        }
    }
    set_utf8_cell(m_lines[m_cursor_line], m_cursor_column, utf8_char);
    ++m_cursor_column;
}

void TerminalSession::append_character(char character)
{
    append_codepoint(std::string_view(&character, 1));
}

void TerminalSession::append_line()
{
    if (m_in_alternate_screen)
    {
        if (m_cursor_line + 1 < m_rows)
        {
            ++m_cursor_line;
            while (m_cursor_line >= m_lines.size())
            {
                m_lines.emplace_back();
            }
        }
        m_cursor_column = 0;
    }
    else
    {
        m_lines.emplace_back();
        m_cursor_line = m_lines.size() - 1;
        m_cursor_column = 0;
        m_input_start_column = 0;
        m_pending_input.clear();
        trim_scrollback();
    }
}

void TerminalSession::append_status(std::string message)
{
    if (!m_lines.empty() && m_lines.back().empty())
    {
        m_lines.back() = std::move(message);
    }
    else
    {
        m_lines.push_back(std::move(message));
    }
    append_line();
}

void TerminalSession::clear_screen() noexcept
{
    if (m_in_alternate_screen)
    {
        const std::size_t count = std::max<std::size_t>(m_rows, 1);
        m_lines.assign(count, std::string{});
    }
    else
    {
        m_lines.assign(1, std::string{});
    }
    m_cursor_line = 0;
    m_cursor_column = 0;
    m_saved_cursor_line = 0;
    m_saved_cursor_column = 0;
    m_input_start_column = 0;
    m_pending_input.clear();
    m_parser_state = ParserState::Text;
    m_control_sequence.clear();
}

void TerminalSession::trim_scrollback()
{
    if (m_in_alternate_screen)
    {
        return;
    }
    if (m_lines.size() <= maximum_scrollback_lines)
    {
        return;
    }
    const std::size_t remove_count = m_lines.size() - maximum_scrollback_lines;
    m_lines.erase(m_lines.begin(), m_lines.begin() + static_cast<std::ptrdiff_t>(remove_count));
    if (m_cursor_line >= remove_count)
    {
        m_cursor_line -= remove_count;
    }
    else
    {
        m_cursor_line = 0;
    }
    if (m_saved_cursor_line >= remove_count)
    {
        m_saved_cursor_line -= remove_count;
    }
    else
    {
        m_saved_cursor_line = 0;
    }
}

bool TerminalSession::navigate_history(bool up)
{
    if (m_command_history.empty() || m_lines.empty())
    {
        return false;
    }

    m_cursor_line = m_lines.size() - 1;

    if (m_input_start_column == 0 && m_lines.back().size() >= m_pending_input.size())
    {
        m_input_start_column = m_lines.back().size() - m_pending_input.size();
    }

    if (up)
    {
        if (!m_history_index.has_value())
        {
            m_saved_pending_input = m_pending_input;
            m_history_index = m_command_history.size() - 1;
        }
        else if (*m_history_index > 0)
        {
            --(*m_history_index);
        }
        else
        {
            return false;
        }
    }
    else
    {
        if (!m_history_index.has_value())
        {
            return false;
        }
        if (*m_history_index + 1 < m_command_history.size())
        {
            ++(*m_history_index);
        }
        else
        {
            m_history_index.reset();
        }
    }

    const std::string_view target_text = m_history_index.has_value()
        ? std::string_view(m_command_history[*m_history_index])
        : std::string_view(m_saved_pending_input);

    if (m_input_start_column > m_lines.back().size())
    {
        m_input_start_column = m_lines.back().size();
    }

    m_lines.back().resize(m_input_start_column);
    m_lines.back().append(target_text);
    m_cursor_column = m_lines.back().size();
    m_pending_input = std::string(target_text);

    return true;
}

bool TerminalSession::send_mouse_scroll(std::ptrdiff_t line_delta, std::size_t column, std::size_t row)
{
    if (!m_running || line_delta == 0)
    {
        return false;
    }

    if (m_mouse_tracking != MouseTracking::Off)
    {
        const int button = (line_delta < 0) ? 64 : 65; // 64 = WheelUp, 65 = WheelDown
        const std::size_t steps = std::clamp<std::size_t>(std::abs(line_delta) / 3, 1, 5);
        if (m_sgr_mouse)
        {
            // SGR format: \x1b[<button;col;rowM
            std::string seq;
            for (std::size_t i = 0; i < steps; ++i)
            {
                seq += "\x1B[<" + std::to_string(button) + ";" +
                       std::to_string(std::max<std::size_t>(1, column)) + ";" +
                       std::to_string(std::max<std::size_t>(1, row)) + "M";
            }
            return write_input(seq);
        }
        else
        {
            // X10 / normal mouse format: \x1b[M Cb Cx Cy (where each is 32 + val)
            std::string seq;
            const char cb = static_cast<char>(32 + button);
            const char cx = static_cast<char>(32 + std::clamp<std::size_t>(column, 1, 223));
            const char cy = static_cast<char>(32 + std::clamp<std::size_t>(row, 1, 223));
            for (std::size_t i = 0; i < steps; ++i)
            {
                seq += "\x1B[M";
                seq.push_back(cb);
                seq.push_back(cx);
                seq.push_back(cy);
            }
            return write_input(seq);
        }
    }

    if (m_in_alternate_screen && m_alternate_scroll)
    {
        // Alternate scroll mode: Send Arrow Up / Down keys to the running CLI program (less, vim, nano, man, etc.)
        const std::string_view arrow = (line_delta < 0)
            ? (m_application_cursor_keys ? "\x1BOA" : "\x1B[A")
            : (m_application_cursor_keys ? "\x1BOB" : "\x1B[B");
        const std::size_t steps = std::clamp<std::size_t>(std::abs(line_delta), 1, 5);
        std::string seq;
        for (std::size_t i = 0; i < steps; ++i)
        {
            seq.append(arrow);
        }
        return write_input(seq);
    }

    return false;
}

bool TerminalSession::send_mouse_button(
    MouseButton button, MouseAction action,
    std::size_t column, std::size_t row,
    bool shift, bool meta, bool ctrl)
{
    if (!m_running || m_mouse_tracking == MouseTracking::Off)
    {
        return false;
    }

    if (m_mouse_tracking == MouseTracking::X10 && action != MouseAction::Press)
    {
        return false;
    }

    int btn_code = static_cast<int>(button);
    if (action == MouseAction::Motion)
    {
        btn_code += 32;
    }
    if (shift) btn_code += 4;
    if (meta)  btn_code += 8;
    if (ctrl)  btn_code += 16;

    const std::size_t col = std::max<std::size_t>(1, column);
    const std::size_t r = std::max<std::size_t>(1, row);

    if (m_sgr_mouse)
    {
        const char terminator = (action == MouseAction::Release) ? 'm' : 'M';
        const std::string seq = "\x1B[<" + std::to_string(btn_code) + ";" +
                                std::to_string(col) + ";" +
                                std::to_string(r) + terminator;
        return write_input(seq);
    }
    else
    {
        if (action == MouseAction::Release)
        {
            btn_code = 3;
            if (shift) btn_code += 4;
            if (meta)  btn_code += 8;
            if (ctrl)  btn_code += 16;
        }
        const char cb = static_cast<char>(32 + btn_code);
        const char cx = static_cast<char>(32 + std::clamp<std::size_t>(col, 1, 223));
        const char cy = static_cast<char>(32 + std::clamp<std::size_t>(r, 1, 223));
        std::string seq = "\x1B[M";
        seq.push_back(cb);
        seq.push_back(cx);
        seq.push_back(cy);
        return write_input(seq);
    }
}

bool TerminalSession::send_mouse_motion(
    std::size_t column, std::size_t row,
    bool button_pressed, MouseButton pressed_button,
    bool shift, bool meta, bool ctrl)
{
    if (!m_running || m_mouse_tracking == MouseTracking::Off)
    {
        return false;
    }

    if (m_mouse_tracking == MouseTracking::X10)
    {
        return false;
    }

    if (m_mouse_tracking == MouseTracking::ButtonEvent && !button_pressed)
    {
        return false;
    }

    int btn_code = button_pressed ? static_cast<int>(pressed_button) : 3;
    btn_code += 32;
    if (shift) btn_code += 4;
    if (meta)  btn_code += 8;
    if (ctrl)  btn_code += 16;

    const std::size_t col = std::max<std::size_t>(1, column);
    const std::size_t r = std::max<std::size_t>(1, row);

    if (m_sgr_mouse)
    {
        const std::string seq = "\x1B[<" + std::to_string(btn_code) + ";" +
                                std::to_string(col) + ";" +
                                std::to_string(r) + "M";
        return write_input(seq);
    }
    else
    {
        const char cb = static_cast<char>(32 + btn_code);
        const char cx = static_cast<char>(32 + std::clamp<std::size_t>(col, 1, 223));
        const char cy = static_cast<char>(32 + std::clamp<std::size_t>(r, 1, 223));
        std::string seq = "\x1B[M";
        seq.push_back(cb);
        seq.push_back(cx);
        seq.push_back(cy);
        return write_input(seq);
    }
}

} // namespace Zenvra::Terminal
