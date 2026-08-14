#include "Platform/Cocoa/Components/TerminalPanel.h"
#include "Platform/Cocoa/Components/StudioWorkspaceRenderer.h"
#include "Utility/Fonts.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <span>

namespace Zenvra::Platform::Cocoa::Components {

static inline int round_to_int(float v) { return static_cast<int>(std::lround(v)); }

static std::filesystem::path current_terminal_directory(const std::filesystem::path& working_directory)
{
    std::error_code ec;
    if (!working_directory.empty() && std::filesystem::is_directory(working_directory, ec))
    {
        return working_directory;
    }
    const std::filesystem::path current = std::filesystem::current_path(ec);
    if (!ec && !current.empty())
    {
        return current;
    }
    const char* home = std::getenv("HOME");
    if (home != nullptr && *home != '\0')
    {
        return std::filesystem::path{home};
    }
    return std::filesystem::path{};
}

bool TerminalPanel::toggle() { return m_model.toggle(current_terminal_directory(m_working_directory)); }

bool TerminalPanel::handle_pointer_press(
    const UI::Editor::StudioEditorLayoutResult &layout, float px, float py,
    double event_time) {
  if (is_resize_handle_point(layout, px, py)) {
    constexpr double double_click_interval = 0.4;
    const float maximum_distance = 5.0F * layout.dpi_scale;
    const bool double_click = m_last_resize_click_time != 0.0 &&
        event_time - m_last_resize_click_time <= double_click_interval &&
        std::abs(px - m_last_resize_click_x) <= maximum_distance &&
        std::abs(py - m_last_resize_click_y) <= maximum_distance;
    if (double_click) {
      m_last_resize_click_time = 0.0;
      return m_resize_model.toggle_maximized();
    }
    m_last_resize_click_time = event_time;
    m_last_resize_click_x = px;
    m_last_resize_click_y = py;
    return m_resize_model.begin_resize();
  }
  if (!is_visible() || !layout.terminal_panel_bounds.contains(px, py)) {
    return false;
  }
  m_model.set_focused(true);
  m_cursor_blink.reset();
  if (close_button_bounds(layout).contains(px, py)) {
    return m_model.close_active_session();
  }
  if (add_button_bounds(layout).contains(px, py)) {
    return m_model.create_session(current_terminal_directory(m_working_directory));
  }
  const std::span<const Terminal::TerminalSessionEntry> sessions = m_model.get_sessions();
  for (std::size_t index = 0; index < sessions.size(); ++index) {
    if (session_tab_bounds(layout, index).contains(px, py)) {
      static_cast<void>(m_model.activate_session(index));
      m_cursor_blink.reset();
      return true;
    }
  }
  return true;
}

bool TerminalPanel::handle_pointer_move(
    const UI::Editor::StudioEditorLayoutResult &layout, float px, float py) noexcept {
  return m_resize_model.set_hovered(
      is_resizing() || is_resize_handle_point(layout, px, py));
}

bool TerminalPanel::handle_pointer_drag(
    const UI::Editor::StudioEditorLayoutResult &layout, float py) noexcept {
  return m_resize_model.resize_from_pointer(
      py,
      layout.tab_bar_bounds.bottom(),
      layout.status_bar_bounds.y,
      layout.dpi_scale);
}

bool TerminalPanel::handle_pointer_release() noexcept {
  return m_resize_model.end_resize();
}

bool TerminalPanel::handle_text_input(std::string_view text) {
  m_cursor_blink.reset();
  return m_model.send_text(text);
}
bool TerminalPanel::handle_key(Terminal::TerminalInputKey key) {
  m_cursor_blink.reset();
  return m_model.send_key(key);
}
bool TerminalPanel::handle_control(char letter) {
  m_cursor_blink.reset();
  return m_model.send_control(letter);
}
bool TerminalPanel::handle_scroll(std::ptrdiff_t delta, bool horiz) noexcept {
  if (horiz) {
    std::ptrdiff_t new_offset =
        static_cast<std::ptrdiff_t>(m_horizontal_scroll_offset) + delta;
    if (new_offset < 0)
      new_offset = 0;
    if (m_horizontal_scroll_offset != static_cast<std::size_t>(new_offset)) {
      m_horizontal_scroll_offset = static_cast<std::size_t>(new_offset);
      return true;
    }
    return false;
  } else {
    const std::size_t maximum_offset =
        m_last_total_rows > m_last_visible_rows
            ? m_last_total_rows - m_last_visible_rows
            : 0;
    return m_model.scroll(delta, maximum_offset);
  }
}
bool TerminalPanel::poll() { return m_model.poll(); }
bool TerminalPanel::tick_animations() noexcept { return m_cursor_blink.tick(); }
void TerminalPanel::shutdown() noexcept { m_model.shutdown(); }
bool TerminalPanel::is_visible() const noexcept { return m_model.is_visible(); }
bool TerminalPanel::is_focused() const noexcept { return m_model.is_focused(); }
bool TerminalPanel::is_resizing() const noexcept {
  return m_resize_model.is_resizing();
}
bool TerminalPanel::is_maximized() const noexcept {
  return m_resize_model.is_maximized();
}
float TerminalPanel::get_height() const noexcept {
  return m_resize_model.get_height();
}
void TerminalPanel::set_focused(bool focused) noexcept {
  m_model.set_focused(focused);
}
void TerminalPanel::set_working_directory(
    const std::filesystem::path &d) noexcept {
  m_working_directory = d;
}

bool TerminalPanel::contains(const UI::Editor::StudioEditorLayoutResult &layout,
                             float px, float py) const noexcept {
  return is_visible() && layout.terminal_panel_bounds.contains(px, py);
}

bool TerminalPanel::is_resize_handle_point(
    const UI::Editor::StudioEditorLayoutResult &layout, float px,
    float py) const noexcept {
  if (!is_visible())
    return false;
  const UI::Rect handle = resize_handle_bounds(layout);
  return handle.contains(px, py);
}

bool TerminalPanel::is_interactive_point(
    const UI::Editor::StudioEditorLayoutResult &layout, float px,
    float py) const noexcept {
  if (!is_visible())
    return false;
  const std::span<const Terminal::TerminalSessionEntry> sessions = m_model.get_sessions();
  for (std::size_t index = 0; index < sessions.size(); ++index) {
    if (session_tab_bounds(layout, index).contains(px, py)) {
      return true;
    }
  }
  if (add_button_bounds(layout).contains(px, py) ||
      close_button_bounds(layout).contains(px, py)) {
    return true;
  }
  return contains(layout, px, py);
}

void TerminalPanel::render(const StudioWorkspaceRenderer &surface,
                           CGContextRef context,
                           const UI::Editor::StudioEditorLayoutResult &layout) {
  if (!is_visible())
    return;

  // Background
  surface.fill_rectangle(context, layout.terminal_panel_bounds, surface.m_colors.editor_background);
  surface.fill_rectangle(context, layout.terminal_header_bounds, surface.m_colors.tab_background);

  // Header bottom border
  surface.draw_line(context,
      round_to_int(layout.terminal_header_bounds.x),
      round_to_int(layout.terminal_header_bounds.bottom() - 1.0F),
      round_to_int(layout.terminal_header_bounds.right()),
      round_to_int(layout.terminal_header_bounds.bottom() - 1.0F),
      surface.m_colors.border);

  // Splitter bar top line
  const CGFloat* splitter_color = (m_resize_model.is_hovered() || m_resize_model.is_resizing())
      ? surface.m_colors.accent
      : surface.m_colors.border;
  surface.draw_line(context,
      round_to_int(layout.terminal_panel_bounds.x),
      round_to_int(layout.terminal_panel_bounds.y),
      round_to_int(layout.terminal_panel_bounds.right()),
      round_to_int(layout.terminal_panel_bounds.y),
      splitter_color);

  if (m_resize_model.is_hovered() || m_resize_model.is_resizing()) {
    surface.fill_rectangle(context,
        UI::Rect{layout.terminal_panel_bounds.x,
            layout.terminal_panel_bounds.y - surface.m_dpi_scale,
            layout.terminal_panel_bounds.width,
            std::max(2.0F * surface.m_dpi_scale, 2.0F)},
        surface.m_colors.accent);
  }

  if (layout.terminal_panel_bounds.is_empty()) {
    return;
  }

  // Draw "Terminal" label
  surface.draw_text(context, *surface.m_ui_font, "Terminal",
      layout.terminal_header_bounds.x + 10.0F * surface.m_dpi_scale,
      layout.terminal_header_bounds.y + layout.terminal_header_bounds.height * 0.5F,
      surface.m_text.primary);

  // Draw Session Tabs
  const std::span<const Terminal::TerminalSessionEntry> sessions = m_model.get_sessions();
  const std::optional<std::size_t> active_index = m_model.get_active_index();
  for (std::size_t index = 0; index < sessions.size(); ++index) {
    UI::Rect tab = session_tab_bounds(layout, index);
    const bool active = active_index && *active_index == index;
    surface.fill_rectangle(context, tab, active ? surface.m_colors.tab_active_background : surface.m_colors.tab_background);

    const int tab_left = round_to_int(tab.x);
    const int tab_right = round_to_int(tab.right()) - 1;
    const int tab_top = round_to_int(tab.y);
    const int tab_bottom = round_to_int(tab.bottom()) - 1;
    surface.draw_line(context, tab_left, tab_top, tab_right, tab_top, surface.m_colors.border);
    surface.draw_line(context, tab_left, tab_top, tab_left, tab_bottom, surface.m_colors.border);
    surface.draw_line(context, tab_right, tab_top, tab_right, tab_bottom, surface.m_colors.border);
    if (!active) {
      surface.draw_line(context, tab_left, tab_bottom, tab_right, tab_bottom, surface.m_colors.border);
    }

    surface.draw_svg_icon(context, "Assets/icons/terminal.svg",
        round_to_int(tab.x + 12.0F * surface.m_dpi_scale),
        round_to_int(tab.y + tab.height * 0.5F),
        std::max(round_to_int(13.0F * surface.m_dpi_scale), 10),
        active ? surface.m_palette.text_primary : surface.m_palette.text_muted,
        active ? surface.m_palette.tab_active_background : surface.m_palette.tab_background);

    surface.draw_text(context, *surface.m_small_font, sessions[index].title,
        tab.x + 23.0F * surface.m_dpi_scale, tab.y + tab.height * 0.5F,
        active ? surface.m_text.primary : surface.m_text.muted);
  }

  // Draw Add (+) Button
  const UI::Rect add = add_button_bounds(layout);
  surface.draw_line(context, round_to_int(add.x + add.width * 0.5F), round_to_int(add.y + 7.0F * surface.m_dpi_scale),
      round_to_int(add.x + add.width * 0.5F), round_to_int(add.bottom() - 7.0F * surface.m_dpi_scale), surface.m_colors.text_muted);
  surface.draw_line(context, round_to_int(add.x + 7.0F * surface.m_dpi_scale), round_to_int(add.y + add.height * 0.5F),
      round_to_int(add.right() - 7.0F * surface.m_dpi_scale), round_to_int(add.y + add.height * 0.5F), surface.m_colors.text_muted);

  // Draw Close (x) Button
  const UI::Rect close = close_button_bounds(layout);
  surface.draw_line(context, round_to_int(close.x + 8.0F * surface.m_dpi_scale), round_to_int(close.y + 8.0F * surface.m_dpi_scale),
      round_to_int(close.right() - 8.0F * surface.m_dpi_scale), round_to_int(close.bottom() - 8.0F * surface.m_dpi_scale), surface.m_colors.text_muted);
  surface.draw_line(context, round_to_int(close.right() - 8.0F * surface.m_dpi_scale), round_to_int(close.y + 8.0F * surface.m_dpi_scale),
      round_to_int(close.x + 8.0F * surface.m_dpi_scale), round_to_int(close.bottom() - 8.0F * surface.m_dpi_scale), surface.m_colors.text_muted);

  // Shell Info
  const Terminal::TerminalSession* session = m_model.get_active_session();
  if (session == nullptr || surface.m_editor_font == nullptr) {
    return;
  }
  if (sessions.size() <= 4 &&
      layout.terminal_header_bounds.width >= 600.0F * surface.m_dpi_scale) {
    const std::string shell_label = "Local  " + session->get_shell_path().string();
    const int label_width = surface.m_small_font->getTextWidth(shell_label);
    surface.draw_text(context, *surface.m_small_font, shell_label,
        close.x - 12.0F * surface.m_dpi_scale - static_cast<float>(label_width),
        layout.terminal_header_bounds.y + layout.terminal_header_bounds.height * 0.5F,
        session->is_running() ? surface.m_text.success : surface.m_text.muted);
  }

  const float padding_x = 10.0F * surface.m_dpi_scale;
  const float line_height = std::max(
      static_cast<float>(surface.m_editor_font->getHeight()) + 2.0F * surface.m_dpi_scale,
      12.0F * surface.m_dpi_scale);
  const float content_top_padding = 5.0F * surface.m_dpi_scale;
  const float content_bottom_padding = 8.0F * surface.m_dpi_scale;
  const float usable_content_height = std::max(
      layout.terminal_content_bounds.height - content_bottom_padding, 0.0F);
  const std::size_t visible_rows = usable_content_height > 0.0F
      ? std::max<std::size_t>(
          static_cast<std::size_t>(std::floor(usable_content_height / line_height)), 1)
      : 0;
  const int glyph_width = std::max(surface.m_editor_font->getTextWidth("M"), 1);
  const std::size_t visible_columns = static_cast<std::size_t>(std::max(
      (layout.terminal_content_bounds.width - 22.0F * surface.m_dpi_scale) /
          static_cast<float>(glyph_width),
      1.0F));

  constexpr std::size_t minimum_pty_columns = 200;
  const std::size_t pty_columns = std::max(visible_columns, minimum_pty_columns);
  m_model.resize(pty_columns, std::max<std::size_t>(visible_rows, 1));
  if (visible_rows == 0) {
    return;
  }
  const std::span<const std::string> lines = session->get_lines();
  std::size_t max_line_length = 0;
  for (const auto &line : lines) {
    max_line_length = std::max(max_line_length, line.size());
  }

  const std::size_t maximum_offset = lines.size() > visible_rows
      ? lines.size() - visible_rows
      : 0;
  const std::size_t offset = std::min(m_model.get_scroll_offset(), maximum_offset);
  m_last_total_rows = lines.size();
  m_last_visible_rows = visible_rows;

  if (m_force_horizontal_scroll_to_cursor && is_focused() && offset == 0 && !lines.empty()) {
    const std::size_t last_line_len = lines.back().size();
    if (last_line_len > m_horizontal_scroll_offset + visible_columns) {
      m_horizontal_scroll_offset = last_line_len - visible_columns;
    } else if (m_horizontal_scroll_offset > 0 && last_line_len <= visible_columns) {
      m_horizontal_scroll_offset = 0;
    }
    m_force_horizontal_scroll_to_cursor = false;
  }

  const std::size_t end = lines.size() - offset;
  const std::size_t start = end > visible_rows ? end - visible_rows : 0;
  const std::size_t displayed_rows = end - start;

  surface.push_clip(context, layout.terminal_content_bounds);

  const float first_center_y = layout.terminal_content_bounds.y + content_top_padding +
                               line_height * 0.5F;
  float current_y = first_center_y;
  for (std::size_t index = start; index < end; ++index) {
    const std::size_t h_offset = m_horizontal_scroll_offset;
    const std::size_t len = lines[index].size();
    const std::string_view line =
        len > h_offset
            ? std::string_view{lines[index]}.substr(
                  h_offset, std::min(visible_columns, len - h_offset))
            : std::string_view{};
    if (!line.empty()) {
      surface.draw_text(context, *surface.m_editor_font, std::string(line),
                        layout.terminal_content_bounds.x + padding_x,
                        current_y, surface.m_text.primary);
    }
    current_y += line_height;
  }

  surface.pop_clip(context);

  // Vertical scrollbar
  if (lines.size() > visible_rows) {
    const float track_top = layout.terminal_content_bounds.y + content_top_padding;
    const float track_bottom = layout.terminal_content_bounds.bottom() - content_bottom_padding;
    const float track_height = std::max(track_bottom - track_top, 1.0F);
    const float thumb_height = std::max(
        track_height * static_cast<float>(visible_rows) / static_cast<float>(lines.size()),
        18.0F * surface.m_dpi_scale);
    const std::size_t maximum_start = lines.size() - visible_rows;
    const float progress = maximum_start == 0 ? 0.0F
        : static_cast<float>(start) / static_cast<float>(maximum_start);

    surface.fill_rounded_rectangle(
        context,
        UI::Rect{layout.terminal_content_bounds.right() - 4.0F * surface.m_dpi_scale,
            track_top + progress * std::max(track_height - thumb_height, 0.0F),
            2.0F * surface.m_dpi_scale,
            std::min(thumb_height, track_height)},
        surface.m_colors.text_muted, 1.0F * surface.m_dpi_scale);
  }

  // Horizontal scrollbar
  if (max_line_length > visible_columns) {
    const float track_left = layout.terminal_content_bounds.x + padding_x;
    const float track_right = layout.terminal_content_bounds.right() - 14.0F * surface.m_dpi_scale;
    const float track_width = std::max(track_right - track_left, 1.0F);
    const float thumb_width = std::max(
        track_width * static_cast<float>(visible_columns) / static_cast<float>(max_line_length),
        18.0F * surface.m_dpi_scale);
    const std::size_t maximum_start = max_line_length - visible_columns;
    const float progress = maximum_start == 0 ? 0.0F
        : static_cast<float>(m_horizontal_scroll_offset) / static_cast<float>(maximum_start);

    surface.fill_rectangle(
        context,
        UI::Rect{
            track_left + progress * std::max(track_width - thumb_width, 0.0F),
            layout.terminal_content_bounds.bottom() - 4.0F * surface.m_dpi_scale,
            std::min(thumb_width, track_width), 2.0F * surface.m_dpi_scale},
        surface.m_colors.text_muted);
  }

  // Blinking cursor
  if (is_focused() && offset == 0 && !lines.empty() && m_cursor_blink.is_visible()) {
    const std::size_t h_offset = m_horizontal_scroll_offset;
    const std::string &last = lines.back();
    const std::size_t len = last.size();
    const std::string cursor_text =
        len > h_offset
            ? last.substr(h_offset, std::min(visible_columns, len - h_offset))
            : "";

    const int cursor_x = round_to_int(layout.terminal_content_bounds.x + padding_x) +
        surface.m_editor_font->getTextWidth(cursor_text) + round_to_int(6.0F * surface.m_dpi_scale);
    const float cursor_y = first_center_y + static_cast<float>(displayed_rows - 1) * line_height -
        line_height * 0.5F;
    surface.fill_rectangle(
        context,
        UI::Rect{static_cast<float>(cursor_x), cursor_y,
            std::max(surface.m_dpi_scale, 1.0F), line_height - 2.0F * surface.m_dpi_scale},
        surface.m_colors.text_primary);
  }
}

UI::Rect TerminalPanel::session_tab_bounds(
    const UI::Editor::StudioEditorLayoutResult &layout,
    std::size_t index) const noexcept {
  const float scale = layout.dpi_scale;
  const float start_x = layout.terminal_header_bounds.x + 72.0F * scale;
  const float reserved_width = 56.0F * scale;
  const float available_width = std::max(
      layout.terminal_header_bounds.right() - start_x - reserved_width, 0.0F);
  const float tab_width = std::min(
      112.0F * scale,
      available_width / static_cast<float>(std::max<std::size_t>(m_model.get_sessions().size(), 1)));
  return UI::Rect{start_x + static_cast<float>(index) * tab_width,
      layout.terminal_header_bounds.y, tab_width, layout.terminal_header_bounds.height};
}

UI::Rect TerminalPanel::add_button_bounds(
    const UI::Editor::StudioEditorLayoutResult &layout) const noexcept {
  const float scale = layout.dpi_scale;
  const float start_x = layout.terminal_header_bounds.x + 72.0F * scale;
  const float available_width = std::max(layout.terminal_header_bounds.right() - start_x - 56.0F * scale, 0.0F);
  const float tab_width = std::min(
      112.0F * scale,
      available_width / static_cast<float>(std::max<std::size_t>(m_model.get_sessions().size(), 1)));
  const float x = start_x + static_cast<float>(m_model.get_sessions().size()) * tab_width;
  return UI::Rect{x, layout.terminal_header_bounds.y,
      28.0F * scale, layout.terminal_header_bounds.height};
}

UI::Rect TerminalPanel::close_button_bounds(
    const UI::Editor::StudioEditorLayoutResult &layout) const noexcept {
  const float width = 28.0F * layout.dpi_scale;
  return UI::Rect{layout.terminal_header_bounds.right() - width,
      layout.terminal_header_bounds.y, width, layout.terminal_header_bounds.height};
}

UI::Rect TerminalPanel::resize_handle_bounds(
    const UI::Editor::StudioEditorLayoutResult &layout) const noexcept {
  const float handle_height = std::max(8.0F * layout.dpi_scale, 6.0F);
  return UI::Rect{
      layout.terminal_panel_bounds.x,
      layout.terminal_panel_bounds.y - handle_height * 0.5F,
      layout.terminal_panel_bounds.width,
      handle_height,
  };
}

} // namespace Zenvra::Platform::Cocoa::Components
