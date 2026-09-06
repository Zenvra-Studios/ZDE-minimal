#include "TerminalPanelModel.h"

#include <algorithm>
#include <cctype>

namespace Zenvra::Terminal {

/**
 *
 *
 **/
bool TerminalPanelModel::toggle(const std::filesystem::path &working_directory) {
  m_visible = !m_visible;
  m_focused = m_visible;
  if (m_visible && m_sessions.empty()) {
    static_cast<void>(create_session(working_directory));
  }
  return true;
}

/**
 *
 *
 */
bool TerminalPanelModel::create_session(
    const std::filesystem::path &working_directory) {
  if (m_sessions.size() >= maximum_sessions) {
    return false;
  }
  auto session = std::make_unique<TerminalSession>();
  static_cast<void>(session->start(working_directory, m_columns, m_rows));
  // Read initial output immediately with zero latency
  static_cast<void>(session->poll());

  const std::size_t identifier = m_next_identifier++;
  std::string shell_name = session->get_shell_path().stem().string();
  if (shell_name.empty()) {
    shell_name = "Terminal";
  }
  m_sessions.push_back(TerminalSessionEntry{
      .identifier = identifier,
      .title = shell_name,
      .session = std::move(session),
  });
  m_active_index = m_sessions.size() - 1;
  m_scroll_offset = 0;
  m_visible = true;
  m_focused = true;
  return true;
}

/**
 *
 *
 **/
bool TerminalPanelModel::close_active_session() {
  if (!m_active_index || *m_active_index >= m_sessions.size()) {
    return false;
  }
  remove_session(*m_active_index);
  return true;
}

bool TerminalPanelModel::close_session(std::size_t index) {
  if (index >= m_sessions.size()) {
    return false;
  }
  remove_session(index);
  return true;
}

/**
 *
 *
 */
bool TerminalPanelModel::activate_session(std::size_t index) noexcept {
  if (index >= m_sessions.size()) {
    return false;
  }
  const bool changed = !m_active_index || *m_active_index != index;
  m_active_index = index;
  m_scroll_offset = 0;
  m_visible = true;
  m_focused = true;
  return changed;
}

/**
 *
 *
 */
void TerminalPanelModel::shutdown() noexcept {
  for (TerminalSessionEntry &entry : m_sessions) {
    entry.session->stop();
  }
  m_sessions.clear();
  m_active_index.reset();
  m_visible = false;
  m_focused = false;
  m_scroll_offset = 0;
}

/**
 *
 *
 */
bool TerminalPanelModel::send_text(std::string_view text) {
  TerminalSession *session = get_active_session();
  if (!m_visible || !m_focused || session == nullptr) {
    return false;
  }
  m_scroll_offset = 0;
  return session->write_input(text);
}

/**
 *
 *
 */
bool TerminalPanelModel::send_key(TerminalInputKey key) {
  TerminalSession *session = get_active_session();
  if (!m_visible || !m_focused || session == nullptr) {
    return false;
  }
  m_scroll_offset = 0;

  switch (key) {
  case TerminalInputKey::Enter:
    return session->write_input("\r");
  case TerminalInputKey::Backspace:
#if defined(_WIN32)
    return session->write_input("\x08");
#else
    return session->write_input("\x7F");
#endif
  case TerminalInputKey::Tab:
    return session->write_input("\t");
  case TerminalInputKey::Escape:
    return session->write_input("\x1B");
  case TerminalInputKey::ArrowUp:
    return session->write_input("\x1B[A");
  case TerminalInputKey::ArrowDown:
    return session->write_input("\x1B[B");
  case TerminalInputKey::ArrowRight:
    return session->write_input("\x1B[C");
  case TerminalInputKey::ArrowLeft:
    return session->write_input("\x1B[D");
  case TerminalInputKey::Home:
    return session->write_input("\x1B[H");
  case TerminalInputKey::End:
    return session->write_input("\x1B[F");
  case TerminalInputKey::PageUp:
    return session->write_input("\x1B[5~");
  case TerminalInputKey::PageDown:
    return session->write_input("\x1B[6~");
  case TerminalInputKey::DeleteForward:
    return session->write_input("\x1B[3~");
  case TerminalInputKey::DeleteWordBackward:
    return session->write_input("\x17");
  case TerminalInputKey::DeleteWordForward:
    return session->write_input("\x1B\x64");
  }
  return false;
}

/**
 *
 *
 */
bool TerminalPanelModel::send_control(char letter) {
  const unsigned char normalized = static_cast<unsigned char>(
      std::toupper(static_cast<unsigned char>(letter)));
  if (normalized < 'A' || normalized > 'Z') {
    return false;
  }
  const char control = static_cast<char>(normalized - 'A' + 1);
  return send_text(std::string_view{&control, 1});
}

/**
 *
 *
 */
bool TerminalPanelModel::poll() {
  bool changed = false;
  for (std::size_t i = 0; i < m_sessions.size(); ++i) {
    TerminalSessionEntry &entry = m_sessions[i];
    changed = entry.session->poll() || changed;

    std::string tab_title = entry.session->get_title();
    if (tab_title.empty()) {
      tab_title = entry.session->get_shell_path().stem().string();
    }
    if (!tab_title.empty() && tab_title != "Terminal" &&
        entry.title != tab_title) {
      entry.title = tab_title;
      changed = true;
    }
  }
  return changed;
}

/**
 *
 *
 */
void TerminalPanelModel::remove_session(std::size_t index) noexcept {
  if (index >= m_sessions.size()) {
    return;
  }

  const bool removed_active = m_active_index && *m_active_index == index;
  m_sessions[index].session->stop();
  m_sessions.erase(m_sessions.begin() + static_cast<std::ptrdiff_t>(index));
  if (m_sessions.empty()) {
    m_active_index.reset();
    m_visible = false;
    m_focused = false;
    m_scroll_offset = 0;
    return;
  }

  if (!m_active_index) {
    m_active_index = 0;
  } else if (removed_active) {
    m_active_index = std::min(index, m_sessions.size() - 1);
    m_scroll_offset = 0;
  } else if (index < *m_active_index) {
    --*m_active_index;
  }
}

/**
 *
 *
 */
bool TerminalPanelModel::scroll(std::ptrdiff_t line_delta,
                                std::size_t maximum_offset, std::size_t column,
                                std::size_t row) noexcept {
  TerminalSession *session = get_active_session();
  if (session == nullptr || line_delta == 0) {
    return false;
  }

  if (session->send_mouse_scroll(line_delta, column, row)) {
    return true;
  }

  const std::size_t previous = m_scroll_offset;
  if (line_delta < 0) {
    m_scroll_offset =
        std::min(maximum_offset,
                 m_scroll_offset + static_cast<std::size_t>(-line_delta));
  } else {
    const std::size_t amount = static_cast<std::size_t>(line_delta);
    m_scroll_offset = amount > m_scroll_offset ? 0 : m_scroll_offset - amount;
  }
  return previous != m_scroll_offset;
}

bool TerminalPanelModel::send_mouse_button(TerminalSession::MouseButton button,
                                           TerminalSession::MouseAction action,
                                           std::size_t column, std::size_t row,
                                           bool shift, bool meta, bool ctrl) {
  if (TerminalSession *session = get_active_session()) {
    return session->send_mouse_button(button, action, column, row, shift, meta,
                                      ctrl);
  }
  return false;
}

bool TerminalPanelModel::send_mouse_motion(
    std::size_t column, std::size_t row, bool button_pressed,
    TerminalSession::MouseButton pressed_button, bool shift, bool meta,
    bool ctrl) {
  if (TerminalSession *session = get_active_session()) {
    return session->send_mouse_motion(column, row, button_pressed,
                                      pressed_button, shift, meta, ctrl);
  }
  return false;
}

bool TerminalPanelModel::is_mouse_tracking_active() const noexcept {
  if (const TerminalSession *session = get_active_session()) {
    return session->is_mouse_tracking_active();
  }
  return false;
}

void TerminalPanelModel::resize(std::size_t columns,
                                std::size_t rows) noexcept {
  m_columns = columns;
  m_rows = rows;
  for (auto &entry : m_sessions) {
    if (entry.session) {
      entry.session->resize(columns, rows);
    }
  }
}

/**
 *
 *
 */
bool TerminalPanelModel::is_visible() const noexcept { return m_visible; }

/**
 *
 *
 */
bool TerminalPanelModel::is_focused() const noexcept {
  return m_visible && m_focused;
}

void TerminalPanelModel::set_focused(bool focused) noexcept {
  m_focused = m_visible && focused;
}

/**
 *
 *
 */
std::size_t TerminalPanelModel::get_scroll_offset() const noexcept {
  return m_scroll_offset;
}

std::optional<std::size_t>
TerminalPanelModel::get_active_index() const noexcept {
  return m_active_index;
}

std::span<const TerminalSessionEntry>
TerminalPanelModel::get_sessions() const noexcept {
  return m_sessions;
}

TerminalSession *TerminalPanelModel::get_active_session() noexcept {
  return m_active_index && *m_active_index < m_sessions.size()
             ? m_sessions[*m_active_index].session.get()
             : nullptr;
}

const TerminalSession *TerminalPanelModel::get_active_session() const noexcept {
  return m_active_index && *m_active_index < m_sessions.size()
             ? m_sessions[*m_active_index].session.get()
             : nullptr;
}

bool TerminalPanelModel::is_active_session_conpty() const noexcept {
  const TerminalSession *session = get_active_session();
  return session != nullptr && session->is_conpty_mode();
}

void TerminalPanelModel::start_selection(std::size_t line,
                                         std::size_t column) noexcept {
  m_selection.start = TerminalPosition{line, column};
  m_selection.end = TerminalPosition{line, column};
}

void TerminalPanelModel::update_selection(std::size_t line,
                                          std::size_t column) noexcept {
  m_selection.end = TerminalPosition{line, column};
}

void TerminalPanelModel::clear_selection() noexcept {
  m_selection.start = TerminalPosition{0, 0};
  m_selection.end = TerminalPosition{0, 0};
}

void TerminalPanelModel::select_word(std::size_t line,
                                     std::size_t column) noexcept {
  const TerminalSession *session = get_active_session();
  if (session == nullptr) {
    return;
  }
  const std::span<const std::string> lines = session->get_lines();
  if (line >= lines.size()) {
    return;
  }
  const std::string &text = lines[line];
  if (text.empty()) {
    return;
  }
  std::size_t col = std::min(column, text.size() - 1);

  auto is_word_char = [](char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' ||
           c == '-' || c == '.';
  };

  if (!is_word_char(text[col])) {
    m_selection.start = TerminalPosition{line, col};
    m_selection.end = TerminalPosition{line, col + 1};
    return;
  }

  std::size_t start_col = col;
  while (start_col > 0 && is_word_char(text[start_col - 1])) {
    --start_col;
  }
  std::size_t end_col = col;
  while (end_col < text.size() && is_word_char(text[end_col])) {
    ++end_col;
  }
  m_selection.start = TerminalPosition{line, start_col};
  m_selection.end = TerminalPosition{line, end_col};
}

void TerminalPanelModel::select_line(std::size_t line) noexcept {
  const TerminalSession *session = get_active_session();
  if (session == nullptr) {
    return;
  }
  const std::span<const std::string> lines = session->get_lines();
  if (line >= lines.size()) {
    return;
  }
  m_selection.start = TerminalPosition{line, 0};
  m_selection.end = TerminalPosition{line, lines[line].size()};
}

bool TerminalPanelModel::has_selection() const noexcept {
  return !m_selection.is_empty();
}

const TerminalSelection &TerminalPanelModel::get_selection() const noexcept {
  return m_selection;
}

std::string TerminalPanelModel::get_selected_text() const {
  const TerminalSession *session = get_active_session();
  if (session == nullptr || m_selection.is_empty()) {
    return {};
  }
  return m_selection.extract_text(session->get_lines());
}

} // namespace Zenvra::Terminal
