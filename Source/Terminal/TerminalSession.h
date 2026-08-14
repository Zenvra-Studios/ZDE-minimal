#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Zenvra::Terminal
{

class TerminalSession
{
public:
    TerminalSession();
    ~TerminalSession();

    TerminalSession(const TerminalSession&) = delete;
    TerminalSession& operator=(const TerminalSession&) = delete;

    [[nodiscard]] bool start(const std::filesystem::path& working_directory = {});
    void stop() noexcept;
    [[nodiscard]] bool write_input(std::string_view text);
    [[nodiscard]] bool poll();
    void resize(std::size_t columns, std::size_t rows) noexcept;

    [[nodiscard]] bool is_running() const noexcept;
    [[nodiscard]] const std::filesystem::path& get_shell_path() const noexcept;
    [[nodiscard]] std::span<const std::string> get_lines() const noexcept;
    [[nodiscard]] std::size_t get_cursor_line() const noexcept { return m_cursor_line; }
    [[nodiscard]] std::size_t get_cursor_column() const noexcept { return m_cursor_column; }
    [[nodiscard]] bool is_in_alternate_screen() const noexcept { return m_in_alternate_screen; }
    [[nodiscard]] static std::filesystem::path resolve_host_shell();

private:
    struct Implementation;

    enum class ParserState
    {
        Text,
        Escape,
        ControlSequence,
        OperatingSystemCommand,
        OperatingSystemCommandEscape,
    };

    void consume_output(std::string_view output);
    void apply_control_sequence(char command);
    void append_codepoint(std::string_view utf8_char);
    void append_character(char character);
    void append_line();
    void append_status(std::string message);
    void clear_screen() noexcept;
    void trim_scrollback();
    [[nodiscard]] bool navigate_history(bool up);

    std::unique_ptr<Implementation> m_implementation;
    std::filesystem::path m_shell_path;
    std::vector<std::string> m_lines{std::string{}};
    std::vector<std::string> m_main_screen_lines;
    std::size_t m_cursor_line = 0;
    std::size_t m_cursor_column = 0;
    std::size_t m_saved_cursor_line = 0;
    std::size_t m_saved_cursor_column = 0;
    std::size_t m_main_cursor_line = 0;
    std::size_t m_main_cursor_column = 0;
    std::size_t m_input_start_column = 0;
    std::string m_pending_input;
    std::vector<std::string> m_command_history;
    std::optional<std::size_t> m_history_index;
    std::string m_saved_pending_input;
    std::string m_utf8_sequence;
    std::size_t m_utf8_expected = 0;
    ParserState m_parser_state = ParserState::Text;
    std::string m_control_sequence;
    std::size_t m_columns = 100;
    std::size_t m_rows = 24;
    bool m_in_alternate_screen = false;
    bool m_running = false;
};

} // namespace Zenvra::Terminal
