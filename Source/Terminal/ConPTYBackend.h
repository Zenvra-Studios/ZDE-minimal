#pragma once

#include "Terminal/TerminalBackend.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <vector>

namespace Zenvra::Terminal {

#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE 0x00020016
#endif

#ifndef HPCON
DECLARE_HANDLE(HPCON);
#endif

class ConPTYBackend final : public ITerminalBackend {
public:
    ConPTYBackend();
    ~ConPTYBackend() override;

    [[nodiscard]] static bool is_supported() noexcept;

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
        return TerminalBackendType::ConPTY;
    }

    [[nodiscard]] bool check_exit(uint32_t& out_exit_code) override;

    [[nodiscard]] std::chrono::steady_clock::time_point get_start_time() const noexcept override {
        return m_start_time;
    }

    [[nodiscard]] uint32_t get_pid() const noexcept override {
        return m_process_id;
    }

private:
    HANDLE m_process = nullptr;
    HANDLE m_input_write = nullptr;
    HANDLE m_output_read = nullptr;
    HPCON m_pseudo_console = nullptr;
    LPPROC_THREAD_ATTRIBUTE_LIST m_attribute_list = nullptr;
    std::vector<BYTE> m_attribute_buffer;
    DWORD m_process_id = 0;
    std::chrono::steady_clock::time_point m_start_time{};
    bool m_running = false;
};

} // namespace Zenvra::Terminal
#endif
