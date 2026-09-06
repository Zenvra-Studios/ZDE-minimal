#pragma once

#include "Terminal/TerminalBackend.h"

#if !defined(_WIN32)
#include <sys/types.h>
#endif

namespace Zenvra::Terminal {

#if !defined(_WIN32)
class PosixTTYBackend final : public ITerminalBackend {
public:
    PosixTTYBackend();
    ~PosixTTYBackend() override;

    [[nodiscard]] bool start(
        const std::filesystem::path& executable,
        const std::wstring& arguments,
        const std::filesystem::path& working_directory,
        std::size_t columns,
        std::size_t rows) override;

    void stop() noexcept override;

    [[nodiscard]] bool write_input(std::string_view text) override;

    [[nodiscard]] std::size_t read_output(std::span<char> buffer) override;

    void resize(std::size_t columns, std::size_t rows) noexcept override;

    [[nodiscard]] bool is_running() const noexcept override;

    [[nodiscard]] bool is_pty() const noexcept override { return true; }

    [[nodiscard]] TerminalBackendType get_type() const noexcept override {
        return TerminalBackendType::PosixTTY;
    }

    [[nodiscard]] bool check_exit(uint32_t& out_exit_code) override;

    [[nodiscard]] std::chrono::steady_clock::time_point get_start_time() const noexcept override {
        return m_start_time;
    }

    [[nodiscard]] uint32_t get_pid() const noexcept override {
        return static_cast<uint32_t>(m_process_id);
    }

private:
    int m_master_fd = -1;
    pid_t m_process_id = -1;
    std::chrono::steady_clock::time_point m_start_time{};
    bool m_running = false;
};
#endif

} // namespace Zenvra::Terminal
