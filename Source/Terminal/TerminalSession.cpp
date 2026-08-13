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
#endif

} // namespace

struct TerminalSession::Implementation
{
#if defined(_WIN32)
    HANDLE process = nullptr;
    HANDLE input_write = nullptr;
    HANDLE output_read = nullptr;
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
    m_cursor_column = 0;
    m_input_start_column = 0;
    m_pending_input.clear();
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
    SECURITY_ATTRIBUTES security_attributes{};
    security_attributes.nLength = sizeof(security_attributes);
    security_attributes.bInheritHandle = TRUE;
    HANDLE input_read = nullptr;
    HANDLE output_write = nullptr;
    if (CreatePipe(&input_read, &m_implementation->input_write, &security_attributes, 0) == FALSE ||
        CreatePipe(&m_implementation->output_read, &output_write, &security_attributes, 0) == FALSE)
    {
        if (input_read != nullptr) CloseHandle(input_read);
        if (output_write != nullptr) CloseHandle(output_write);
        stop();
        append_status("[Unable to create terminal pipes]");
        return false;
    }
    SetHandleInformation(m_implementation->input_write, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(m_implementation->output_read, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdInput = input_read;
    startup.hStdOutput = output_write;
    startup.hStdError = output_write;
    PROCESS_INFORMATION process_info{};
    std::wstring command = quote_windows_argument(m_shell_path.wstring()) +
        windows_shell_arguments(m_shell_path);
    const std::wstring directory = working_directory.empty()
        ? std::wstring{}
        : working_directory.wstring();
    const BOOL created = CreateProcessW(
        m_shell_path.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
        nullptr, directory.empty() ? nullptr : directory.c_str(), &startup, &process_info);
    CloseHandle(input_read);
    CloseHandle(output_write);
    if (created == FALSE)
    {
        stop();
        append_status("[Unable to start the local shell]");
        return false;
    }
    CloseHandle(process_info.hThread);
    m_implementation->process = process_info.hProcess;
#else
    winsize terminal_size{};
    terminal_size.ws_col = 100;
    terminal_size.ws_row = 24;
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
    const bool is_enter = text == "\r" || text == "\n";
    const bool is_backspace = text == "\x7F" || text == "\b";
    const bool printable = is_printable_input(text);
    const bool clear_requested = is_enter && is_clear_command(m_pending_input);

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
#if !defined(_WIN32)
    if (m_implementation->master_fd >= 0)
    {
        winsize size{};
        size.ws_col = static_cast<unsigned short>(columns);
        size.ws_row = static_cast<unsigned short>(rows);
        static_cast<void>(::ioctl(m_implementation->master_fd, TIOCSWINSZ, &size));
    }
#else
    if (m_implementation->process != nullptr)
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

bool TerminalSession::is_running() const noexcept { return m_running; }

const std::filesystem::path& TerminalSession::get_shell_path() const noexcept { return m_shell_path; }

std::span<const std::string> TerminalSession::get_lines() const noexcept { return m_lines; }

void TerminalSession::consume_output(std::string_view output)
{
    for (const char character : output)
    {
        switch (m_parser_state)
        {
        case ParserState::Text:
            if (character == '\x1B') m_parser_state = ParserState::Escape;
            else if (character == '\n') append_line();
            else if (character == '\r') m_cursor_column = 0;
            else if (character == '\b' && m_cursor_column > m_input_start_column)
            {
                --m_cursor_column;
            }
            else if (character == '\t')
            {
                const std::size_t count = 4 - (m_cursor_column % 4);
                for (std::size_t index = 0; index < count; ++index) append_character(' ');
            }
            else if (static_cast<unsigned char>(character) >= 0x20U && character != '\x7F')
            {
                append_character(character);
            }
            break;
        case ParserState::Escape:
            if (character == '[')
            {
                m_control_sequence.clear();
                m_parser_state = ParserState::ControlSequence;
            }
            else if (character == ']') m_parser_state = ParserState::OperatingSystemCommand;
            else m_parser_state = ParserState::Text;
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
            if (character == '\a') m_parser_state = ParserState::Text;
            else if (character == '\x1B') m_parser_state = ParserState::OperatingSystemCommandEscape;
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
    std::size_t amount = 1;
    const std::size_t separator = m_control_sequence.find(';');
    const std::string_view first_parameter = std::string_view{m_control_sequence}.substr(0, separator);
    if (!first_parameter.empty() && first_parameter.front() != '?')
    {
        std::size_t parsed = 0;
        for (const char character : first_parameter)
        {
            if (character < '0' || character > '9')
            {
                parsed = 0;
                break;
            }
            parsed = parsed * 10 + static_cast<std::size_t>(character - '0');
        }
        if (parsed > 0)
        {
            amount = parsed;
        }
    }

    if (m_lines.empty())
    {
        m_lines.emplace_back();
    }
    std::string& line = m_lines.back();
    switch (command)
    {
    case 'C':
        m_cursor_column = std::min(m_cursor_column + amount, line.size());
        break;
    case 'D':
        m_cursor_column = amount > m_cursor_column
            ? m_input_start_column
            : std::max(m_cursor_column - amount, m_input_start_column);
        break;
    case 'G':
        m_cursor_column = std::max(
            m_input_start_column,
            std::min(amount - 1, line.size()));
        break;
    case 'K':
        if (first_parameter == "2")
        {
            line.clear();
            m_cursor_column = 0;
            m_input_start_column = 0;
        }
        else if (first_parameter == "1")
        {
            const std::size_t end = std::min(m_cursor_column + 1, line.size());
            if (m_input_start_column < end)
            {
                const std::size_t count = end - m_input_start_column;
                line.replace(m_input_start_column, count, count, ' ');
            }
        }
        else if (m_cursor_column < line.size())
        {
            line.erase(m_cursor_column);
        }
        break;
    case 'J':
        if (first_parameter == "2" || first_parameter == "3")
        {
            clear_screen();
        }
        else if (first_parameter.empty() || first_parameter == "0")
        {
            line.erase(std::min(m_cursor_column, line.size()));
        }
        else if (first_parameter == "1")
        {
            const std::size_t end = std::min(m_cursor_column + 1, line.size());
            if (m_input_start_column < end)
            {
                line.replace(m_input_start_column,
                    end - m_input_start_column,
                    end - m_input_start_column,
                    ' ');
            }
        }
        break;
    case 'H':
        m_cursor_column = m_input_start_column;
        break;
    default:
        break;
    }
}

void TerminalSession::append_character(char character)
{
    if (m_lines.empty()) m_lines.emplace_back();
    if (m_cursor_column < m_input_start_column)
    {
        m_cursor_column = m_input_start_column;
    }
    if (character == ' ' && m_cursor_column == m_input_start_column &&
        m_pending_input.empty())
    {
        return;
    }
    if (m_cursor_column >= m_columns)
    {
        m_lines.emplace_back();
        m_cursor_column = 0;
        m_input_start_column = 0;
        trim_scrollback();
    }
    std::string& line = m_lines.back();
    if (m_cursor_column < line.size()) line[m_cursor_column] = character;
    else
    {
        if (m_cursor_column > line.size()) line.append(m_cursor_column - line.size(), ' ');
        line.push_back(character);
    }
    ++m_cursor_column;
}

void TerminalSession::append_line()
{
    m_lines.emplace_back();
    m_cursor_column = 0;
    m_input_start_column = 0;
    m_pending_input.clear();
    trim_scrollback();
}

void TerminalSession::append_status(std::string message)
{
    if (!m_lines.empty() && m_lines.back().empty()) m_lines.back() = std::move(message);
    else m_lines.push_back(std::move(message));
    append_line();
}

void TerminalSession::clear_screen() noexcept
{
    m_lines.assign(1, std::string{});
    m_cursor_column = 0;
    m_input_start_column = 0;
    m_pending_input.clear();
    m_parser_state = ParserState::Text;
    m_control_sequence.clear();
}

void TerminalSession::trim_scrollback()
{
    if (m_lines.size() <= maximum_scrollback_lines) return;
    const std::size_t remove_count = m_lines.size() - maximum_scrollback_lines;
    m_lines.erase(m_lines.begin(), m_lines.begin() + static_cast<std::ptrdiff_t>(remove_count));
}

} // namespace Zenvra::Terminal
