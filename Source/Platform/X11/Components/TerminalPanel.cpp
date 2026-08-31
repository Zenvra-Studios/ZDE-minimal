#include "Platform/X11/Components/TerminalPanel.h"
#include "Platform/X11/Components/StudioWorkspaceRenderer.h"
#include "Services/Output/OutputLogManager.h"
#include "Utility/Fonts.h"
#include "Utility/MathUtil.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace Zenvra::Platform::X11::Components
{

namespace
{

using Zenvra::Utility::round_to_int;


std::filesystem::path current_terminal_directory(
    const std::filesystem::path& workspace_root)
{
    if (!workspace_root.empty())
    {
        return workspace_root;
    }
    std::error_code error;
    const std::filesystem::path current = std::filesystem::current_path(error);
    return error ? std::filesystem::path{} : current;
}

std::string utf8_substr_columns(std::string_view s, std::size_t start_col, std::size_t count_col)
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

} // namespace

bool TerminalPanel::toggle()
{
    const bool was_visible = m_model.is_visible();
    const std::filesystem::path initial_directory = current_terminal_directory(m_working_directory);
    const bool changed = m_model.toggle(initial_directory);
    if (m_model.is_visible() && !was_visible)
    {
        m_resize_model.reset();
    }
    else if (!m_model.is_visible())
    {
        static_cast<void>(m_resize_model.end_resize());
        static_cast<void>(m_resize_model.set_hovered(false));
    }
    return changed;
}

bool TerminalPanel::handle_pointer_press(
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_x,
    float point_y,
    Time event_time,
    int click_count,
    bool shift_held)
{
    if (!is_visible())
    {
        return false;
    }
    if (is_resize_handle_point(layout, point_x, point_y))
    {
        constexpr Time double_click_interval = 400;
        const float maximum_distance = 5.0F * layout.dpi_scale;
        const bool double_click = m_last_resize_click_time != 0 &&
            event_time - m_last_resize_click_time <= double_click_interval &&
            std::abs(point_x - m_last_resize_click_x) <= maximum_distance &&
            std::abs(point_y - m_last_resize_click_y) <= maximum_distance;
        if (double_click)
        {
            m_last_resize_click_time = 0;
            return m_resize_model.toggle_maximized();
        }
        m_last_resize_click_time = event_time;
        m_last_resize_click_x = point_x;
        m_last_resize_click_y = point_y;
        return m_resize_model.begin_resize();
    }
    if (!layout.terminal_panel_bounds.contains(point_x, point_y))
    {
        return false;
    }
    m_model.set_focused(true);
    m_cursor_blink.reset();

    if (terminal_channel_tab_bounds(layout).contains(point_x, point_y))
    {
        m_active_channel = PanelChannel::Terminal;
        return true;
    }
    if (output_channel_tab_bounds(layout).contains(point_x, point_y))
    {
        m_active_channel = PanelChannel::Output;
        return true;
    }
    if (m_active_channel == PanelChannel::Output && clear_output_button_bounds(layout).contains(point_x, point_y))
    {
        Services::Output::OutputLogManager::instance().clear(Services::Output::OutputCategory::Build);
        return true;
    }

    if (add_button_bounds(layout).contains(point_x, point_y))
    {
        return m_model.create_session(current_terminal_directory(m_working_directory));
    }
    const std::span<const Terminal::TerminalSessionEntry> sessions = m_model.get_sessions();
    for (std::size_t index = 0; index < sessions.size(); ++index)
    {
        UI::Rect tab = session_tab_bounds(layout, index);
        const std::size_t id = sessions[index].identifier;
        if (m_tab_animated_offset_x.contains(id))
        {
            tab.x += m_tab_animated_offset_x[id];
        }
        if (tab.contains(point_x, point_y))
        {
            const float close_btn_w = 22.0F * layout.dpi_scale;
            if (point_x >= tab.right() - close_btn_w)
            {
                const float shift = tab.width;
                static_cast<void>(m_model.close_session(index));
                const auto remaining = m_model.get_sessions();
                for (std::size_t k = index; k < remaining.size(); ++k)
                {
                    m_tab_animated_offset_x[remaining[k].identifier] += shift;
                }
                return true;
            }
            static_cast<void>(m_model.activate_session(index));
            m_cursor_blink.reset();
            return true;
        }
    }
    if (layout.terminal_content_bounds.contains(point_x, point_y))
    {
        const float line_height = m_cached_line_height > 0.0F
                                      ? m_cached_line_height
                                      : std::max(16.0F * layout.dpi_scale, 12.0F * layout.dpi_scale);
        const float char_width = m_cached_char_width > 0.0F
                                     ? m_cached_char_width
                                     : std::max(8.0F * layout.dpi_scale, 1.0F);
        const float padding_x = 10.0F * layout.dpi_scale;
        const float content_top_padding = 5.0F * layout.dpi_scale;
        const float local_y = point_y - (layout.terminal_content_bounds.y + content_top_padding);
        const int row = static_cast<int>(std::floor(local_y / line_height));
        const std::size_t offset = std::min(m_model.get_scroll_offset(), m_last_total_rows);
        const std::size_t end = m_last_total_rows > offset ? m_last_total_rows - offset : 0;
        const std::size_t start = end > m_last_visible_rows ? end - m_last_visible_rows : 0;
        const std::size_t line_idx = start + std::clamp(row, 0, static_cast<int>(m_last_visible_rows > 0 ? m_last_visible_rows - 1 : 0));

        const float local_x = point_x - (layout.terminal_content_bounds.x + padding_x);
        const int col = static_cast<int>(std::floor(std::max(local_x, 0.0F) / char_width));
        const std::size_t col_idx = static_cast<std::size_t>(std::max(0, col));
        const Terminal::TerminalSession* session = m_model.get_active_session();
        const bool is_cli_app = (session && session->is_in_alternate_screen()) || m_model.is_mouse_tracking_active();

        if (is_cli_app && !shift_held)
        {
            m_model.clear_selection();
            m_selecting_text = false;
            if (m_model.is_mouse_tracking_active())
            {
                const std::size_t visual_col = col_idx + 1;
                const std::size_t visual_row = static_cast<std::size_t>(std::clamp(row, 0, static_cast<int>(m_last_visible_rows > 0 ? m_last_visible_rows - 1 : 0))) + 1;
                m_cli_mouse_down = true;
                m_last_cli_mouse_col = visual_col;
                m_last_cli_mouse_row = visual_row;
                return m_model.send_mouse_button(
                    Terminal::TerminalSession::MouseButton::Left,
                    Terminal::TerminalSession::MouseAction::Press,
                    visual_col, visual_row);
            }
            return true;
        }

        if (click_count == 2)
        {
            m_model.select_word(line_idx, col_idx);
            m_selecting_text = false;
        }
        else if (click_count >= 3)
        {
            m_model.select_line(line_idx);
            m_selecting_text = false;
        }
        else
        {
            m_model.start_selection(line_idx, col_idx);
            m_selecting_text = true;
        }
        return true;
    }
    m_model.clear_selection();
    return true;
}

bool TerminalPanel::handle_pointer_move(
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_x,
    float point_y) noexcept
{
    const bool resize_hover = m_resize_model.set_hovered(
        is_resizing() || is_resize_handle_point(layout, point_x, point_y));

    if (m_model.is_mouse_tracking_active() && layout.terminal_content_bounds.contains(point_x, point_y))
    {
        const float line_height = m_cached_line_height > 0.0F
                                      ? m_cached_line_height
                                      : std::max(16.0F * layout.dpi_scale, 12.0F * layout.dpi_scale);
        const float char_width = m_cached_char_width > 0.0F
                                     ? m_cached_char_width
                                     : std::max(8.0F * layout.dpi_scale, 1.0F);
        const float padding_x = 10.0F * layout.dpi_scale;
        const float content_top_padding = 5.0F * layout.dpi_scale;
        const float local_y = point_y - (layout.terminal_content_bounds.y + content_top_padding);
        const int row = static_cast<int>(std::floor(local_y / line_height));
        const float local_x = point_x - (layout.terminal_content_bounds.x + padding_x);
        const int col = static_cast<int>(std::floor(std::max(local_x, 0.0F) / char_width));
        const std::size_t visual_col = static_cast<std::size_t>(std::max(0, col)) + 1;
        const std::size_t visual_row = static_cast<std::size_t>(std::clamp(row, 0, static_cast<int>(m_last_visible_rows > 0 ? m_last_visible_rows - 1 : 0))) + 1;

        if (visual_col != m_last_cli_mouse_col || visual_row != m_last_cli_mouse_row)
        {
            m_last_cli_mouse_col = visual_col;
            m_last_cli_mouse_row = visual_row;
            if (m_model.send_mouse_motion(visual_col, visual_row, false))
            {
                return true;
            }
        }
    }

    return resize_hover;
}

bool TerminalPanel::handle_pointer_drag(
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_x,
    float point_y) noexcept
{
    if (m_cli_mouse_down)
    {
        const float line_height = m_cached_line_height > 0.0F
                                      ? m_cached_line_height
                                      : std::max(16.0F * layout.dpi_scale, 12.0F * layout.dpi_scale);
        const float char_width = m_cached_char_width > 0.0F
                                     ? m_cached_char_width
                                     : std::max(8.0F * layout.dpi_scale, 1.0F);
        const float padding_x = 10.0F * layout.dpi_scale;
        const float content_top_padding = 5.0F * layout.dpi_scale;
        const float local_y = point_y - (layout.terminal_content_bounds.y + content_top_padding);
        const int row = static_cast<int>(std::floor(local_y / line_height));
        const float local_x = point_x - (layout.terminal_content_bounds.x + padding_x);
        const int col = static_cast<int>(std::floor(std::max(local_x, 0.0F) / char_width));
        const std::size_t visual_col = static_cast<std::size_t>(std::max(0, col)) + 1;
        const std::size_t visual_row = static_cast<std::size_t>(std::clamp(row, 0, static_cast<int>(m_last_visible_rows > 0 ? m_last_visible_rows - 1 : 0))) + 1;

        if (visual_col != m_last_cli_mouse_col || visual_row != m_last_cli_mouse_row)
        {
            m_last_cli_mouse_col = visual_col;
            m_last_cli_mouse_row = visual_row;
            return m_model.send_mouse_motion(visual_col, visual_row, true, Terminal::TerminalSession::MouseButton::Left);
        }
        return true;
    }

    if (m_selecting_text)
    {
        const float line_height = m_cached_line_height > 0.0F
                                      ? m_cached_line_height
                                      : std::max(16.0F * layout.dpi_scale, 12.0F * layout.dpi_scale);
        const float char_width = m_cached_char_width > 0.0F
                                     ? m_cached_char_width
                                     : std::max(8.0F * layout.dpi_scale, 1.0F);
        const float padding_x = 10.0F * layout.dpi_scale;
        const float content_top_padding = 5.0F * layout.dpi_scale;
        const float local_y = point_y - (layout.terminal_content_bounds.y + content_top_padding);
        const int row = static_cast<int>(std::floor(local_y / line_height));
        const std::size_t offset = std::min(m_model.get_scroll_offset(), m_last_total_rows);
        const std::size_t end = m_last_total_rows > offset ? m_last_total_rows - offset : 0;
        const std::size_t start = end > m_last_visible_rows ? end - m_last_visible_rows : 0;
        const std::size_t line_idx = start + std::clamp(row, 0, static_cast<int>(m_last_visible_rows > 0 ? m_last_visible_rows - 1 : 0));

        const float local_x = point_x - (layout.terminal_content_bounds.x + padding_x);
        const int col = static_cast<int>(std::floor(std::max(local_x, 0.0F) / char_width));
        const std::size_t col_idx = static_cast<std::size_t>(std::max(0, col));

        m_model.update_selection(line_idx, col_idx);
        return true;
    }

    if (is_resizing())
    {
        return handle_pointer_drag(layout, point_y);
    }

    return false;
}

bool TerminalPanel::handle_pointer_drag(
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_y) noexcept
{
    return m_resize_model.resize_from_pointer(
        point_y,
        layout.tab_bar_bounds.bottom(),
        layout.status_bar_bounds.y,
        layout.dpi_scale);
}

bool TerminalPanel::handle_pointer_release() noexcept
{
    if (m_cli_mouse_down)
    {
        m_cli_mouse_down = false;
        static_cast<void>(m_model.send_mouse_button(
            Terminal::TerminalSession::MouseButton::Left,
            Terminal::TerminalSession::MouseAction::Release,
            m_last_cli_mouse_col, m_last_cli_mouse_row));
    }
    if (m_selecting_text && m_model.has_selection() && m_copy_callback)
    {
        m_copy_callback(m_model.get_selected_text());
    }
    m_selecting_text = false;
    return m_resize_model.end_resize();
}

bool TerminalPanel::handle_text_input(std::string_view text) {
  m_model.clear_selection();
  const bool changed = m_model.send_text(text);
  if (changed) {
    m_force_horizontal_scroll_to_cursor = true;
    m_cursor_blink.reset();
  }
  return changed;
}
bool TerminalPanel::handle_key(Terminal::TerminalInputKey key) {
  m_model.clear_selection();
  const bool changed = m_model.send_key(key);
  if (changed) {
    m_force_horizontal_scroll_to_cursor = true;
    m_cursor_blink.reset();
  }
  return changed;
}
bool TerminalPanel::handle_control(char letter) {
  if ((letter == 'c' || letter == 'C') && m_model.has_selection()) {
    if (m_copy_callback) {
      m_copy_callback(m_model.get_selected_text());
    }
    m_model.clear_selection();
    return true;
  }
  const bool changed = m_model.send_control(letter);
  if (changed) {
    m_cursor_blink.reset();
    m_force_horizontal_scroll_to_cursor = true;
  }
  return changed;
}

bool TerminalPanel::handle_scroll(
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_x,
    float point_y,
    std::ptrdiff_t line_delta,
    bool horizontal) noexcept
{
    if (horizontal)
    {
        std::ptrdiff_t new_offset = static_cast<std::ptrdiff_t>(m_horizontal_scroll_offset) + line_delta;
        if (new_offset < 0) new_offset = 0;
        if (m_horizontal_scroll_offset != static_cast<std::size_t>(new_offset))
        {
            m_horizontal_scroll_offset = static_cast<std::size_t>(new_offset);
            return true;
        }
        return false;
    }

    const float line_height = std::max(16.0F * layout.dpi_scale, 12.0F * layout.dpi_scale);
    const float padding_x = 10.0F * layout.dpi_scale;
    const float content_top_padding = 5.0F * layout.dpi_scale;
    const float local_y = point_y - (layout.terminal_content_bounds.y + content_top_padding);
    const int row = static_cast<int>(std::floor(local_y / line_height));
    const float local_x = point_x - (layout.terminal_content_bounds.x + padding_x);
    const float glyph_w = std::max(8.0F * layout.dpi_scale, 1.0F);
    const int col = static_cast<int>(std::round(local_x / glyph_w));
    const std::size_t visual_col = std::max(0, col) + 1;
    const std::size_t visual_row = static_cast<std::size_t>(std::clamp(row, 0, static_cast<int>(m_last_visible_rows > 0 ? m_last_visible_rows - 1 : 0))) + 1;

    const std::size_t maximum_offset = m_last_total_rows > m_last_visible_rows
        ? m_last_total_rows - m_last_visible_rows
        : 0;
    return m_model.scroll(line_delta, maximum_offset, visual_col, visual_row);
}

bool TerminalPanel::handle_scroll(std::ptrdiff_t line_delta, bool horizontal) noexcept
{
    if (horizontal)
    {
        std::ptrdiff_t new_offset = static_cast<std::ptrdiff_t>(m_horizontal_scroll_offset) + line_delta;
        if (new_offset < 0) new_offset = 0;
        if (m_horizontal_scroll_offset != static_cast<std::size_t>(new_offset))
        {
            m_horizontal_scroll_offset = static_cast<std::size_t>(new_offset);
            return true;
        }
        return false;
    }

    const std::size_t maximum_offset = m_last_total_rows > m_last_visible_rows
        ? m_last_total_rows - m_last_visible_rows
        : 0;
    return m_model.scroll(line_delta, maximum_offset);
}
bool TerminalPanel::poll()
{
    const bool changed = m_model.poll();
    if (changed)
    {
        // New output should wake the cursor so it is visible right after
        // activity.
        m_cursor_blink.reset();
        m_force_horizontal_scroll_to_cursor = true;
    }
    if (!m_model.is_visible())
    {
        static_cast<void>(m_resize_model.end_resize());
        static_cast<void>(m_resize_model.set_hovered(false));
    }
    return changed;
}

bool TerminalPanel::tick_animations() noexcept
{
    bool needs_redraw = m_model.is_focused() && m_cursor_blink.tick();
    bool animating = false;
    for (auto& [id, offset_x] : m_tab_animated_offset_x)
    {
        if (std::abs(offset_x) > 0.5F)
        {
            offset_x += (0.0F - offset_x) * 0.3F;
            animating = true;
        }
        else
        {
            offset_x = 0.0F;
        }
    }
    return needs_redraw || animating;
}
void TerminalPanel::shutdown() noexcept
{
    m_model.shutdown();
    m_resize_model.reset();
    m_last_resize_click_time = 0;
}
bool TerminalPanel::is_visible() const noexcept
{
    return m_model.is_visible();
}

void TerminalPanel::set_working_directory(
    const std::filesystem::path& directory) noexcept
{
    m_working_directory = directory;
}
bool TerminalPanel::is_focused() const noexcept { return m_model.is_focused(); }
bool TerminalPanel::is_resizing() const noexcept { return m_resize_model.is_resizing(); }
bool TerminalPanel::is_maximized() const noexcept { return m_resize_model.is_maximized(); }
float TerminalPanel::get_height() const noexcept { return m_resize_model.get_height(); }
void TerminalPanel::set_focused(bool focused) noexcept
{
    m_model.set_focused(focused);
    m_cursor_blink.reset();
}

bool TerminalPanel::contains(
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_x,
    float point_y) const noexcept
{
    return is_visible() && layout.terminal_panel_bounds.contains(point_x, point_y);
}

bool TerminalPanel::is_resize_handle_point(
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_x,
    float point_y) const noexcept
{
    return is_visible() && resize_handle_bounds(layout).contains(point_x, point_y);
}

bool TerminalPanel::is_interactive_point(
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_x,
    float point_y) const noexcept
{
    if (!is_visible())
    {
        return false;
    }
    const std::span<const Terminal::TerminalSessionEntry> sessions = m_model.get_sessions();
    for (std::size_t index = 0; index < sessions.size(); ++index)
    {
        if (session_tab_bounds(layout, index).contains(point_x, point_y))
        {
            return true;
        }
    }
    if (add_button_bounds(layout).contains(point_x, point_y) ||
        close_button_bounds(layout).contains(point_x, point_y))
    {
        return true;
    }
    return false;
}

void TerminalPanel::render(
    const StudioWorkspaceRenderer& surface,
    Drawable drawable,
    const UI::Editor::StudioEditorLayoutResult& layout)
{
    if (!is_visible())
    {
        return;
    }
    surface.fill_rectangle(drawable, layout.terminal_panel_bounds, surface.m_pixels.editor_background);
    surface.fill_rectangle(drawable, layout.terminal_header_bounds, surface.m_pixels.tab_background);
    surface.draw_line(drawable,
        round_to_int(layout.terminal_header_bounds.x), round_to_int(layout.terminal_header_bounds.bottom()) - 1,
        round_to_int(layout.terminal_header_bounds.right()), round_to_int(layout.terminal_header_bounds.bottom()) - 1,
        surface.m_pixels.border);
    surface.draw_line(drawable,
        round_to_int(layout.terminal_panel_bounds.x), round_to_int(layout.terminal_panel_bounds.y),
        round_to_int(layout.terminal_panel_bounds.right()), round_to_int(layout.terminal_panel_bounds.y),
        surface.m_pixels.border);
    if (m_resize_model.is_hovered() || m_resize_model.is_resizing())
    {
        surface.fill_rectangle(drawable,
            UI::Rect{layout.terminal_panel_bounds.x,
                layout.terminal_panel_bounds.y - 1.0F * surface.m_dpi_scale,
                layout.terminal_panel_bounds.width,
                2.0F * surface.m_dpi_scale},
            surface.m_pixels.accent);
    }
    if (layout.terminal_panel_bounds.is_empty())
    {
        return;
    }

    const float scale = surface.m_dpi_scale;
    const float bar_h = std::max(2.0F * scale, 2.0F);

    // Draw Channel Switcher Tabs: Terminal, Output
    const UI::Rect term_tab = terminal_channel_tab_bounds(layout);
    const UI::Rect out_tab = output_channel_tab_bounds(layout);

    if (m_active_channel == PanelChannel::Terminal)
    {
        surface.fill_rectangle(drawable, UI::Rect{term_tab.x, term_tab.y + 1.0F, term_tab.width, term_tab.height - 1.0F}, surface.m_pixels.tab_active_background);
        surface.fill_rectangle(drawable, UI::Rect{term_tab.x, term_tab.y, term_tab.width, bar_h}, surface.m_pixels.accent);
        if (term_tab.x > layout.terminal_header_bounds.x)
        {
            surface.draw_line(drawable, round_to_int(term_tab.x), round_to_int(term_tab.y), round_to_int(term_tab.x), round_to_int(term_tab.bottom()) - 1, surface.m_pixels.border);
        }
        surface.draw_line(drawable, round_to_int(term_tab.right()), round_to_int(term_tab.y), round_to_int(term_tab.right()), round_to_int(term_tab.bottom()) - 1, surface.m_pixels.border);
        surface.draw_line(drawable, round_to_int(term_tab.x) + 1, round_to_int(term_tab.bottom()) - 1, round_to_int(term_tab.right()) - 1, round_to_int(term_tab.bottom()) - 1, surface.m_pixels.tab_active_background);
    }
    else
    {
        surface.draw_line(drawable, round_to_int(term_tab.right()), round_to_int(term_tab.y + 4.0F * scale), round_to_int(term_tab.right()), round_to_int(term_tab.bottom() - 5.0F * scale), surface.m_pixels.border);
    }
    const int term_text_w = surface.m_small_font->getTextWidth("Terminal");
    const float term_text_x = term_tab.x + (term_tab.width - static_cast<float>(term_text_w)) * 0.5F;
    surface.draw_text(drawable, *surface.m_small_font, "Terminal",
        term_text_x,
        term_tab.y + term_tab.height * 0.5F,
        (m_active_channel == PanelChannel::Terminal) ? surface.m_text.primary : surface.m_text.muted);

    if (m_active_channel == PanelChannel::Output)
    {
        surface.fill_rectangle(drawable, UI::Rect{out_tab.x, out_tab.y + 1.0F, out_tab.width, out_tab.height - 1.0F}, surface.m_pixels.tab_active_background);
        surface.fill_rectangle(drawable, UI::Rect{out_tab.x, out_tab.y, out_tab.width, bar_h}, surface.m_pixels.accent);
        surface.draw_line(drawable, round_to_int(out_tab.x), round_to_int(out_tab.y), round_to_int(out_tab.x), round_to_int(out_tab.bottom()) - 1, surface.m_pixels.border);
        surface.draw_line(drawable, round_to_int(out_tab.right()), round_to_int(out_tab.y), round_to_int(out_tab.right()), round_to_int(out_tab.bottom()) - 1, surface.m_pixels.border);
        surface.draw_line(drawable, round_to_int(out_tab.x) + 1, round_to_int(out_tab.bottom()) - 1, round_to_int(out_tab.right()) - 1, round_to_int(out_tab.bottom()) - 1, surface.m_pixels.tab_active_background);
    }
    else
    {
        surface.draw_line(drawable, round_to_int(out_tab.right()), round_to_int(out_tab.y + 4.0F * scale), round_to_int(out_tab.right()), round_to_int(out_tab.bottom() - 5.0F * scale), surface.m_pixels.border);
    }
    const int out_text_w = surface.m_small_font->getTextWidth("Output");
    const float out_text_x = out_tab.x + (out_tab.width - static_cast<float>(out_text_w)) * 0.5F;
    surface.draw_text(drawable, *surface.m_small_font, "Output",
        out_text_x,
        out_tab.y + out_tab.height * 0.5F,
        (m_active_channel == PanelChannel::Output) ? surface.m_text.primary : surface.m_text.muted);

    if (m_active_channel == PanelChannel::Output)
    {
        const UI::Rect clear_btn = clear_output_button_bounds(layout);
        const int clear_w = surface.m_small_font->getTextWidth("Clear");
        surface.draw_text(drawable, *surface.m_small_font, "Clear",
            clear_btn.x + (clear_btn.width - static_cast<float>(clear_w)) * 0.5F,
            clear_btn.y + clear_btn.height * 0.5F,
            surface.m_text.muted);

        const auto output_lines = Services::Output::OutputLogManager::instance().get_lines(Services::Output::OutputCategory::Build);
        const float line_h = 16.0F * surface.m_dpi_scale;
        float cur_y = layout.terminal_content_bounds.y + 6.0F * surface.m_dpi_scale + line_h * 0.5F;
        for (std::size_t i = 0; i < output_lines.size(); ++i)
        {
            if (cur_y > layout.terminal_content_bounds.bottom() + line_h) break;
            const std::string& l = output_lines[i];
            surface.draw_text(drawable, *surface.m_editor_font, l,
                layout.terminal_content_bounds.x + 12.0F * surface.m_dpi_scale, cur_y, surface.m_text.primary);
            cur_y += line_h;
        }
        return;
    }

    // Divider between channel switcher and session tabs
    surface.draw_line(drawable,
        round_to_int(layout.terminal_header_bounds.x + 150.0F * scale),
        round_to_int(layout.terminal_header_bounds.y + 5.0F * scale),
        round_to_int(layout.terminal_header_bounds.x + 150.0F * scale),
        round_to_int(layout.terminal_header_bounds.bottom() - 6.0F * scale),
        surface.m_pixels.border);

    const std::span<const Terminal::TerminalSessionEntry> sessions = m_model.get_sessions();
    const std::optional<std::size_t> active_index = m_model.get_active_index();
    for (std::size_t index = 0; index < sessions.size(); ++index)
    {
        UI::Rect tab = session_tab_bounds(layout, index);
        const std::size_t id = sessions[index].identifier;
        if (m_tab_animated_offset_x.contains(id))
        {
            tab.x += m_tab_animated_offset_x[id];
        }

        const bool active = active_index && *active_index == index;
        if (active)
        {
            surface.fill_rectangle(drawable, UI::Rect{tab.x, tab.y + 1.0F, tab.width, tab.height - 1.0F}, surface.m_pixels.tab_active_background);
            surface.fill_rectangle(drawable, UI::Rect{tab.x, tab.y, tab.width, bar_h}, surface.m_pixels.accent);
            surface.draw_line(drawable, round_to_int(tab.x), round_to_int(tab.y), round_to_int(tab.x), round_to_int(tab.bottom()) - 1, surface.m_pixels.border);
            surface.draw_line(drawable, round_to_int(tab.right()), round_to_int(tab.y), round_to_int(tab.right()), round_to_int(tab.bottom()) - 1, surface.m_pixels.border);
            surface.draw_line(drawable, round_to_int(tab.x) + 1, round_to_int(tab.bottom()) - 1, round_to_int(tab.right()) - 1, round_to_int(tab.bottom()) - 1, surface.m_pixels.tab_active_background);

            // Draw Close icon on active session tab
            const float close_icon_x = tab.right() - 14.0F * scale;
            surface.draw_svg_icon(drawable, "Assets/icons/diagnostic-error.svg",
                round_to_int(close_icon_x),
                round_to_int(tab.y + tab.height * 0.5F),
                std::max(round_to_int(10.0F * scale), 9),
                surface.m_palette.text_muted, surface.m_palette.tab_active_background);
        }
        else
        {
            surface.draw_line(drawable, round_to_int(tab.right()), round_to_int(tab.y + 4.0F * scale), round_to_int(tab.right()), round_to_int(tab.bottom() - 5.0F * scale), surface.m_pixels.border);
        }
        surface.draw_svg_icon(drawable, "Assets/icons/vscode-codicons/icons/terminal.svg",
            round_to_int(tab.x + 12.0F * surface.m_dpi_scale),
            round_to_int(tab.y + tab.height * 0.5F),
            std::max(round_to_int(13.0F * surface.m_dpi_scale), 10),
            active ? surface.m_palette.text_primary : surface.m_palette.text_muted,
            active ? surface.m_palette.tab_active_background : surface.m_palette.tab_background);
        surface.draw_text(drawable, *surface.m_small_font, sessions[index].title,
            tab.x + 23.0F * surface.m_dpi_scale, tab.y + tab.height * 0.5F,
            active ? surface.m_text.primary : surface.m_text.muted);
    }

    if (m_tab_animated_offset_x.size() > sessions.size() + 8) {
        std::unordered_set<std::size_t> active_ids;
        for (const auto &s : sessions) {
            active_ids.insert(s.identifier);
        }
        std::erase_if(m_tab_animated_offset_x, [&](const auto &item) { return !active_ids.contains(item.first); });
    }

    const UI::Rect add = add_button_bounds(layout);
    surface.draw_line(drawable, round_to_int(add.x + add.width * 0.5F),
                      round_to_int(add.y + 8.0F * surface.m_dpi_scale),
                      round_to_int(add.x + add.width * 0.5F),
                      round_to_int(add.bottom() - 8.0F * surface.m_dpi_scale),
                      surface.m_pixels.text_muted);
    surface.draw_line(
        drawable, round_to_int(add.x + 6.0F * surface.m_dpi_scale),
        round_to_int(add.y + add.height * 0.5F),
        round_to_int(add.right() - 6.0F * surface.m_dpi_scale),
        round_to_int(add.y + add.height * 0.5F), surface.m_pixels.text_muted);

    const Terminal::TerminalSession* session = m_model.get_active_session();
    if (session == nullptr || surface.m_editor_font == nullptr || surface.m_small_font == nullptr)
    {
        return;
    }
    const float right_space_start = add.right() + 16.0F * scale;
    const float available_shell_width =
        layout.terminal_header_bounds.right() - right_space_start - 16.0F * scale;

    if (sessions.size() <= 4 && available_shell_width >= 80.0F * scale)
    {
        std::string shell_label = "Local  " + session->get_shell_path().string();
        int label_width = surface.m_small_font->getTextWidth(shell_label);
        if (static_cast<float>(label_width) > available_shell_width)
        {
            shell_label = "Local  " + session->get_shell_path().filename().string();
            label_width = surface.m_small_font->getTextWidth(shell_label);
        }
        if (static_cast<float>(label_width) <= available_shell_width)
        {
            surface.draw_text(drawable, *surface.m_small_font, shell_label,
                layout.terminal_header_bounds.right() - 16.0F * scale - static_cast<float>(label_width),
                layout.terminal_header_bounds.y + layout.terminal_header_bounds.height * 0.5F,
                session->is_running() ? surface.m_text.success : surface.m_text.muted);
        }
    }
    const float padding_x = 10.0F * surface.m_dpi_scale;
    const float line_height = std::max(
        static_cast<float>(surface.m_editor_font->getHeight()) + 2.0F * surface.m_dpi_scale,
        12.0F * surface.m_dpi_scale);
    const float content_top_padding = 5.0F * surface.m_dpi_scale;
    const float content_bottom_padding = 8.0F * surface.m_dpi_scale;
    const float usable_content_height = std::max(
        layout.terminal_content_bounds.height - (content_top_padding + content_bottom_padding), 0.0F);
    const std::size_t visible_rows = usable_content_height > 0.0F
        ? std::max<std::size_t>(
            static_cast<std::size_t>(std::floor(usable_content_height / line_height)), 1)
        : 0;
    const int glyph_width = std::max(surface.m_editor_font->getTextWidth("M"), 1);
    m_cached_char_width = static_cast<float>(glyph_width);
    m_cached_line_height = line_height;
    const std::size_t visible_columns = static_cast<std::size_t>(std::max(
        (layout.terminal_content_bounds.width - 22.0F * surface.m_dpi_scale) /
            static_cast<float>(glyph_width),
        1.0F));
    m_model.resize(visible_columns, std::max<std::size_t>(visible_rows, 1));
    if (visible_rows == 0)
    {
        return;
    }
    const std::span<const std::string> lines = session->get_lines();
    std::size_t max_line_length = 0;
    for (const auto& line : lines)
    {
        max_line_length = std::max(max_line_length, line.size());
    }

    const std::size_t maximum_offset = lines.size() > visible_rows
        ? lines.size() - visible_rows
        : 0;
    const std::size_t offset = std::min(m_model.get_scroll_offset(), maximum_offset);
    m_last_total_rows = lines.size();
    m_last_visible_rows = visible_rows;

    // Auto-scroll horizontally to keep the cursor visible on the
    // active (bottom) line while the user is typing.
    if (m_force_horizontal_scroll_to_cursor && is_focused() && offset == 0 && !lines.empty())
    {
        const std::size_t last_line_len = lines.back().size();
        if (last_line_len > m_horizontal_scroll_offset + visible_columns)
        {
            m_horizontal_scroll_offset = last_line_len - visible_columns;
        }
        else if (m_horizontal_scroll_offset > 0 && last_line_len <= visible_columns)
        {
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
    const float first_center_y = layout.terminal_content_bounds.y + content_top_padding +
        line_height * 0.5F;
    float center_y = first_center_y;
    const Terminal::TerminalSelection& selection = m_model.get_selection();
    const bool has_selection = m_model.has_selection();
    const UI::Rect terminal_clip_rect = layout.terminal_content_bounds;

    for (std::size_t index = start; index < end; ++index)
    {
        if (center_y + line_height * 0.5F > layout.terminal_content_bounds.bottom())
        {
            break;
        }
        const std::string& line = lines[index];
        const std::size_t len = line.size();

        const auto spans = session->get_line_spans(index);
        float cur_x = layout.terminal_content_bounds.x + padding_x;

        if (spans.empty())
        {
            if (!line.empty())
            {
                surface.draw_text(drawable, *surface.m_editor_font, line,
                    cur_x, center_y, surface.m_text.primary,
                    &terminal_clip_rect);
            }
        }
        else
        {
            for (const auto& span : spans)
            {
                if (span.text.empty())
                {
                    continue;
                }
                const int span_w = surface.m_editor_font->getTextWidth(span.text);
                if (span_w <= 0)
                {
                    continue;
                }

                if (!span.attributes.background.is_default || span.attributes.inverse)
                {
                    UI::Theme::Color bg_theme;
                    if (span.attributes.inverse)
                    {
                        bg_theme = span.attributes.foreground.is_default
                            ? surface.m_palette.text_primary
                            : UI::Theme::Color{span.attributes.foreground.r, span.attributes.foreground.g, span.attributes.foreground.b, 255};
                    }
                    else
                    {
                        bg_theme = UI::Theme::Color{span.attributes.background.r, span.attributes.background.g, span.attributes.background.b, 255};
                    }
                    const unsigned long bg_pixel = surface.allocate_color(bg_theme);
                    surface.fill_rectangle(
                        drawable,
                        UI::Rect{cur_x, center_y - line_height * 0.5F, static_cast<float>(span_w), line_height},
                        bg_pixel);
                }

                UI::Theme::Color fg_theme;
                if (span.attributes.inverse)
                {
                    fg_theme = span.attributes.background.is_default
                        ? surface.m_palette.editor_background
                        : UI::Theme::Color{span.attributes.background.r, span.attributes.background.g, span.attributes.background.b, 255};
                }
                else if (span.attributes.foreground.is_default)
                {
                    fg_theme = surface.m_palette.text_primary;
                }
                else
                {
                    fg_theme = UI::Theme::Color{span.attributes.foreground.r, span.attributes.foreground.g, span.attributes.foreground.b, 255};
                }

                if (span.attributes.bold)
                {
                    fg_theme.red = static_cast<uint8_t>(std::min<int>(fg_theme.red + 40, 255));
                    fg_theme.green = static_cast<uint8_t>(std::min<int>(fg_theme.green + 40, 255));
                    fg_theme.blue = static_cast<uint8_t>(std::min<int>(fg_theme.blue + 40, 255));
                }
                if (span.attributes.dim)
                {
                    fg_theme.red = static_cast<uint8_t>(fg_theme.red * 0.65F);
                    fg_theme.green = static_cast<uint8_t>(fg_theme.green * 0.65F);
                    fg_theme.blue = static_cast<uint8_t>(fg_theme.blue * 0.65F);
                }

                if (!span.attributes.hidden)
                {
                    surface.draw_text(drawable, *surface.m_editor_font, span.text,
                        cur_x, center_y, fg_theme, &terminal_clip_rect);
                }

                if (span.attributes.underline)
                {
                    const int ul_y = round_to_int(center_y + line_height * 0.38F);
                    const unsigned long fg_pixel = surface.allocate_color(fg_theme);
                    surface.draw_line(drawable, round_to_int(cur_x), ul_y,
                        round_to_int(cur_x + span_w), ul_y, fg_pixel);
                }

                cur_x += static_cast<float>(span_w);
            }
        }

        if (has_selection && !session->is_in_alternate_screen() && !m_model.is_mouse_tracking_active() && selection.intersects_line(index))
        {
            const auto [col_start, col_end] = selection.get_line_range(index, len);
            if (col_end > col_start || (col_start == 0 && col_end == 0 && index < selection.normalized_end().line))
            {
                const std::string pre_sel = utf8_substr_columns(line, 0, col_start);
                const std::string sel_str = utf8_substr_columns(line, col_start, col_end - col_start);
                const int sel_x = round_to_int(layout.terminal_content_bounds.x + padding_x) +
                    surface.m_editor_font->getTextWidth(pre_sel);
                int sel_w = std::max(surface.m_editor_font->getTextWidth(sel_str), 0);

                if (index < selection.normalized_end().line)
                {
                    sel_w += round_to_int(8.0F * surface.m_dpi_scale);
                }
                sel_w = std::max(sel_w, round_to_int(4.0F * surface.m_dpi_scale));

                const unsigned long sel_color = surface.m_pixels.selection_background != 0
                    ? surface.m_pixels.selection_background
                    : surface.m_pixels.accent;

                surface.fill_rectangle(
                    drawable,
                    UI::Rect{static_cast<float>(sel_x), center_y - line_height * 0.5F, static_cast<float>(sel_w), line_height},
                    sel_color);
            }
        }

        center_y += line_height;
    }
    // Vertical scrollbar.
    if (lines.size() > visible_rows)
    {
        const float track_top = layout.terminal_content_bounds.y + content_top_padding - surface.m_dpi_scale;
        const float track_bottom = layout.terminal_content_bounds.bottom() - content_bottom_padding;
        const float track_height = std::max(track_bottom - track_top, 1.0F);
        const float thumb_height = std::max(
            track_height * static_cast<float>(visible_rows) / static_cast<float>(lines.size()),
            18.0F * surface.m_dpi_scale);
        const float progress = maximum_offset == 0
            ? 0.0F
            : static_cast<float>(maximum_offset - offset) / static_cast<float>(maximum_offset);
        surface.fill_rectangle(drawable,
            UI::Rect{layout.terminal_content_bounds.right() - 4.0F * surface.m_dpi_scale,
                track_top,
                2.0F * surface.m_dpi_scale,
                track_height},
            surface.m_pixels.tab_background);
        surface.fill_rectangle(drawable,
            UI::Rect{layout.terminal_content_bounds.right() - 4.0F * surface.m_dpi_scale,
                track_top +
                    progress * std::max(track_height - thumb_height, 0.0F),
                2.0F * surface.m_dpi_scale,
                std::min(thumb_height, track_height)},
            surface.m_pixels.text_muted);
    }

    // Blinking cursor.
    const std::size_t cursor_line_idx = session->get_cursor_line();
    const std::size_t cursor_col_idx = session->get_cursor_column();
    if (is_focused() && cursor_line_idx >= start && cursor_line_idx < end && m_cursor_blink.is_visible())
    {
        const std::size_t visual_row = cursor_line_idx - start;
        const float line_center_y = first_center_y + static_cast<float>(visual_row) * line_height;
        const float caret_height = line_height - 2.0F * surface.m_dpi_scale;
        const float caret_width = std::max(2.0F * surface.m_dpi_scale, 1.5F);

        std::string cursor_prefix;
        if (cursor_line_idx < lines.size())
        {
            const std::string& target_line = lines[cursor_line_idx];
            if (cursor_col_idx > 0)
            {
                cursor_prefix = utf8_substr_columns(target_line, 0, cursor_col_idx);
            }
        }

        const int cursor_x = round_to_int(layout.terminal_content_bounds.x + padding_x) +
            surface.m_editor_font->getTextWidth(cursor_prefix);
        const float cursor_y = line_center_y - caret_height * 0.5F;
        surface.fill_rectangle(drawable,
            UI::Rect{static_cast<float>(cursor_x), cursor_y,
                caret_width, caret_height},
            surface.m_pixels.text_primary);
    }
}

UI::Rect TerminalPanel::terminal_channel_tab_bounds(
    const UI::Editor::StudioEditorLayoutResult& layout) const noexcept
{
    const float scale = layout.dpi_scale;
    return UI::Rect{
        layout.terminal_header_bounds.x,
        layout.terminal_header_bounds.y,
        74.0F * scale,
        layout.terminal_header_bounds.height};
}

UI::Rect TerminalPanel::output_channel_tab_bounds(
    const UI::Editor::StudioEditorLayoutResult& layout) const noexcept
{
    const float scale = layout.dpi_scale;
    return UI::Rect{
        layout.terminal_header_bounds.x + 74.0F * scale,
        layout.terminal_header_bounds.y,
        68.0F * scale,
        layout.terminal_header_bounds.height};
}

UI::Rect TerminalPanel::clear_output_button_bounds(
    const UI::Editor::StudioEditorLayoutResult& layout) const noexcept
{
    const float scale = layout.dpi_scale;
    return UI::Rect{
        layout.terminal_header_bounds.right() - 56.0F * scale,
        layout.terminal_header_bounds.y + 4.0F * scale,
        48.0F * scale,
        layout.terminal_header_bounds.height - 8.0F * scale};
}

UI::Rect TerminalPanel::session_tab_bounds(
    const UI::Editor::StudioEditorLayoutResult& layout,
    std::size_t index) const noexcept
{
    const float scale = layout.dpi_scale;
    const float start_x = layout.terminal_header_bounds.x + 152.0F * scale;
    const float tab_width = 108.0F * scale;
    const float x = start_x + static_cast<float>(index) * tab_width;
    return UI::Rect{x, layout.terminal_header_bounds.y, tab_width,
                    layout.terminal_header_bounds.height};
}

UI::Rect TerminalPanel::add_button_bounds(
    const UI::Editor::StudioEditorLayoutResult& layout) const noexcept
{
    const float scale = layout.dpi_scale;
    const float start_x = layout.terminal_header_bounds.x + 152.0F * scale;
    const float tab_width = 108.0F * scale;
    const float x =
        start_x + static_cast<float>(m_model.get_sessions().size()) * tab_width +
        4.0F * scale;
    return UI::Rect{x, layout.terminal_header_bounds.y, 24.0F * scale,
                    layout.terminal_header_bounds.height};
}

UI::Rect TerminalPanel::close_button_bounds(
    const UI::Editor::StudioEditorLayoutResult& layout) const noexcept
{
    const std::optional<std::size_t> active_index = m_model.get_active_index();
    if (!active_index) {
        return UI::Rect{0.0F, 0.0F, 0.0F, 0.0F};
    }
    const std::span<const Terminal::TerminalSessionEntry> sessions =
        m_model.get_sessions();
    if (*active_index >= sessions.size()) {
        return UI::Rect{0.0F, 0.0F, 0.0F, 0.0F};
    }
    const UI::Rect tab = session_tab_bounds(layout, *active_index);
    const float width = 20.0F * layout.dpi_scale;
    return UI::Rect{tab.right() - width - 2.0F * layout.dpi_scale,
                    layout.terminal_header_bounds.y,
                    width, layout.terminal_header_bounds.height};
}

UI::Rect TerminalPanel::resize_handle_bounds(
    const UI::Editor::StudioEditorLayoutResult& layout) const noexcept
{
    const float handle_height = std::max(8.0F * layout.dpi_scale, 6.0F);
    return UI::Rect{
        layout.terminal_panel_bounds.x,
        layout.terminal_panel_bounds.y - handle_height * 0.5F,
        layout.terminal_panel_bounds.width,
        handle_height,
    };
}

} // namespace Zenvra::Platform::X11::Components
