#include "Terminal/PosixTTYBackend.h"

#if !defined(_WIN32)
#include <cerrno>
#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <util.h>
#else
#include <pty.h>
#endif

namespace Zenvra::Terminal {

PosixTTYBackend::PosixTTYBackend() = default;

PosixTTYBackend::~PosixTTYBackend() {
    stop();
}

bool PosixTTYBackend::start(
    const std::filesystem::path& executable,
    [[maybe_unused]] const std::wstring& arguments,
    const std::filesystem::path& working_directory,
    std::size_t columns,
    std::size_t rows) {
    stop();

    winsize terminal_size{};
    terminal_size.ws_col = static_cast<unsigned short>(columns);
    terminal_size.ws_row = static_cast<unsigned short>(rows);

    const pid_t pid = ::forkpty(&m_master_fd, nullptr, nullptr, &terminal_size);
    if (pid < 0) {
        m_master_fd = -1;
        terminal_debug_log("[PosixTTYBackend] forkpty failed (errno " + std::to_string(errno) + ")");
        return false;
    }

    if (pid == 0) {
        // Child process
        if (!working_directory.empty()) {
            static_cast<void>(::chdir(working_directory.c_str()));
        }
        const std::string exe_str = executable.string();
        ::setenv("SHELL", exe_str.c_str(), 1);
        ::setenv("TERM", "xterm-256color", 1);
        ::setenv("COLORTERM", "truecolor", 1);
        ::setenv("TERM_PROGRAM", "ZDE", 1);
        ::setenv("TERM_PROGRAM_VERSION", "1.0.0", 1);

        const std::string shell_name = executable.filename().string();
        if (shell_name == "fish") {
            ::execl(exe_str.c_str(), exe_str.c_str(), "-i", nullptr);
        } else if (shell_name == "zsh" || shell_name == "bash") {
            ::execl(exe_str.c_str(), exe_str.c_str(), "-i", "-l", nullptr);
        }
        ::execl(exe_str.c_str(), exe_str.c_str(), "-i", nullptr);
        ::execl(exe_str.c_str(), exe_str.c_str(), nullptr);
        ::_exit(127);
    }

    m_process_id = pid;
    const int current_flags = ::fcntl(m_master_fd, F_GETFL, 0);
    if (current_flags >= 0) {
        static_cast<void>(::fcntl(m_master_fd, F_SETFL, current_flags | O_NONBLOCK));
    }

    m_start_time = std::chrono::steady_clock::now();
    m_running = true;
    terminal_debug_log("[PosixTTYBackend] forkpty OK (PID " + std::to_string(pid) + ")");
    return true;
}

void PosixTTYBackend::stop() noexcept {
    if (m_master_fd >= 0) {
        ::close(m_master_fd);
        m_master_fd = -1;
    }
    if (m_process_id > 0) {
        ::kill(m_process_id, SIGHUP);
        int status = 0;
        const pid_t wait_result = ::waitpid(m_process_id, &status, WNOHANG);
        if (wait_result == 0) {
            ::kill(m_process_id, SIGTERM);
            for (int i = 0; i < 5; ++i) {
                if (::waitpid(m_process_id, &status, WNOHANG) != 0) {
                    break;
                }
                ::usleep(10000);
            }
            if (::waitpid(m_process_id, &status, WNOHANG) == 0) {
                ::kill(m_process_id, SIGKILL);
                ::waitpid(m_process_id, &status, 0);
            }
        }
        m_process_id = -1;
    }
    m_running = false;
}

bool PosixTTYBackend::write_input(std::string_view text) {
    if (!m_running || m_master_fd < 0 || text.empty()) {
        return false;
    }
    std::size_t total_written = 0;
    while (total_written < text.size()) {
        const ssize_t written = ::write(
            m_master_fd,
            text.data() + total_written,
            text.size() - total_written);
        if (written > 0) {
            total_written += static_cast<std::size_t>(written);
            continue;
        }
        if (written < 0 && errno == EINTR) {
            continue;
        }
        break;
    }
    return total_written == text.size();
}

std::size_t PosixTTYBackend::read_output(std::span<char> buffer) {
    if (!m_running || m_master_fd < 0 || buffer.empty()) {
        return 0;
    }
    const ssize_t bytes_read = ::read(m_master_fd, buffer.data(), buffer.size());
    if (bytes_read > 0) {
        return static_cast<std::size_t>(bytes_read);
    }
    return 0;
}

void PosixTTYBackend::resize(std::size_t columns, std::size_t rows) noexcept {
    if (m_master_fd >= 0) {
        winsize ws{};
        ws.ws_col = static_cast<unsigned short>(columns);
        ws.ws_row = static_cast<unsigned short>(rows);
        ::ioctl(m_master_fd, TIOCSWINSZ, &ws);
    }
}

bool PosixTTYBackend::is_running() const noexcept {
    return m_running;
}

bool PosixTTYBackend::check_exit(uint32_t& out_exit_code) {
    if (m_process_id <= 0) {
        out_exit_code = 0;
        return true;
    }
    int status = 0;
    const pid_t wait_result = ::waitpid(m_process_id, &status, WNOHANG);
    if (wait_result == m_process_id) {
        const int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        out_exit_code = static_cast<uint32_t>(exit_code);
        m_running = false;
        return true;
    }
    return false;
}

} // namespace Zenvra::Terminal
#endif
