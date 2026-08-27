#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
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

    [[nodiscard]] bool start(const std::filesystem::path& working_directory = {},
                             std::size_t initial_columns = 160,
                             std::size_t initial_rows = 30);
    void stop() noexcept;
    [[nodiscard]] bool write_input(std::string_view text);
    [[nodiscard]] bool poll();
    void resize(std::size_t columns, std::size_t rows) noexcept;

    enum class MouseTracking
    {
        Off,
        X10,
        ButtonEvent,
        AnyEvent,
    };

    enum class MouseButton
    {
        Left = 0,
        Middle = 1,
        Right = 2,
        Release = 3,
    };

    enum class MouseAction
    {
        Press,
        Release,
        Motion,
    };

    [[nodiscard]] bool is_running() const noexcept;
    [[nodiscard]] const std::filesystem::path& get_shell_path() const noexcept;
    [[nodiscard]] std::span<const std::string> get_lines() const noexcept;
    [[nodiscard]] std::size_t get_cursor_line() const noexcept { return m_cursor_line; }
    [[nodiscard]] std::size_t get_cursor_column() const noexcept { return m_cursor_column; }
    [[nodiscard]] bool is_in_alternate_screen() const noexcept { return m_in_alternate_screen; }
    [[nodiscard]] bool is_mouse_tracking_active() const noexcept { return m_mouse_tracking != MouseTracking::Off; }
    [[nodiscard]] MouseTracking get_mouse_tracking() const noexcept { return m_mouse_tracking; }
    [[nodiscard]] bool send_mouse_scroll(std::ptrdiff_t line_delta, std::size_t column = 1, std::size_t row = 1);
    [[nodiscard]] bool send_mouse_button(MouseButton button, MouseAction action,
                                         std::size_t column, std::size_t row,
                                         bool shift = false, bool meta = false, bool ctrl = false);
    [[nodiscard]] bool send_mouse_motion(std::size_t column, std::size_t row,
                                         bool button_pressed, MouseButton pressed_button = MouseButton::Left,
                                         bool shift = false, bool meta = false, bool ctrl = false);
    [[nodiscard]] bool navigate_history(bool up);
    [[nodiscard]] static std::filesystem::path resolve_host_shell();
    void consume_output(std::string_view output);

private:
    struct Implementation;

    enum class ParserState
    {
        Text,
        Escape,
        DesignateCharacterSet,
        ControlSequence,
        OperatingSystemCommand,
        OperatingSystemCommandEscape,
        DeviceControlString,
        DeviceControlStringEscape,
    };

    void apply_control_sequence(char command);
    void append_codepoint(std::string_view utf8_char);
    void append_character(char character);
    void append_line();
    void append_status(std::string message);
    void clear_screen() noexcept;
    void trim_scrollback();

    std::unique_ptr<Implementation> m_implementation;
    std::filesystem::path m_shell_path;
    std::filesystem::path m_working_directory;
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
    std::size_t m_pending_input_cursor = 0;
    std::vector<std::string> m_command_history;
    std::optional<std::size_t> m_history_index;
    std::string m_saved_pending_input;
    std::string m_utf8_sequence;
    std::size_t m_utf8_expected = 0;
    ParserState m_parser_state = ParserState::Text;
    std::string m_control_sequence;
    std::size_t m_columns = 240;
    std::size_t m_rows = 24;
    MouseTracking m_mouse_tracking = MouseTracking::Off;
    bool m_sgr_mouse = false;
    bool m_application_cursor_keys = false;
    bool m_alternate_scroll = true;
    bool m_in_alternate_screen = false;
    bool m_running = false;
};

} // namespace Zenvra::Terminal
