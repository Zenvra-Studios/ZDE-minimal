#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace Zenvra::Terminal {

enum class TerminalBackendType {
    ConPTY,
    PosixTTY,
    PipeFallback,
};

void terminal_debug_log(std::string_view msg);

class ITerminalBackend {
public:
    virtual ~ITerminalBackend() = default;

    [[nodiscard]] virtual bool start(
        const std::filesystem::path& executable,
        const std::wstring& arguments,
        const std::filesystem::path& working_directory,
        std::size_t columns,
        std::size_t rows) = 0;

    virtual void stop() noexcept = 0;

    [[nodiscard]] virtual bool write_input(std::string_view text) = 0;

    [[nodiscard]] virtual std::size_t read_output(std::span<char> buffer) = 0;

    virtual void resize(std::size_t columns, std::size_t rows) noexcept = 0;

    [[nodiscard]] virtual bool is_running() const noexcept = 0;

    [[nodiscard]] virtual bool is_pty() const noexcept = 0;

    [[nodiscard]] virtual TerminalBackendType get_type() const noexcept = 0;

    [[nodiscard]] virtual bool check_exit(uint32_t& out_exit_code) = 0;

    [[nodiscard]] virtual std::chrono::steady_clock::time_point get_start_time() const noexcept = 0;

    [[nodiscard]] virtual uint32_t get_pid() const noexcept = 0;
};

} // namespace Zenvra::Terminal
