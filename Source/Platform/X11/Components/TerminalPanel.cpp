#include "Platform/X11/Components/TerminalPanel.h"

#include "Platform/X11/Components/StudioWorkspaceRenderer.h"
#include "Utility/Fonts.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace Zenvra::Platform::X11::Components
{

namespace
{

int round_to_int(float value)
{
    return static_cast<int>(std::lround(value));
}

std::filesystem::path current_terminal_directory()
{
    std::error_code error;
    const std::filesystem::path current = std::filesystem::current_path(error);
    return error ? std::filesystem::path{} : current;
}

} // namespace

bool TerminalPanel::toggle()
{
    const bool was_visible = m_model.is_visible();
    const bool changed = m_model.toggle(current_terminal_directory());
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
    Time event_time)
{
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
    if (!is_visible() || !layout.terminal_panel_bounds.contains(point_x, point_y))
    {
        return false;
    }
    m_model.set_focused(true);
    if (close_button_bounds(layout).contains(point_x, point_y))
    {
        return m_model.close_active_session();
    }
    if (add_button_bounds(layout).contains(point_x, point_y))
    {
        return m_model.create_session(current_terminal_directory());
    }
    const std::span<const Terminal::TerminalSessionEntry> sessions = m_model.get_sessions();
    for (std::size_t index = 0; index < sessions.size(); ++index)
    {
        if (session_tab_bounds(layout, index).contains(point_x, point_y))
        {
            static_cast<void>(m_model.activate_session(index));
            return true;
        }
    }
    return true;
}

bool TerminalPanel::handle_pointer_move(
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_x,
    float point_y) noexcept
{
    return m_resize_model.set_hovered(
        is_resizing() || is_resize_handle_point(layout, point_x, point_y));
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
    return m_resize_model.end_resize();
}

bool TerminalPanel::handle_text_input(std::string_view text) { return m_model.send_text(text); }
bool TerminalPanel::handle_key(Terminal::TerminalInputKey key) { return m_model.send_key(key); }
bool TerminalPanel::handle_control(char letter) { return m_model.send_control(letter); }
bool TerminalPanel::handle_scroll(std::ptrdiff_t line_delta) noexcept { return m_model.scroll(line_delta); }
bool TerminalPanel::poll()
{
    const bool changed = m_model.poll();
    if (!m_model.is_visible())
    {
        static_cast<void>(m_resize_model.end_resize());
        static_cast<void>(m_resize_model.set_hovered(false));
    }
    return changed;
}
void TerminalPanel::shutdown() noexcept
{
    m_model.shutdown();
    m_resize_model.reset();
    m_last_resize_click_time = 0;
}
bool TerminalPanel::is_visible() const noexcept { return m_model.is_visible(); }
bool TerminalPanel::is_focused() const noexcept { return m_model.is_focused(); }
bool TerminalPanel::is_resizing() const noexcept { return m_resize_model.is_resizing(); }
bool TerminalPanel::is_maximized() const noexcept { return m_resize_model.is_maximized(); }
float TerminalPanel::get_height() const noexcept { return m_resize_model.get_height(); }
void TerminalPanel::set_focused(bool focused) noexcept { m_model.set_focused(focused); }

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
    const unsigned long splitter_color =
        (m_resize_model.is_hovered() || m_resize_model.is_resizing())
        ? surface.m_pixels.accent
        : surface.m_pixels.border;
    surface.draw_line(drawable,
        round_to_int(layout.terminal_panel_bounds.x), round_to_int(layout.terminal_panel_bounds.y),
        round_to_int(layout.terminal_panel_bounds.right()), round_to_int(layout.terminal_panel_bounds.y),
        splitter_color);
    if (m_resize_model.is_hovered() || m_resize_model.is_resizing())
    {
        surface.fill_rectangle(drawable,
            UI::Rect{layout.terminal_panel_bounds.x,
                layout.terminal_panel_bounds.y - surface.m_dpi_scale,
                layout.terminal_panel_bounds.width,
                std::max(2.0F * surface.m_dpi_scale, 2.0F)},
            surface.m_pixels.accent);
    }
    if (layout.terminal_panel_bounds.is_empty())
    {
        return;
    }
    surface.draw_text(drawable, *surface.m_ui_font, "Terminal",
        layout.terminal_header_bounds.x + 10.0F * surface.m_dpi_scale,
        layout.terminal_header_bounds.y + layout.terminal_header_bounds.height * 0.5F,
        surface.m_text.primary);

    const std::span<const Terminal::TerminalSessionEntry> sessions = m_model.get_sessions();
    const std::optional<std::size_t> active_index = m_model.get_active_index();
    for (std::size_t index = 0; index < sessions.size(); ++index)
    {
        const UI::Rect tab = session_tab_bounds(layout, index);
        const bool active = active_index && *active_index == index;
        if (active)
        {
            surface.fill_rectangle(drawable, tab, surface.m_pixels.tab_active_background);
            surface.fill_rectangle(drawable,
                UI::Rect{tab.x, tab.bottom() - surface.m_dpi_scale, tab.width, surface.m_dpi_scale},
                surface.m_pixels.accent);
        }
        surface.draw_svg_icon(drawable, "Assets/icons/terminal.svg",
            round_to_int(tab.x + 12.0F * surface.m_dpi_scale),
            round_to_int(tab.y + tab.height * 0.5F),
            std::max(round_to_int(13.0F * surface.m_dpi_scale), 10),
            active ? surface.m_palette.text_primary : surface.m_palette.text_muted,
            active ? surface.m_palette.tab_active_background : surface.m_palette.tab_background);
        surface.draw_text(drawable, *surface.m_small_font, sessions[index].title,
            tab.x + 23.0F * surface.m_dpi_scale, tab.y + tab.height * 0.5F,
            active ? surface.m_text.primary : surface.m_text.muted);
    }

    const UI::Rect add = add_button_bounds(layout);
    surface.draw_line(drawable, round_to_int(add.x + add.width * 0.5F), round_to_int(add.y + 7.0F * surface.m_dpi_scale),
        round_to_int(add.x + add.width * 0.5F), round_to_int(add.bottom() - 7.0F * surface.m_dpi_scale), surface.m_pixels.text_muted);
    surface.draw_line(drawable, round_to_int(add.x + 7.0F * surface.m_dpi_scale), round_to_int(add.y + add.height * 0.5F),
        round_to_int(add.right() - 7.0F * surface.m_dpi_scale), round_to_int(add.y + add.height * 0.5F), surface.m_pixels.text_muted);
    const UI::Rect close = close_button_bounds(layout);
    surface.draw_line(drawable, round_to_int(close.x + 8.0F * surface.m_dpi_scale), round_to_int(close.y + 8.0F * surface.m_dpi_scale),
        round_to_int(close.right() - 8.0F * surface.m_dpi_scale), round_to_int(close.bottom() - 8.0F * surface.m_dpi_scale), surface.m_pixels.text_muted);
    surface.draw_line(drawable, round_to_int(close.right() - 8.0F * surface.m_dpi_scale), round_to_int(close.y + 8.0F * surface.m_dpi_scale),
        round_to_int(close.x + 8.0F * surface.m_dpi_scale), round_to_int(close.bottom() - 8.0F * surface.m_dpi_scale), surface.m_pixels.text_muted);

    const Terminal::TerminalSession* session = m_model.get_active_session();
    if (session == nullptr || surface.m_editor_font == nullptr)
    {
        return;
    }
    if (sessions.size() <= 4 &&
        layout.terminal_header_bounds.width >= 600.0F * surface.m_dpi_scale)
    {
        const std::string shell_label = "Local  " + session->get_shell_path().string();
        const int label_width = surface.m_small_font->getTextWidth(shell_label);
        surface.draw_text(drawable, *surface.m_small_font, shell_label,
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
    m_model.resize(visible_columns, std::max<std::size_t>(visible_rows, 1));
    if (visible_rows == 0)
    {
        return;
    }
    const std::span<const std::string> lines = session->get_lines();
    const std::size_t offset = std::min(m_model.get_scroll_offset(), lines.size());
    const std::size_t end = lines.size() - offset;
    const std::size_t start = end > visible_rows ? end - visible_rows : 0;
    const std::size_t displayed_rows = end - start;
    const float first_center_y = layout.terminal_content_bounds.y + content_top_padding +
        line_height * 0.5F;
    float center_y = first_center_y;
    for (std::size_t index = start; index < end; ++index)
    {
        const std::string_view line = lines[index].size() > visible_columns
            ? std::string_view{lines[index]}.substr(0, visible_columns)
            : std::string_view{lines[index]};
        surface.draw_text(drawable, *surface.m_editor_font, line,
            layout.terminal_content_bounds.x + padding_x, center_y, surface.m_text.primary);
        center_y += line_height;
    }
    if (lines.size() > visible_rows)
    {
        const float track_top = layout.terminal_content_bounds.y + content_top_padding - surface.m_dpi_scale;
        const float track_bottom = layout.terminal_content_bounds.bottom() - content_bottom_padding;
        const float track_height = std::max(track_bottom - track_top, 1.0F);
        const float thumb_height = std::max(
            track_height * static_cast<float>(visible_rows) / static_cast<float>(lines.size()),
            18.0F * surface.m_dpi_scale);
        const std::size_t maximum_start = lines.size() - visible_rows;
        const float progress = maximum_start == 0
            ? 0.0F
            : static_cast<float>(start) / static_cast<float>(maximum_start);
        surface.fill_rectangle(drawable,
            UI::Rect{layout.terminal_content_bounds.right() - 4.0F * surface.m_dpi_scale,
                track_top +
                    progress * std::max(track_height - thumb_height, 0.0F),
                2.0F * surface.m_dpi_scale,
                std::min(thumb_height, track_height)},
            surface.m_pixels.text_muted);
    }
    if (is_focused() && offset == 0 && !lines.empty())
    {
        const std::string& last = lines.back();
        const std::string cursor_text = last.substr(0, std::min(last.size(), visible_columns));
        const int cursor_x = round_to_int(layout.terminal_content_bounds.x + padding_x) +
            surface.m_editor_font->getTextWidth(cursor_text);
        const int cursor_y = round_to_int(first_center_y +
            static_cast<float>(displayed_rows - 1) * line_height - line_height * 0.5F);
        surface.fill_rectangle(drawable,
            UI::Rect{static_cast<float>(cursor_x), static_cast<float>(cursor_y),
                std::max(surface.m_dpi_scale, 1.0F), line_height - 2.0F * surface.m_dpi_scale},
            surface.m_pixels.text_primary);
    }
}

UI::Rect TerminalPanel::session_tab_bounds(
    const UI::Editor::StudioEditorLayoutResult& layout,
    std::size_t index) const noexcept
{
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
    const UI::Editor::StudioEditorLayoutResult& layout) const noexcept
{
    const float scale = layout.dpi_scale;
    const float start_x = layout.terminal_header_bounds.x + 72.0F * scale;
    const float available_width = std::max(
        layout.terminal_header_bounds.right() - start_x - 56.0F * scale, 0.0F);
    const float tab_width = std::min(
        112.0F * scale,
        available_width / static_cast<float>(std::max<std::size_t>(m_model.get_sessions().size(), 1)));
    const float x = start_x + static_cast<float>(m_model.get_sessions().size()) * tab_width;
    return UI::Rect{x, layout.terminal_header_bounds.y,
        28.0F * scale, layout.terminal_header_bounds.height};
}

UI::Rect TerminalPanel::close_button_bounds(
    const UI::Editor::StudioEditorLayoutResult& layout) const noexcept
{
    const float width = 28.0F * layout.dpi_scale;
    return UI::Rect{layout.terminal_header_bounds.right() - width,
        layout.terminal_header_bounds.y, width, layout.terminal_header_bounds.height};
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
