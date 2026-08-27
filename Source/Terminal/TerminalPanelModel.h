#pragma once
#include "Terminal/TerminalSelection.h"
#include "Terminal/TerminalSession.h"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Zenvra::Terminal {

enum class TerminalInputKey {
  Enter,
  Backspace,
  Tab,
  Escape,
  ArrowUp,
  ArrowDown,
  ArrowLeft,
  ArrowRight,
  Home,
  End,
  PageUp,
  PageDown,
  DeleteForward,
  DeleteWordBackward,
  DeleteWordForward
};

struct TerminalSessionEntry {
  std::size_t identifier = 0;
  std::string title;
  std::unique_ptr<TerminalSession> session;
};

class TerminalPanelModel {
public:
  [[nodiscard]] bool
  toggle(const std::filesystem::path &working_directory = {});
  [[nodiscard]] bool
  create_session(const std::filesystem::path &working_directory = {});
  [[nodiscard]] bool close_active_session();
  [[nodiscard]] bool close_session(std::size_t index);
  [[nodiscard]] bool activate_session(std::size_t index) noexcept;
  void shutdown() noexcept;

  [[nodiscard]] bool send_text(std::string_view text);
  [[nodiscard]] bool send_key(TerminalInputKey key);
  [[nodiscard]] bool send_control(char letter);
  [[nodiscard]] bool poll();
  [[nodiscard]] bool scroll(std::ptrdiff_t line_delta,
                            std::size_t maximum_offset,
                            std::size_t column = 1,
                            std::size_t row = 1) noexcept;
  [[nodiscard]] bool send_mouse_button(
      TerminalSession::MouseButton button,
      TerminalSession::MouseAction action,
      std::size_t column, std::size_t row,
      bool shift = false, bool meta = false, bool ctrl = false);
  [[nodiscard]] bool send_mouse_motion(
      std::size_t column, std::size_t row,
      bool button_pressed,
      TerminalSession::MouseButton pressed_button = TerminalSession::MouseButton::Left,
      bool shift = false, bool meta = false, bool ctrl = false);
  [[nodiscard]] bool is_mouse_tracking_active() const noexcept;
  void resize(std::size_t columns, std::size_t rows) noexcept;

  [[nodiscard]] bool is_visible() const noexcept;
  [[nodiscard]] bool is_focused() const noexcept;
  void set_focused(bool focused) noexcept;
  [[nodiscard]] std::size_t get_scroll_offset() const noexcept;
  [[nodiscard]] std::optional<std::size_t> get_active_index() const noexcept;
  [[nodiscard]] std::span<const TerminalSessionEntry> get_sessions() const noexcept;

  [[nodiscard]] TerminalSession *get_active_session() noexcept;
  [[nodiscard]] const TerminalSession *get_active_session() const noexcept;

  void start_selection(std::size_t line, std::size_t column) noexcept;
  void update_selection(std::size_t line, std::size_t column) noexcept;
  void clear_selection() noexcept;
  void select_word(std::size_t line, std::size_t column) noexcept;
  void select_line(std::size_t line) noexcept;
  [[nodiscard]] bool has_selection() const noexcept;
  [[nodiscard]] const TerminalSelection &get_selection() const noexcept;
  [[nodiscard]] std::string get_selected_text() const;

private:
  static constexpr std::size_t maximum_sessions = 8;

  void remove_session(std::size_t index) noexcept;

  std::vector<TerminalSessionEntry> m_sessions;
  std::optional<std::size_t> m_active_index;
  TerminalSelection m_selection;
  std::size_t m_next_identifier = 1;
  std::size_t m_scroll_offset = 0;
  std::size_t m_columns = 240;
  std::size_t m_rows = 30;
  bool m_visible = false;
  bool m_focused = false;
};

} // namespace Zenvra::Terminal
