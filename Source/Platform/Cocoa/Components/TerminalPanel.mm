#import <Cocoa/Cocoa.h>

#include "Platform/Cocoa/Components/TerminalPanel.h"
#include "Platform/Cocoa/Components/StudioWorkspaceRenderer.h"
#include "Services/Output/OutputLogManager.h"
#include "Utility/Fonts.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <span>
#include <string_view>

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

static std::string utf8_substr_columns(std::string_view s, std::size_t start_col, std::size_t count_col)
{
  if (s.empty()) return {};
  std::size_t col = 0;
  std::size_t byte_start = s.size();
  std::size_t byte_end = s.size();

  for (std::size_t i = 0; i < s.size(); )
  {
    if (col == start_col)
    {
      byte_start = i;
    }
    if (col == start_col + count_col)
    {
      byte_end = i;
      break;
    }
    unsigned char c = static_cast<unsigned char>(s[i]);
    std::size_t len = 1;
    if ((c & 0x80) == 0) len = 1;
    else if ((c & 0xE0) == 0xC0) len = 2;
    else if ((c & 0xF0) == 0xE0) len = 3;
    else if ((c & 0xF8) == 0xF0) len = 4;

    len = std::min(len, s.size() - i);
    i += len;
    ++col;
  }
  if (byte_start >= s.size()) return {};
  return std::string(s.substr(byte_start, byte_end - byte_start));
}

static void copy_text_to_clipboard(const std::string& text) {
  if (text.empty()) return;
  NSPasteboard* pb = [NSPasteboard generalPasteboard];
  [pb clearContents];
  [pb setString:[NSString stringWithUTF8String:text.c_str()] forType:NSPasteboardTypeString];
}

} // namespace

