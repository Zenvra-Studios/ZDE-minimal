#include "Platform/Cocoa/Components/TerminalPanel.h"
#include "Platform/Cocoa/Components/StudioWorkspaceRenderer.h"
#include "Utility/Fonts.h"

#include <algorithm>
#include <cmath>

namespace Zenvra::Platform::Cocoa::Components {

int round_to_int(float v) { return static_cast<int>(std::lround(v)); }

bool TerminalPanel::toggle() { return m_model.toggle(m_working_directory); }

bool TerminalPanel::handle_pointer_press(
    const UI::Editor::StudioEditorLayoutResult &layout, float px, float py,
    double event_time) {
  if (!is_visible())
    return false;
  if (is_resize_handle_point(layout, px, py)) {
    m_resize_model.begin_resize();
    return true;
  }
  if (contains(layout, px, py)) {
    m_model.set_focused(true);
    return true;
  }
  return false;
}

bool TerminalPanel::handle_pointer_move(
    const UI::Editor::StudioEditorLayoutResult &, float, float) noexcept {
  return false;
}

bool TerminalPanel::handle_pointer_drag(
    const UI::Editor::StudioEditorLayoutResult &layout, float py) noexcept {
  if (!m_resize_model.is_resizing())
    return false;
  (void)py;
  return true;
}

bool TerminalPanel::handle_pointer_release() noexcept {
  return m_resize_model.end_resize();
}

bool TerminalPanel::handle_text_input(std::string_view text) {
  return m_model.send_text(text);
}
bool TerminalPanel::handle_key(Terminal::TerminalInputKey key) {
  return m_model.send_key(key);
}
bool TerminalPanel::handle_control(char letter) {
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
  return contains(layout, px, py);
}

void TerminalPanel::render(const StudioWorkspaceRenderer &surface,
                           CGContextRef context,
                           const UI::Editor::StudioEditorLayoutResult &layout) {
  if (!is_visible())
    return;

  // Terminal header
  surface.fill_rectangle(context, layout.terminal_header_bounds,
                         surface.m_colors.tab_background);
  surface.draw_line(context, round_to_int(layout.terminal_header_bounds.x),
                    round_to_int(layout.terminal_header_bounds.y),
                    round_to_int(layout.terminal_header_bounds.right()),
                    round_to_int(layout.terminal_header_bounds.y),
                    surface.m_colors.border);

  const float header_center_y = layout.terminal_header_bounds.y +
                                layout.terminal_header_bounds.height * 0.5F;
  surface.draw_text(context, *surface.m_small_font, "Terminal",
                    layout.terminal_header_bounds.x +
                        12.0F * surface.m_dpi_scale,
                    header_center_y, surface.m_text.primary);

  // Terminal content area
  surface.fill_rectangle(context, layout.terminal_content_bounds,
                         surface.m_colors.editor_background);

  // Render terminal output
  const auto &sessions = m_model.get_sessions();
  const auto active_idx = m_model.get_active_index();
  if (!sessions.empty() && active_idx.has_value()) {
    const auto &active = sessions[active_idx.value()];
    const auto &buffer = active.session->get_lines();
    const float line_height =
        static_cast<float>(surface.m_editor_font->getHeight());
    const float content_top_padding = 4.0F * surface.m_dpi_scale;
    const float content_bottom_padding = 4.0F * surface.m_dpi_scale;
    const float available_height = layout.terminal_content_bounds.height -
                                   content_top_padding - content_bottom_padding;
    const std::size_t visible_rows =
        static_cast<std::size_t>(available_height / line_height);

    std::size_t max_line_length = 0;
    for (const auto &line : buffer) {
      max_line_length = std::max(max_line_length, line.size());
    }

    const float visible_width_chars =
        (layout.terminal_content_bounds.width - 16.0F * surface.m_dpi_scale) /
        static_cast<float>(
            std::max(1, surface.m_editor_font->getTextWidth("X")));
    const std::size_t visible_columns =
        static_cast<std::size_t>(std::max(0.0F, visible_width_chars));

    const std::size_t offset =
        std::min(m_model.get_scroll_offset(), buffer.size());

    if (is_focused() && offset == 0 && !buffer.empty()) {
      const std::size_t last_line_len = buffer.back().size();
      if (last_line_len > m_horizontal_scroll_offset + visible_columns) {
        m_horizontal_scroll_offset = last_line_len - visible_columns;
      } else if (last_line_len < m_horizontal_scroll_offset) {
        m_horizontal_scroll_offset = last_line_len > visible_columns
                                         ? last_line_len - visible_columns
                                         : 0;
      }
    }

    const std::size_t end = buffer.size() - offset;
    const std::size_t start = end > visible_rows ? end - visible_rows : 0;
    m_last_total_rows = buffer.size();
    m_last_visible_rows = visible_rows;

    /*
            std::size_t max_line_length = 0;
            for (const auto& line : buffer) {
                max_line_length = std::max(max_line_length, line.size());
            }
    */

    surface.push_clip(context, layout.terminal_content_bounds);

    float current_y = layout.terminal_content_bounds.y + content_top_padding +
                      line_height * 0.5F;
    for (std::size_t index = start; index < end; ++index) {
      const std::size_t h_offset = m_horizontal_scroll_offset;
      const std::size_t len = buffer[index].size();
      const std::string_view line =
          len > h_offset
              ? std::string_view{buffer[index]}.substr(
                    h_offset, std::min(visible_columns, len - h_offset))
              : std::string_view{};
      if (!line.empty()) {
        surface.draw_text(context, *surface.m_editor_font, std::string(line),
                          layout.terminal_content_bounds.x +
                              8.0F * surface.m_dpi_scale,
                          current_y, surface.m_text.primary);
      }
      current_y += line_height;
    }

    surface.pop_clip(context);

    if (buffer.size() > visible_rows) {
      const float track_top = layout.terminal_content_bounds.y +
                              content_top_padding - surface.m_dpi_scale;
      const float track_bottom =
          layout.terminal_content_bounds.bottom() - content_bottom_padding;
      const float track_height = std::max(track_bottom - track_top, 1.0F);
      const float thumb_height =
          std::max(track_height * static_cast<float>(visible_rows) /
                       static_cast<float>(buffer.size()),
                   18.0F * surface.m_dpi_scale);
      const std::size_t maximum_start = buffer.size() - visible_rows;
      const float progress =
          maximum_start == 0
              ? 0.0F
              : static_cast<float>(start) / static_cast<float>(maximum_start);

      CGFloat scrollbar_rgba[4];
      StudioWorkspaceRenderer::color_to_rgba(surface.m_palette.text_muted,
                                             scrollbar_rgba);
      surface.fill_rounded_rectangle(
          context,
          UI::Rect{layout.terminal_content_bounds.right() -
                       4.0F * surface.m_dpi_scale,
                   track_top +
                       progress * std::max(track_height - thumb_height, 0.0F),
                   2.0F * surface.m_dpi_scale,
                   std::min(thumb_height, track_height)},
          scrollbar_rgba, 1.0F * surface.m_dpi_scale);
    }

    if (max_line_length > visible_columns) {
      const float track_left =
          layout.terminal_content_bounds.x + 8.0F * surface.m_dpi_scale;
      const float track_right =
          layout.terminal_content_bounds.right() - 14.0F * surface.m_dpi_scale;
      const float track_width = std::max(track_right - track_left, 1.0F);
      const float thumb_width =
          std::max(track_width * static_cast<float>(visible_columns) /
                       static_cast<float>(max_line_length),
                   18.0F * surface.m_dpi_scale);
      const std::size_t maximum_start = max_line_length - visible_columns;
      const float progress =
          maximum_start == 0 ? 0.0F
                             : static_cast<float>(m_horizontal_scroll_offset) /
                                   static_cast<float>(maximum_start);

      CGFloat scrollbar_rgba[4];
      StudioWorkspaceRenderer::color_to_rgba(surface.m_palette.text_muted,
                                             scrollbar_rgba);
      surface.fill_rectangle(
          context,
          UI::Rect{
              track_left + progress * std::max(track_width - thumb_width, 0.0F),
              layout.terminal_content_bounds.bottom() -
                  4.0F * surface.m_dpi_scale,
              std::min(thumb_width, track_width), 2.0F * surface.m_dpi_scale},
          scrollbar_rgba);
    }

    if (is_focused() && offset == 0 && !buffer.empty() &&
        m_cursor_blink.is_visible()) {
      const std::size_t h_offset = m_horizontal_scroll_offset;
      const std::string &last = buffer.back();
      const std::size_t len = last.size();
      const std::string cursor_text =
          len > h_offset
              ? last.substr(h_offset, std::min(visible_columns, len - h_offset))
              : "";

      const float first_center_y = layout.terminal_content_bounds.y +
                                   content_top_padding + line_height * 0.5F;
      const float displayed_rows =
          start < end ? static_cast<float>(end - start) : 0.0F;
      const float line_center_y =
          first_center_y + std::max(displayed_rows - 1.0F, 0.0F) * line_height;
      const float caret_height = line_height - 2.0F * surface.m_dpi_scale;

      int text_width = 0;
      if (surface.m_editor_font && !cursor_text.empty()) {
        text_width = surface.m_editor_font->getTextWidth(cursor_text);
      }

      const float cursor_x = layout.terminal_content_bounds.x +
                             8.0F * surface.m_dpi_scale +
                             static_cast<float>(text_width);
      const float cursor_y = line_center_y - caret_height * 0.5F;

      CGFloat caret_rgba[4];
      StudioWorkspaceRenderer::color_to_rgba(surface.m_palette.text_primary,
                                             caret_rgba);
      surface.fill_rectangle(context,
                             UI::Rect{cursor_x, cursor_y,
                                      std::max(surface.m_dpi_scale, 1.0F),
                                      caret_height},
                             caret_rgba);
    }
  }
}

UI::Rect TerminalPanel::session_tab_bounds(
    const UI::Editor::StudioEditorLayoutResult &layout,
    std::size_t) const noexcept {
  return {};
}
UI::Rect TerminalPanel::add_button_bounds(
    const UI::Editor::StudioEditorLayoutResult &) const noexcept {
  return {};
}
UI::Rect TerminalPanel::close_button_bounds(
    const UI::Editor::StudioEditorLayoutResult &) const noexcept {
  return {};
}

UI::Rect TerminalPanel::resize_handle_bounds(
    const UI::Editor::StudioEditorLayoutResult &layout) const noexcept {
  return {layout.terminal_panel_bounds.x, layout.terminal_panel_bounds.y - 3.0F,
          layout.terminal_panel_bounds.width, 6.0F};
}

} // namespace Zenvra::Platform::Cocoa::Components