namespace Zenvra::Platform::Cocoa::Components {

bool TerminalPanel::toggle() {
  const bool was_visible = m_model.is_visible();
  const bool changed = m_model.toggle(current_terminal_directory(m_working_directory));
  if (m_model.is_visible() && !was_visible) {
    m_resize_model.reset();
  } else if (!m_model.is_visible()) {
    static_cast<void>(m_resize_model.end_resize());
    static_cast<void>(m_resize_model.set_hovered(false));
  }
  return changed;
}

bool TerminalPanel::create_terminal() {
  if (!m_model.is_visible()) {
    m_model.toggle(current_terminal_directory(m_working_directory));
    m_resize_model.reset();
  } else {
    m_model.create_session(current_terminal_directory(m_working_directory));
  }
  m_active_channel = PanelChannel::Terminal;
  m_model.set_focused(true);
  m_cursor_blink.reset();
  return true;
}

bool TerminalPanel::handle_pointer_press(
    const UI::Editor::StudioEditorLayoutResult &layout, float px, float py,
    double event_time) {
  if (!is_visible()) {
    return false;
  }
  if (is_resize_handle_point(layout, px, py)) {
    if (event_time - m_last_resize_click_time < 0.35 &&
        std::abs(px - m_last_resize_click_x) < 4.0F &&
        std::abs(py - m_last_resize_click_y) < 4.0F) {
      m_last_resize_click_time = 0.0;
      return m_resize_model.toggle_maximized();
    }
    m_last_resize_click_time = event_time;
    m_last_resize_click_x = px;
    m_last_resize_click_y = py;
    return m_resize_model.begin_resize();
  }
  if (!layout.terminal_panel_bounds.contains(px, py)) {
    return false;
  }
  m_model.set_focused(true);
  m_cursor_blink.reset();

  if (terminal_channel_tab_bounds(layout).contains(px, py)) {
    m_active_channel = PanelChannel::Terminal;
    return true;
  }
  if (output_channel_tab_bounds(layout).contains(px, py)) {
    m_active_channel = PanelChannel::Output;
    return true;
  }
  if (m_active_channel == PanelChannel::Output && clear_output_button_bounds(layout).contains(px, py)) {
    Services::Output::OutputLogManager::instance().clear(Services::Output::OutputCategory::Build);
    return true;
  }

  if (close_button_bounds(layout).contains(px, py)) {
    static_cast<void>(toggle());
    return true;
  }

  if (add_button_bounds(layout).contains(px, py)) {
    return m_model.create_session(current_terminal_directory(m_working_directory));
  }
  const std::span<const Terminal::TerminalSessionEntry> sessions = m_model.get_sessions();
  for (std::size_t index = 0; index < sessions.size(); ++index) {
    UI::Rect tab = session_tab_bounds(layout, index);
    const std::size_t id = sessions[index].identifier;
    if (m_tab_animated_offset_x.contains(id)) {
      tab.x += m_tab_animated_offset_x[id];
    }
    if (tab.contains(px, py)) {
      const float close_btn_w = 22.0F * layout.dpi_scale;
      if (px >= tab.right() - close_btn_w) {
        const float shift = tab.width;
        static_cast<void>(m_model.close_session(index));
        const auto remaining = m_model.get_sessions();
        for (std::size_t k = index; k < remaining.size(); ++k) {
          m_tab_animated_offset_x[remaining[k].identifier] += shift;
        }
        return true;
      }
      static_cast<void>(m_model.activate_session(index));
      m_cursor_blink.reset();
      return true;
    }
  }
  if (layout.terminal_content_bounds.contains(px, py)) {
    const float line_height = std::max(16.0F * layout.dpi_scale, 12.0F * layout.dpi_scale);
    const float padding_x = 10.0F * layout.dpi_scale;
    const float content_top_padding = 5.0F * layout.dpi_scale;
    const float local_y = py - (layout.terminal_content_bounds.y + content_top_padding);
    const int row = static_cast<int>(std::floor(local_y / line_height));
    const std::size_t offset = std::min(m_model.get_scroll_offset(), m_last_total_rows);
    const std::size_t end = m_last_total_rows > offset ? m_last_total_rows - offset : 0;
    const std::size_t start = end > m_last_visible_rows ? end - m_last_visible_rows : 0;
    const std::size_t line_idx = start + std::clamp(row, 0, static_cast<int>(m_last_visible_rows > 0 ? m_last_visible_rows - 1 : 0));

    const float local_x = px - (layout.terminal_content_bounds.x + padding_x);
    const float glyph_w = std::max(8.0F * layout.dpi_scale, 1.0F);
    const int col = static_cast<int>(std::round(local_x / glyph_w));
    const std::size_t col_idx = std::max(0, col);

    m_model.start_selection(line_idx, col_idx);
    return true;
  }
  m_model.clear_selection();
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
  m_model.clear_selection();
  return m_model.send_text(text);
}
bool TerminalPanel::handle_key(Terminal::TerminalInputKey key) {
  m_cursor_blink.reset();
  m_model.clear_selection();
  return m_model.send_key(key);
}
bool TerminalPanel::handle_control(char letter) {
  if ((letter == 'c' || letter == 'C') && m_model.has_selection()) {
    const std::string sel_text = m_model.get_selected_text();
    copy_text_to_clipboard(sel_text);
    m_model.clear_selection();
    return true;
  }
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
bool TerminalPanel::tick_animations() noexcept {
  bool animating = false;
  for (auto &[id, offset_x] : m_tab_animated_offset_x) {
    if (std::abs(offset_x) > 0.5F) {
      offset_x += (0.0F - offset_x) * 0.3F;
      animating = true;
    } else {
      offset_x = 0.0F;
    }
  }
  return m_cursor_blink.tick() || animating;
}
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

  const float scale = surface.m_dpi_scale;

  // 1. Background fills
  surface.fill_rectangle(context, layout.terminal_panel_bounds, surface.m_colors.editor_background);
  surface.fill_rectangle(context, layout.terminal_header_bounds, surface.m_colors.tab_background);

  // 2. Borders
  // Header bottom border
  surface.draw_line(context,
      round_to_int(layout.terminal_header_bounds.x),
      round_to_int(layout.terminal_header_bounds.bottom() - 1.0F),
      round_to_int(layout.terminal_header_bounds.right()),
      round_to_int(layout.terminal_header_bounds.bottom() - 1.0F),
      surface.m_colors.border);

  // Panel top border (with resize highlight)
  surface.draw_line(context,
      round_to_int(layout.terminal_panel_bounds.x),
      round_to_int(layout.terminal_panel_bounds.y),
      round_to_int(layout.terminal_panel_bounds.right()),
      round_to_int(layout.terminal_panel_bounds.y),
      surface.m_colors.border);
  if (m_resize_model.is_hovered() || m_resize_model.is_resizing()) {
    surface.fill_rectangle(context,
        UI::Rect{layout.terminal_panel_bounds.x,
                 layout.terminal_panel_bounds.y - 1.0F * scale,
                 layout.terminal_panel_bounds.width,
                 2.5F * scale},
        surface.m_colors.accent);
  }

  // Panel bottom border
  surface.draw_line(context,
      round_to_int(layout.terminal_panel_bounds.x),
      round_to_int(layout.terminal_panel_bounds.bottom() - 1.0F),
      round_to_int(layout.terminal_panel_bounds.right()),
      round_to_int(layout.terminal_panel_bounds.bottom() - 1.0F),
      surface.m_colors.border);

  // Left & right borders
  surface.draw_line(context,
      round_to_int(layout.terminal_panel_bounds.x),
      round_to_int(layout.terminal_panel_bounds.y),
      round_to_int(layout.terminal_panel_bounds.x),
      round_to_int(layout.terminal_panel_bounds.bottom()),
      surface.m_colors.border);
  surface.draw_line(context,
      round_to_int(layout.terminal_panel_bounds.right() - 1.0F),
      round_to_int(layout.terminal_panel_bounds.y),
      round_to_int(layout.terminal_panel_bounds.right() - 1.0F),
      round_to_int(layout.terminal_panel_bounds.bottom()),
      surface.m_colors.border);

  if (layout.terminal_panel_bounds.is_empty()) {
    return;
  }

  // 3. Channel Switcher Tabs: [ Terminal ] [ Output ]
  const UI::Rect term_tab = terminal_channel_tab_bounds(layout);
  const UI::Rect out_tab = output_channel_tab_bounds(layout);
  const float bar_h = std::max(2.0F * scale, 2.0F);

  // Terminal Channel Tab
  if (m_active_channel == PanelChannel::Terminal) {
    surface.fill_rectangle(context, term_tab, surface.m_colors.tab_active_background);
    surface.fill_rectangle(context, UI::Rect{term_tab.x, term_tab.y, term_tab.width, bar_h}, surface.m_colors.accent);
    surface.draw_line(context, round_to_int(term_tab.x), round_to_int(term_tab.y), round_to_int(term_tab.x), round_to_int(term_tab.bottom()), surface.m_colors.border);
    surface.draw_line(context, round_to_int(term_tab.right()), round_to_int(term_tab.y), round_to_int(term_tab.right()), round_to_int(term_tab.bottom()), surface.m_colors.border);
  } else {
    surface.draw_line(context, round_to_int(term_tab.x), round_to_int(term_tab.bottom() - 1.0F), round_to_int(term_tab.right()), round_to_int(term_tab.bottom() - 1.0F), surface.m_colors.border);
    surface.draw_line(context, round_to_int(term_tab.right()), round_to_int(term_tab.y + 4.0F * scale), round_to_int(term_tab.right()), round_to_int(term_tab.bottom() - 4.0F * scale), surface.m_colors.border);
  }
  const int term_text_w = surface.m_small_font->getTextWidth("Terminal");
  const float term_text_x = term_tab.x + (term_tab.width - static_cast<float>(term_text_w)) * 0.5F;
  surface.draw_text(context, *surface.m_small_font, "Terminal",
      term_text_x,
      term_tab.y + term_tab.height * 0.5F,
      (m_active_channel == PanelChannel::Terminal) ? surface.m_text.primary : surface.m_text.muted);

  // Output Channel Tab
  if (m_active_channel == PanelChannel::Output) {
    surface.fill_rectangle(context, out_tab, surface.m_colors.tab_active_background);
    surface.fill_rectangle(context, UI::Rect{out_tab.x, out_tab.y, out_tab.width, bar_h}, surface.m_colors.accent);
    surface.draw_line(context, round_to_int(out_tab.x), round_to_int(out_tab.y), round_to_int(out_tab.x), round_to_int(out_tab.bottom()), surface.m_colors.border);
    surface.draw_line(context, round_to_int(out_tab.right()), round_to_int(out_tab.y), round_to_int(out_tab.right()), round_to_int(out_tab.bottom()), surface.m_colors.border);
  } else {
    surface.draw_line(context, round_to_int(out_tab.x), round_to_int(out_tab.bottom() - 1.0F), round_to_int(out_tab.right()), round_to_int(out_tab.bottom() - 1.0F), surface.m_colors.border);
    surface.draw_line(context, round_to_int(out_tab.right()), round_to_int(out_tab.y + 4.0F * scale), round_to_int(out_tab.right()), round_to_int(out_tab.bottom() - 4.0F * scale), surface.m_colors.border);
  }
  const int out_text_w = surface.m_small_font->getTextWidth("Output");
  const float out_text_x = out_tab.x + (out_tab.width - static_cast<float>(out_text_w)) * 0.5F;
  surface.draw_text(context, *surface.m_small_font, "Output",
      out_text_x,
      out_tab.y + out_tab.height * 0.5F,
      (m_active_channel == PanelChannel::Output) ? surface.m_text.primary : surface.m_text.muted);

  // 4. Output View Rendering
  if (m_active_channel == PanelChannel::Output) {
    // Clear button on the right
    const UI::Rect clear_btn = clear_output_button_bounds(layout);
    surface.fill_rounded_rectangle(context, clear_btn, surface.m_colors.tab_background, 3.0F * scale);
    surface.draw_rectangle(context, clear_btn, surface.m_colors.border);
    const int clear_w = surface.m_small_font->getTextWidth("Clear");
    surface.draw_text(context, *surface.m_small_font, "Clear",
        clear_btn.x + (clear_btn.width - static_cast<float>(clear_w)) * 0.5F,
        clear_btn.y + clear_btn.height * 0.5F,
        surface.m_text.muted);

    // Draw Close (x) Button
    const UI::Rect close = close_button_bounds(layout);
    surface.draw_line(context, round_to_int(close.x + 9.0F * scale), round_to_int(close.y + 9.0F * scale),
        round_to_int(close.right() - 9.0F * scale), round_to_int(close.bottom() - 9.0F * scale), surface.m_colors.text_muted);
    surface.draw_line(context, round_to_int(close.right() - 9.0F * scale), round_to_int(close.y + 9.0F * scale),
        round_to_int(close.x + 9.0F * scale), round_to_int(close.bottom() - 9.0F * scale), surface.m_colors.text_muted);

    // Render Output Log Lines
    const auto output_lines = Services::Output::OutputLogManager::instance().get_lines(Services::Output::OutputCategory::Build);
    const float line_height = std::max(
        static_cast<float>(surface.m_editor_font->getHeight()) + 2.0F * scale,
        14.0F * scale);
    const float content_top_padding = 6.0F * scale;
    const float padding_x = 12.0F * scale;

    surface.push_clip(context, layout.terminal_content_bounds);

    float cur_y = layout.terminal_content_bounds.y + content_top_padding + line_height * 0.5F;
    for (std::size_t i = 0; i < output_lines.size(); ++i) {
      if (cur_y > layout.terminal_content_bounds.bottom() + line_height) break;
      if (cur_y >= layout.terminal_content_bounds.y - line_height) {
        const std::string& l = output_lines[i];
        std::string txt_color = surface.m_text.primary;
        if (l.find("[ERROR]") != std::string::npos || l.find("error:") != std::string::npos || l.find("FAILED") != std::string::npos) {
          txt_color = "#f38ba8";
        } else if (l.find("[WARNING]") != std::string::npos || l.find("warning:") != std::string::npos) {
          txt_color = surface.m_text.warning;
        } else if (l.find("SUCCESS") != std::string::npos || l.find("successfully") != std::string::npos) {
          txt_color = surface.m_text.success;
        } else if (l.starts_with("[Build]") || l.starts_with("[CMake]")) {
          txt_color = surface.m_text.accent;
        }
        surface.draw_text(context, *surface.m_editor_font, l,
            layout.terminal_content_bounds.x + padding_x, cur_y, txt_color);
      }
      cur_y += line_height;
    }
    surface.pop_clip(context);
    return;
  }

  // 5. Multi-Terminal Session Tabs
  const std::span<const Terminal::TerminalSessionEntry> sessions = m_model.get_sessions();
  const std::optional<std::size_t> active_index = m_model.get_active_index();
  for (std::size_t index = 0; index < sessions.size(); ++index) {
    UI::Rect tab = session_tab_bounds(layout, index);
    const std::size_t id = sessions[index].identifier;
    if (m_tab_animated_offset_x.contains(id)) {
      tab.x += m_tab_animated_offset_x[id];
    }
    const bool active = active_index && *active_index == index;
    if (active) {
      surface.fill_rectangle(context, tab, surface.m_colors.tab_active_background);
      surface.fill_rectangle(context, UI::Rect{tab.x, tab.y, tab.width, bar_h}, surface.m_colors.accent);
      surface.draw_line(context, round_to_int(tab.x), round_to_int(tab.y), round_to_int(tab.x), round_to_int(tab.bottom()), surface.m_colors.border);
      surface.draw_line(context, round_to_int(tab.right()), round_to_int(tab.y), round_to_int(tab.right()), round_to_int(tab.bottom()), surface.m_colors.border);
    } else {
      surface.draw_line(context, round_to_int(tab.x), round_to_int(tab.bottom() - 1.0F), round_to_int(tab.right()), round_to_int(tab.bottom() - 1.0F), surface.m_colors.border);
      surface.draw_line(context, round_to_int(tab.right()), round_to_int(tab.y + 4.0F * scale), round_to_int(tab.right()), round_to_int(tab.bottom() - 4.0F * scale), surface.m_colors.border);
    }

    surface.draw_svg_icon(context, "Assets/icons/terminal.svg",
        round_to_int(tab.x + 12.0F * scale),
        round_to_int(tab.y + tab.height * 0.5F),
        std::max(round_to_int(13.0F * scale), 10),
        active ? surface.m_palette.text_primary : surface.m_palette.text_muted,
        active ? surface.m_palette.tab_active_background : surface.m_palette.tab_background);

    surface.draw_text(context, *surface.m_small_font, sessions[index].title,
        tab.x + 23.0F * scale, tab.y + tab.height * 0.5F,
        active ? surface.m_text.primary : surface.m_text.muted);

    // Close 'x' button inside active/hovered tab
    if (active || sessions.size() > 1) {
      const float cx = tab.right() - 12.0F * scale;
      const float cy = tab.y + tab.height * 0.5F;
      const float r = 3.5F * scale;
      surface.draw_line(context, round_to_int(cx - r), round_to_int(cy - r),
          round_to_int(cx + r), round_to_int(cy + r), surface.m_colors.text_muted);
      surface.draw_line(context, round_to_int(cx + r), round_to_int(cy - r),
          round_to_int(cx - r), round_to_int(cy + r), surface.m_colors.text_muted);
    }
  }

  if (m_tab_animated_offset_x.size() > sessions.size() + 8) {
    std::unordered_set<std::size_t> active_ids;
    for (const auto &s : sessions) {
      active_ids.insert(s.identifier);
    }
    std::erase_if(m_tab_animated_offset_x, [&](const auto &item) { return !active_ids.contains(item.first); });
  }

  // Draw Add (+) Button
  const UI::Rect add = add_button_bounds(layout);
  surface.draw_line(context, round_to_int(add.x + add.width * 0.5F), round_to_int(add.y + 8.0F * scale),
      round_to_int(add.x + add.width * 0.5F), round_to_int(add.bottom() - 8.0F * scale), surface.m_colors.text_muted);
  surface.draw_line(context, round_to_int(add.x + 8.0F * scale), round_to_int(add.y + add.height * 0.5F),
      round_to_int(add.right() - 8.0F * scale), round_to_int(add.y + add.height * 0.5F), surface.m_colors.text_muted);

  // Draw Close (x) Button
  const UI::Rect close = close_button_bounds(layout);
  surface.draw_line(context, round_to_int(close.x + 9.0F * scale), round_to_int(close.y + 9.0F * scale),
      round_to_int(close.right() - 9.0F * scale), round_to_int(close.bottom() - 9.0F * scale), surface.m_colors.text_muted);
  surface.draw_line(context, round_to_int(close.right() - 9.0F * scale), round_to_int(close.y + 9.0F * scale),
      round_to_int(close.x + 9.0F * scale), round_to_int(close.bottom() - 9.0F * scale), surface.m_colors.text_muted);

  // Shell Status Pill on the Right
  const Terminal::TerminalSession* session = m_model.get_active_session();
  if (session == nullptr || surface.m_editor_font == nullptr) {
    return;
  }
  if (sessions.size() <= 4 &&
      layout.terminal_header_bounds.width >= 560.0F * scale) {
    const std::string shell_label = "Local  " + session->get_shell_path().string();
    const int label_width = surface.m_small_font->getTextWidth(shell_label);
    const float pill_w = static_cast<float>(label_width) + 24.0F * scale;
    const float pill_x = close.x - 12.0F * scale - pill_w;
    const float pill_y = layout.terminal_header_bounds.y + 4.0F * scale;
    const float pill_h = layout.terminal_header_bounds.height - 8.0F * scale;

    const UI::Rect pill_rect{pill_x, pill_y, pill_w, pill_h};
    surface.fill_rounded_rectangle(context, pill_rect, surface.m_colors.tab_background, 3.0F * scale);
    surface.draw_rectangle(context, pill_rect, surface.m_colors.border);

    // Live status dot
    const CGFloat green_dot[4] = {0.14F, 0.82F, 0.55F, 1.0F};
    const CGFloat gray_dot[4] = {0.50F, 0.50F, 0.50F, 1.0F};
    surface.fill_rounded_rectangle(context,
        UI::Rect{pill_x + 6.0F * scale, pill_y + (pill_h - 6.0F * scale) * 0.5F, 6.0F * scale, 6.0F * scale},
        session->is_running() ? green_dot : gray_dot, 3.0F * scale);

    surface.draw_text(context, *surface.m_small_font, shell_label,
        pill_x + 16.0F * scale,
        layout.terminal_header_bounds.y + layout.terminal_header_bounds.height * 0.5F,
        session->is_running() ? surface.m_text.primary : surface.m_text.muted);
  }

  // 6. Terminal Text & Grid Metrics
  const float padding_x = 12.0F * scale;
  const float line_height = std::max(
      static_cast<float>(surface.m_editor_font->getHeight()) + 2.0F * scale,
      14.0F * scale);
  const float content_top_padding = 6.0F * scale;
  const float content_bottom_padding = 8.0F * scale;
  const float usable_content_height = std::max(
      layout.terminal_content_bounds.height - (content_top_padding + content_bottom_padding), 0.0F);
  const std::size_t visible_rows = usable_content_height > 0.0F
      ? std::max<std::size_t>(
          static_cast<std::size_t>(std::floor(usable_content_height / line_height)), 1)
      : 0;
  const double sample_w = static_cast<double>(surface.m_editor_font->getTextWidth("01234567890123456789"));
  const float glyph_width = std::max(static_cast<float>(sample_w / 20.0), 1.0F);
  const std::size_t visible_columns = static_cast<std::size_t>(std::max(
      (layout.terminal_content_bounds.width - padding_x * 2.0F) / glyph_width,
      10.0F));

  m_model.resize(visible_columns, std::max<std::size_t>(visible_rows, 1));
  if (visible_rows == 0) {
    return;
  }
  const std::span<const std::string> lines = session->get_lines();

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

  const std::size_t end = session->is_in_alternate_screen()
      ? std::min(lines.size(), visible_rows)
      : (lines.size() - offset);
  const std::size_t start = session->is_in_alternate_screen()
      ? 0
      : (end > visible_rows ? end - visible_rows : 0);

  surface.push_clip(context, layout.terminal_content_bounds);

  const float first_center_y = layout.terminal_content_bounds.y + content_top_padding +
                               line_height * 0.5F;
  float current_y = first_center_y;
  const Terminal::TerminalSelection& selection = m_model.get_selection();
  const bool has_selection = m_model.has_selection();

  // 7. Render Terminal Lines with Full ANSI / TrueColor Spans
  for (std::size_t index = start; index < end; ++index) {
    if (current_y + line_height * 0.5F > layout.terminal_content_bounds.bottom()) {
      break;
    }
    const std::string& line = lines[index];
    const std::size_t len = line.size();
    const auto spans = session->get_line_spans(index);
    float cur_x = layout.terminal_content_bounds.x + padding_x;

    if (spans.empty()) {
      if (!line.empty()) {
        surface.draw_text(context, *surface.m_editor_font, line,
                          cur_x, current_y, surface.m_colors.text_primary);
      }
    } else {
      for (const auto& span : spans) {
        if (span.text.empty()) {
          continue;
        }
        const int span_w = surface.m_editor_font->getTextWidth(span.text);
        if (span_w <= 0) {
          continue;
        }

        // Draw background if specified or if inverse
        if (!span.attributes.background.is_default || span.attributes.inverse) {
          CGFloat bg_rgba[4] = {1.0, 1.0, 1.0, 1.0};
          if (span.attributes.inverse) {
            if (span.attributes.foreground.is_default) {
              bg_rgba[0] = surface.m_colors.text_primary[0];
              bg_rgba[1] = surface.m_colors.text_primary[1];
              bg_rgba[2] = surface.m_colors.text_primary[2];
              bg_rgba[3] = 1.0;
            } else {
              bg_rgba[0] = static_cast<CGFloat>(span.attributes.foreground.r) / 255.0;
              bg_rgba[1] = static_cast<CGFloat>(span.attributes.foreground.g) / 255.0;
              bg_rgba[2] = static_cast<CGFloat>(span.attributes.foreground.b) / 255.0;
              bg_rgba[3] = 1.0;
            }
          } else {
            bg_rgba[0] = static_cast<CGFloat>(span.attributes.background.r) / 255.0;
            bg_rgba[1] = static_cast<CGFloat>(span.attributes.background.g) / 255.0;
            bg_rgba[2] = static_cast<CGFloat>(span.attributes.background.b) / 255.0;
            bg_rgba[3] = 1.0;
          }
          surface.fill_rectangle(
              context,
              UI::Rect{cur_x, current_y - line_height * 0.5F, static_cast<float>(span_w), line_height},
              bg_rgba);
        }

        // Determine foreground color
        CGFloat fg_rgba[4] = {1.0, 1.0, 1.0, 1.0};
        if (span.attributes.inverse) {
          if (span.attributes.background.is_default) {
            fg_rgba[0] = surface.m_colors.editor_background[0];
            fg_rgba[1] = surface.m_colors.editor_background[1];
            fg_rgba[2] = surface.m_colors.editor_background[2];
            fg_rgba[3] = 1.0;
          } else {
            fg_rgba[0] = static_cast<CGFloat>(span.attributes.background.r) / 255.0;
            fg_rgba[1] = static_cast<CGFloat>(span.attributes.background.g) / 255.0;
            fg_rgba[2] = static_cast<CGFloat>(span.attributes.background.b) / 255.0;
            fg_rgba[3] = 1.0;
          }
        } else if (span.attributes.foreground.is_default) {
          fg_rgba[0] = surface.m_colors.text_primary[0];
          fg_rgba[1] = surface.m_colors.text_primary[1];
          fg_rgba[2] = surface.m_colors.text_primary[2];
          fg_rgba[3] = 1.0;
        } else {
          fg_rgba[0] = static_cast<CGFloat>(span.attributes.foreground.r) / 255.0;
          fg_rgba[1] = static_cast<CGFloat>(span.attributes.foreground.g) / 255.0;
          fg_rgba[2] = static_cast<CGFloat>(span.attributes.foreground.b) / 255.0;
          fg_rgba[3] = 1.0;
        }

        if (span.attributes.bold) {
          fg_rgba[0] = std::min<CGFloat>(fg_rgba[0] + 0.18, 1.0);
          fg_rgba[1] = std::min<CGFloat>(fg_rgba[1] + 0.18, 1.0);
          fg_rgba[2] = std::min<CGFloat>(fg_rgba[2] + 0.18, 1.0);
        }

        if (span.attributes.dim) {
          fg_rgba[0] *= 0.65;
          fg_rgba[1] *= 0.65;
          fg_rgba[2] *= 0.65;
        }

        // Draw text
        if (!span.attributes.hidden) {
          surface.draw_text(context, *surface.m_editor_font, span.text,
                            cur_x, current_y, fg_rgba);
        }

        // Draw underline
        if (span.attributes.underline) {
          const int ul_y = round_to_int(current_y + line_height * 0.38F);
          surface.draw_line(context, round_to_int(cur_x), ul_y,
                            round_to_int(cur_x + span_w), ul_y, fg_rgba);
        }

        cur_x += static_cast<float>(span_w);
      }
    }

    // Selection Highlight Overlay
    if (has_selection && selection.intersects_line(index)) {
      const auto [col_start, col_end] = selection.get_line_range(index, len);
      if (col_end > col_start) {
        const std::string pre_sel = utf8_substr_columns(line, 0, col_start);
        const std::string sel_str = utf8_substr_columns(line, col_start, col_end - col_start);
        const int sel_x = round_to_int(layout.terminal_content_bounds.x + padding_x) +
            surface.m_editor_font->getTextWidth(pre_sel);
        const int sel_w = std::max(surface.m_editor_font->getTextWidth(sel_str), 4);

        const CGFloat sel_rgba[4] = {
          surface.m_colors.accent[0],
          surface.m_colors.accent[1],
          surface.m_colors.accent[2],
          0.35F
        };
        surface.fill_rectangle(
            context,
            UI::Rect{static_cast<float>(sel_x), current_y - line_height * 0.5F, static_cast<float>(sel_w), line_height},
            sel_rgba);
      }
    }

    current_y += line_height;
  }

  surface.pop_clip(context);

  // 8. Vertical Scrollbar
  if (lines.size() > visible_rows) {
    const float track_top = layout.terminal_content_bounds.y + content_top_padding;
    const float track_bottom = layout.terminal_content_bounds.bottom() - content_bottom_padding;
    const float track_height = std::max(track_bottom - track_top, 1.0F);
    const float thumb_height = std::max(
        track_height * static_cast<float>(visible_rows) / static_cast<float>(lines.size()),
        18.0F * scale);
    const std::size_t maximum_start = lines.size() - visible_rows;
    const float progress = maximum_start == 0 ? 0.0F
        : static_cast<float>(start) / static_cast<float>(maximum_start);

    surface.fill_rounded_rectangle(
        context,
        UI::Rect{layout.terminal_content_bounds.right() - 5.0F * scale,
            track_top + progress * std::max(track_height - thumb_height, 0.0F),
            3.0F * scale,
            std::min(thumb_height, track_height)},
        surface.m_colors.text_muted, 1.5F * scale);
  }

  // 9. Blinking Cursor
  const std::size_t cursor_line_idx = session->get_cursor_line();
  const std::size_t cursor_col_idx = session->get_cursor_column();
  if (is_focused() && cursor_line_idx >= start && cursor_line_idx < end && m_cursor_blink.is_visible()) {
    const std::size_t visual_row = cursor_line_idx - start;
    const float line_center_y = first_center_y + static_cast<float>(visual_row) * line_height;
    const float caret_height = line_height - 2.0F * scale;
    const float caret_width = std::max(2.0F * scale, 2.0F);

    std::string cursor_prefix;
    if (cursor_line_idx < lines.size()) {
      const std::string &target_line = lines[cursor_line_idx];
      if (cursor_col_idx > 0) {
        cursor_prefix = utf8_substr_columns(target_line, 0, cursor_col_idx);
      }
    }

    const int cursor_x = round_to_int(layout.terminal_content_bounds.x + padding_x) +
        surface.m_editor_font->getTextWidth(cursor_prefix);
    const float cursor_y = line_center_y - caret_height * 0.5F;
    surface.fill_rectangle(
        context,
        UI::Rect{static_cast<float>(cursor_x), cursor_y,
            caret_width, caret_height},
        surface.m_colors.text_primary);
  }
}

UI::Rect TerminalPanel::terminal_channel_tab_bounds(
    const UI::Editor::StudioEditorLayoutResult &layout) const noexcept {
  const float scale = layout.dpi_scale;
  return UI::Rect{
      layout.terminal_header_bounds.x,
      layout.terminal_header_bounds.y,
      78.0F * scale,
      layout.terminal_header_bounds.height};
}

UI::Rect TerminalPanel::output_channel_tab_bounds(
    const UI::Editor::StudioEditorLayoutResult &layout) const noexcept {
  const float scale = layout.dpi_scale;
  return UI::Rect{
      layout.terminal_header_bounds.x + 78.0F * scale,
      layout.terminal_header_bounds.y,
      72.0F * scale,
      layout.terminal_header_bounds.height};
}

UI::Rect TerminalPanel::clear_output_button_bounds(
    const UI::Editor::StudioEditorLayoutResult &layout) const noexcept {
  const float scale = layout.dpi_scale;
  return UI::Rect{
      layout.terminal_header_bounds.right() - 84.0F * scale,
      layout.terminal_header_bounds.y + 4.0F * scale,
      48.0F * scale,
      layout.terminal_header_bounds.height - 8.0F * scale};
}

UI::Rect TerminalPanel::session_tab_bounds(
    const UI::Editor::StudioEditorLayoutResult &layout,
    std::size_t index) const noexcept {
  const float scale = layout.dpi_scale;
  const float start_x = layout.terminal_header_bounds.x + 150.0F * scale;
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
  const float start_x = layout.terminal_header_bounds.x + 150.0F * scale;
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
