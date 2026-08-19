#include "Platform/Cocoa/Components/ToolSidebar.h"
#include "Platform/Cocoa/Components/StudioWorkspaceRenderer.h"
#include "Utility/Fonts.h"
#include "UI/Editor/FileIconModel.h"

#import <Cocoa/Cocoa.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>

namespace Zenvra::Platform::Cocoa::Components
{

namespace { int round_to_int(float v) { return static_cast<int>(std::lround(v)); } }

namespace
{

std::string ellipsize(AntialiasedFont& font, std::string text, int maximum_width)
{
    if (font.getTextWidth(text) <= maximum_width)
    {
        return text;
    }
    constexpr std::string_view suffix = "...";
    while (!text.empty() &&
           font.getTextWidth(text + std::string{suffix}) > maximum_width)
    {
        text.pop_back();
        while (!text.empty() &&
               (static_cast<unsigned char>(text.back()) & 0xC0U) == 0x80U)
        {
            text.pop_back();
        }
        if (!text.empty() && static_cast<unsigned char>(text.back()) >= 0x80U)
        {
            text.pop_back();
        }
    }
    return text + std::string{suffix};
}

} // namespace

bool ToolSidebar::initialize() { return m_model.initialize(); }
bool ToolSidebar::set_workspace_root(const std::filesystem::path& root) {
    m_search_model.set_workspace_root(root);
    m_source_control_model.set_workspace_root(root);
    return m_model.initialize(root);
}
bool ToolSidebar::activate(UI::Editor::SidebarIcon icon) noexcept { return m_model.activate(icon); }
bool ToolSidebar::is_visible() const noexcept { return m_model.is_visible(); }
bool ToolSidebar::is_active(UI::Editor::SidebarIcon icon) const noexcept { return m_model.is_active(icon); }
bool ToolSidebar::is_hovered(UI::Editor::SidebarIcon icon) const noexcept { return m_hovered_icon.has_value() && *m_hovered_icon == icon; }
float ToolSidebar::get_width() const noexcept { return m_width; }
bool ToolSidebar::is_resizing() const noexcept { return m_resizing; }

bool ToolSidebar::contains(
    const UI::Editor::StudioEditorLayoutResult& layout, float px, float py) const noexcept
{
    return is_visible() && layout.tool_sidebar_bounds.contains(px, py);
}

bool ToolSidebar::is_resize_handle_point(
    const UI::Editor::StudioEditorLayoutResult& layout, float px, float py) const noexcept
{
    if (!is_visible()) return false;
    const float handle_x = layout.tool_sidebar_bounds.right() - 3.0F;
    return px >= handle_x && px <= handle_x + 6.0F &&
           py >= layout.tool_sidebar_bounds.y && py < layout.tool_sidebar_bounds.bottom();
}

bool ToolSidebar::handle_pointer_press(
    StudioWorkspaceRenderer& surface,
    const UI::Editor::StudioEditorLayoutResult& layout,
    float px, float py,
    std::optional<std::filesystem::path>& file_to_open,
    std::optional<std::size_t>& target_line,
    std::optional<std::size_t>& target_col)
{
    if (!is_visible() || !contains(layout, px, py)) return false;

    if (m_model.get_active_icon() == UI::Editor::SidebarIcon::Search)
    {
        return handle_search_press(surface, layout, px, py, file_to_open, target_line, target_col);
    }

    if (m_model.get_active_icon() == UI::Editor::SidebarIcon::VersionControl)
    {
        return handle_source_control_press(surface, layout, px, py, file_to_open);
    }

    if (m_model.get_active_icon() == UI::Editor::SidebarIcon::Project)
    {
        const float scale = layout.dpi_scale;
        const float tree_top = layout.tool_sidebar_bounds.y + header_height * scale;
        const auto sticky = get_sticky_items();
        const float sticky_height = static_cast<float>(sticky.size()) * row_height * scale;
        if (py >= tree_top && py < tree_top + sticky_height) {
            std::size_t sticky_index = static_cast<std::size_t>((py - tree_top) / (row_height * scale));
            if (sticky_index < sticky.size()) {
                m_model.set_scroll_offset(sticky[sticky_index]);
                return true;
            }
        }

        // Check explorer header
        if (m_explorer_header.handle_pointer_press(surface, layout, px, py, m_model, file_to_open)) return true;
        // Check tree item click
        if (auto row = row_from_point(layout, py)) {
            const auto& items = m_model.get_project_items();
            if (*row < items.size()) {
                m_drag_source_row = *row;
                m_drag_press_x = px;
                m_drag_press_y = py;
                m_drag_current_x = px;
                m_drag_current_y = py;
                m_is_dragging_item = false;
                m_drag_target_row.reset();

                const auto item = items[*row];
                m_model.set_selected_path(item.path);

                if (item.directory) {
                    static_cast<void>(m_model.activate_project_item(*row));
                } else {
                    file_to_open = item.path;
                }
                return true;
            }
        }
        // Empty state buttons
        if (m_model.get_workspace_root().empty()) {
            if (m_empty_state_open_btn.get_state().hovered) {
                file_to_open = std::filesystem::path("::OPEN_FOLDER::");
                return true;
            }
        }
    }
    return true;
}

bool ToolSidebar::handle_pointer_move(
    const UI::Editor::StudioEditorLayoutResult& layout, float px, float py) noexcept
{
    if (!is_visible()) return false;

    // Pointer left the sidebar: clear hover states
    if (!layout.tool_sidebar_bounds.contains(px, py)) {
        bool changed = m_hovered_row.has_value() || m_hovered_search_row.has_value() ||
                       m_hovered_sc_row.has_value() || m_hovered_sticky_index.has_value() ||
                       m_hovered_icon.has_value() || m_resize_hovered;
        m_hovered_row.reset();
        m_hovered_search_row.reset();
        m_hovered_sc_row.reset();
        m_hovered_sticky_index.reset();
        m_hovered_icon.reset();
        m_resize_hovered = false;
        changed |= m_explorer_header.handle_pointer_move(layout, px, py);
        return changed;
    }

    bool changed = false;
    if (m_model.get_active_icon() == UI::Editor::SidebarIcon::Search)
    {
        changed |= handle_search_move(layout, px, py);
    }
    else if (m_model.get_active_icon() == UI::Editor::SidebarIcon::VersionControl)
    {
        changed |= handle_source_control_move(layout, px, py);
    }
    else if (m_model.get_active_icon() == UI::Editor::SidebarIcon::Project)
    {
        const float scale = layout.dpi_scale;
        const float tree_top = layout.tool_sidebar_bounds.y + header_height * scale;
        const auto sticky = get_sticky_items();
        const float sticky_height = static_cast<float>(sticky.size()) * row_height * scale;
        std::optional<std::size_t> next_sticky_hover;
        if (py >= tree_top && py < tree_top + sticky_height) {
            std::size_t sticky_index = static_cast<std::size_t>((py - tree_top) / (row_height * scale));
            if (sticky_index < sticky.size()) {
                next_sticky_hover = sticky[sticky_index];
            }
        }
        changed |= next_sticky_hover != m_hovered_sticky_index;
        m_hovered_sticky_index = next_sticky_hover;

        auto new_row = row_from_point(layout, py);
        changed |= new_row != m_hovered_row;
        m_hovered_row = new_row;
        changed |= m_explorer_header.handle_pointer_move(layout, px, py);
    }
    else
    {
        changed |= m_hovered_row.has_value() || m_hovered_sticky_index.has_value() || m_hovered_sc_row.has_value();
        m_hovered_row.reset();
        m_hovered_sticky_index.reset();
        m_hovered_sc_row.reset();
    }

    // Check sidebar icon hover
    const std::span<const UI::Editor::SidebarItem> items = UI::Editor::get_studio_sidebar_items();
    std::optional<UI::Editor::SidebarIcon> new_icon;
    if (auto index = UI::Editor::hit_test_studio_sidebar(layout, px, py)) {
        new_icon = items[*index].icon;
    }
    changed |= new_icon != m_hovered_icon;
    m_hovered_icon = new_icon;

    // Track resize handle hover
    const bool next_resize_hovered = is_resize_handle_point(layout, px, py);
    changed |= next_resize_hovered != m_resize_hovered;
    m_resize_hovered = next_resize_hovered;
    return changed;
}

bool ToolSidebar::handle_scroll(
    const UI::Editor::StudioEditorLayoutResult& layout, std::ptrdiff_t delta) noexcept
{
    if (m_model.get_active_icon() == UI::Editor::SidebarIcon::Search)
    {
        return m_search_model.scroll(delta, search_viewport_row_count(layout));
    }
    if (m_model.get_active_icon() == UI::Editor::SidebarIcon::VersionControl)
    {
        return m_source_control_model.scroll(delta, source_control_viewport_row_count(layout));
    }
    return m_model.scroll(delta, viewport_row_count(layout));
}

bool ToolSidebar::handle_pointer_drag(
    const UI::Editor::StudioEditorLayoutResult& layout, float px, float py) noexcept
{
    if (m_resizing) {
        m_width = std::max(120.0F, m_drag_start_width + (px - m_drag_start_x));
        return true;
    }

    if (m_is_selecting_search_text && m_model.get_active_icon() == UI::Editor::SidebarIcon::Search) {
        const float scale = layout.dpi_scale;
        const float char_w = 7.2F * scale;
        if (m_search_model.get_focused_input() == UI::Editor::SearchInputFocus::Search) {
            const float input_top = layout.tool_sidebar_bounds.y + header_height * scale + 8.0F * scale;
            const UI::Rect search_bounds{layout.tool_sidebar_bounds.x + 28.0F * scale, input_top, std::max(layout.tool_sidebar_bounds.width - 36.0F * scale, 0.0F), 26.0F * scale};
            const float text_x = search_bounds.x + 6.0F * scale;
            const std::string_view q = m_search_model.get_search_query();
            std::size_t idx = 0;
            if (px > text_x) {
                idx = static_cast<std::size_t>((px - text_x + char_w * 0.5F) / char_w);
                idx = std::min(idx, q.size());
            }
            m_search_model.update_drag_selection(idx);
            return true;
        } else if (m_search_model.get_focused_input() == UI::Editor::SearchInputFocus::Replace) {
            const float input_top = layout.tool_sidebar_bounds.y + header_height * scale + 8.0F * scale;
            const float replace_top = input_top + 30.0F * scale;
            const UI::Rect replace_bounds{layout.tool_sidebar_bounds.x + 28.0F * scale, replace_top, std::max(layout.tool_sidebar_bounds.width - 64.0F * scale, 0.0F), 26.0F * scale};
            const float text_x = replace_bounds.x + 6.0F * scale;
            const std::string_view q = m_search_model.get_replace_query();
            std::size_t idx = 0;
            if (px > text_x) {
                idx = static_cast<std::size_t>((px - text_x + char_w * 0.5F) / char_w);
                idx = std::min(idx, q.size());
            }
            m_search_model.update_drag_selection(idx);
            return true;
        }
    }

    if (m_is_selecting_sc_text && m_model.get_active_icon() == UI::Editor::SidebarIcon::VersionControl) {
        const float scale = layout.dpi_scale;
        const float char_w = 7.2F * scale;
        const float input_top = layout.tool_sidebar_bounds.y + header_height * scale + 8.0F * scale;
        const UI::Rect msg_bounds{layout.tool_sidebar_bounds.x + 12.0F * scale, input_top, std::max(layout.tool_sidebar_bounds.width - 24.0F * scale, 0.0F), 26.0F * scale};
        const float text_x = msg_bounds.x + 6.0F * scale;
        const std::string& msg = m_source_control_model.get_commit_message();
        std::size_t idx = 0;
        if (px > text_x) {
            idx = static_cast<std::size_t>((px - text_x + char_w * 0.5F) / char_w);
            idx = std::min(idx, msg.size());
        }
        m_source_control_model.set_caret_and_selection(m_source_control_model.get_caret(), idx);
        return true;
    }

    if (m_drag_source_row.has_value()) {
        const float dist = std::hypot(px - m_drag_press_x, py - m_drag_press_y);
        if (dist > 5.0F) {
            m_is_dragging_item = true;
            m_drag_current_x = px;
            m_drag_current_y = py;
            m_drag_target_row = row_from_point(layout, py);
            return true;
        }
    }
    return false;
}

bool ToolSidebar::handle_pointer_release() noexcept
{
    bool was = m_resizing;
    m_resizing = false;
    m_is_selecting_search_text = false;
    m_is_selecting_sc_text = false;

    if (m_is_dragging_item && m_drag_source_row.has_value()) {
        const auto& items = m_model.get_project_items();
        if (*m_drag_source_row < items.size()) {
            const auto source_path = items[*m_drag_source_row].path;
            std::filesystem::path dest_dir;

            if (m_drag_target_row.has_value() && *m_drag_target_row < items.size()) {
                const auto& target_item = items[*m_drag_target_row];
                if (target_item.directory) {
                    dest_dir = target_item.path;
                } else {
                    dest_dir = target_item.path.parent_path();
                }
            } else {
                dest_dir = m_model.get_workspace_root();
            }

            if (!dest_dir.empty() && dest_dir != source_path && dest_dir != source_path.parent_path()) {
                std::filesystem::path out_path;
                if (m_model.move_item(source_path, dest_dir, out_path)) {
                    m_model.set_selected_path(out_path);
                    was = true;
                }
            }
        }
    }

    m_drag_source_row.reset();
    m_drag_target_row.reset();
    m_is_dragging_item = false;
    return was;
}

bool ToolSidebar::tick_animations() noexcept
{
    bool updated = m_search_model.tick();
    updated |= m_source_control_model.tick();
    if (m_model.get_active_icon() == UI::Editor::SidebarIcon::Search &&
        (m_search_model.get_focused_input() != UI::Editor::SearchInputFocus::None || m_search_model.is_searching())) {
        updated = true;
    }
    if (m_model.get_active_icon() == UI::Editor::SidebarIcon::VersionControl && m_source_control_model.is_input_focused()) {
        updated = true;
    }
    return updated;
}

bool ToolSidebar::is_search_focused() const noexcept
{
    return is_visible() &&
           m_model.get_active_icon() == UI::Editor::SidebarIcon::Search &&
           m_search_model.get_focused_input() != UI::Editor::SearchInputFocus::None;
}

bool ToolSidebar::handle_search_text(std::string_view text)
{
    if (!is_search_focused()) return false;
    m_search_model.insert_text(text);
    return true;
}

bool ToolSidebar::handle_search_command(UI::Editor::EditorInputCommand cmd, bool extend)
{
    if (!is_search_focused()) return false;
    switch (cmd) {
    case UI::Editor::EditorInputCommand::DeleteBackward:
        m_search_model.handle_backspace();
        return true;
    case UI::Editor::EditorInputCommand::DeleteForward:
        m_search_model.handle_delete();
        return true;
    case UI::Editor::EditorInputCommand::MoveLeft:
        m_search_model.handle_left(extend);
        return true;
    case UI::Editor::EditorInputCommand::MoveRight:
        m_search_model.handle_right(extend);
        return true;
    case UI::Editor::EditorInputCommand::MoveHome:
        m_search_model.handle_home(extend);
        return true;
    case UI::Editor::EditorInputCommand::MoveEnd:
        m_search_model.handle_end(extend);
        return true;
    case UI::Editor::EditorInputCommand::InsertNewLine:
        if (m_search_model.get_focused_input() == UI::Editor::SearchInputFocus::Search) {
            m_search_model.execute_search();
        } else if (m_search_model.get_focused_input() == UI::Editor::SearchInputFocus::Replace) {
            m_search_model.replace_all();
        }
        return true;
    case UI::Editor::EditorInputCommand::Escape:
        m_search_model.set_focused_input(UI::Editor::SearchInputFocus::None);
        return true;
    default:
        break;
    }
    return false;
}

bool ToolSidebar::handle_search_action(UI::Editor::EditorAction action)
{
    if (!is_search_focused()) return false;
    switch (action) {
    case UI::Editor::EditorAction::SelectAll:
        m_search_model.select_all();
        return true;
    case UI::Editor::EditorAction::Copy:
        if (m_search_model.has_selection()) {
            NSPasteboard* pb = [NSPasteboard generalPasteboard];
            [pb clearContents];
            [pb setString:[NSString stringWithUTF8String:m_search_model.get_selected_text().c_str()] forType:NSPasteboardTypeString];
            return true;
        }
        break;
    case UI::Editor::EditorAction::Cut:
        if (m_search_model.has_selection()) {
            NSPasteboard* pb = [NSPasteboard generalPasteboard];
            [pb clearContents];
            [pb setString:[NSString stringWithUTF8String:m_search_model.get_selected_text().c_str()] forType:NSPasteboardTypeString];
            m_search_model.handle_backspace();
            return true;
        }
        break;
    case UI::Editor::EditorAction::Paste: {
        NSPasteboard* pb = [NSPasteboard generalPasteboard];
        NSString* str = [pb stringForType:NSPasteboardTypeString];
        if (str) {
            m_search_model.insert_text([str UTF8String]);
            return true;
        }
        break;
    }
    default:
        break;
    }
    return false;
}

bool ToolSidebar::is_source_control_focused() const noexcept
{
    return is_visible() &&
           m_model.get_active_icon() == UI::Editor::SidebarIcon::VersionControl &&
           m_source_control_model.is_input_focused();
}

bool ToolSidebar::handle_source_control_text(std::string_view text)
{
    if (!is_source_control_focused()) return false;
    m_source_control_model.insert_text(text);
    return true;
}

bool ToolSidebar::handle_source_control_command(UI::Editor::EditorInputCommand cmd, bool extend)
{
    if (!is_source_control_focused()) return false;
    switch (cmd) {
    case UI::Editor::EditorInputCommand::DeleteBackward:
        m_source_control_model.handle_backspace();
        return true;
    case UI::Editor::EditorInputCommand::DeleteForward:
        m_source_control_model.handle_delete();
        return true;
    case UI::Editor::EditorInputCommand::MoveLeft:
        m_source_control_model.handle_left(extend);
        return true;
    case UI::Editor::EditorInputCommand::MoveRight:
        m_source_control_model.handle_right(extend);
        return true;
    case UI::Editor::EditorInputCommand::MoveHome:
        m_source_control_model.handle_home(extend);
        return true;
    case UI::Editor::EditorInputCommand::MoveEnd:
        m_source_control_model.handle_end(extend);
        return true;
    case UI::Editor::EditorInputCommand::InsertNewLine:
        static_cast<void>(m_source_control_model.commit());
        return true;
    case UI::Editor::EditorInputCommand::Escape:
        m_source_control_model.set_input_focused(false);
        return true;
    default:
        break;
    }
    return false;
}

bool ToolSidebar::handle_source_control_action(UI::Editor::EditorAction action)
{
    if (!is_source_control_focused()) return false;
    switch (action) {
    case UI::Editor::EditorAction::SelectAll:
        m_source_control_model.select_all();
        return true;
    case UI::Editor::EditorAction::Copy:
        if (m_source_control_model.has_selection()) {
            NSPasteboard* pb = [NSPasteboard generalPasteboard];
            [pb clearContents];
            [pb setString:[NSString stringWithUTF8String:m_source_control_model.get_selected_text().c_str()] forType:NSPasteboardTypeString];
            return true;
        }
        break;
    case UI::Editor::EditorAction::Cut:
        if (m_source_control_model.has_selection()) {
            NSPasteboard* pb = [NSPasteboard generalPasteboard];
            [pb clearContents];
            [pb setString:[NSString stringWithUTF8String:m_source_control_model.get_selected_text().c_str()] forType:NSPasteboardTypeString];
            m_source_control_model.handle_backspace();
            return true;
        }
        break;
    case UI::Editor::EditorAction::Paste: {
        NSPasteboard* pb = [NSPasteboard generalPasteboard];
        NSString* str = [pb stringForType:NSPasteboardTypeString];
        if (str) {
            m_source_control_model.insert_text([str UTF8String]);
            return true;
        }
        break;
    }
    default:
        break;
    }
    return false;
}

std::size_t ToolSidebar::viewport_row_count(
    const UI::Editor::StudioEditorLayoutResult& layout) const noexcept
{
    const float available = std::max(layout.tool_sidebar_bounds.height -
                                         header_height * layout.dpi_scale,
                                     0.0F);
    return static_cast<std::size_t>(
        std::max(std::floor(available / (row_height * layout.dpi_scale)), 0.0F));
}

std::size_t ToolSidebar::search_viewport_row_count(
    const UI::Editor::StudioEditorLayoutResult& layout) const noexcept
{
    const float scale = layout.dpi_scale;
    const float tree_top = search_tree_top_y(layout);
    const float tree_height = std::max(layout.tool_sidebar_bounds.bottom() - tree_top, 0.0F);
    return static_cast<std::size_t>(tree_height / (row_height * scale));
}

float ToolSidebar::search_tree_top_y(
    const UI::Editor::StudioEditorLayoutResult& layout) const noexcept
{
    const float scale = layout.dpi_scale;
    const float input_top = layout.tool_sidebar_bounds.y + header_height * scale + 8.0F * scale;
    float tree_top = input_top + 34.0F * scale;
    if (m_search_model.is_replace_expanded())
    {
        tree_top += 30.0F * scale;
    }
    if (!m_search_model.get_search_query().empty() || !m_search_model.get_search_error().empty())
    {
        tree_top += 24.0F * scale;
    }
    return tree_top;
}

std::size_t ToolSidebar::source_control_viewport_row_count(
    const UI::Editor::StudioEditorLayoutResult& layout) const noexcept
{
    const float scale = layout.dpi_scale;
    const float tree_top = source_control_tree_top_y(layout);
    const float tree_height = std::max(layout.tool_sidebar_bounds.bottom() - tree_top, 0.0F);
    return static_cast<std::size_t>(tree_height / (row_height * scale));
}

float ToolSidebar::source_control_tree_top_y(
    const UI::Editor::StudioEditorLayoutResult& layout) const noexcept
{
    const float scale = layout.dpi_scale;
    const float input_top = layout.tool_sidebar_bounds.y + header_height * scale + 6.0F * scale;
    float tree_top = input_top + 32.0F * scale;
    if (!m_source_control_model.get_last_error().empty())
    {
        tree_top += 20.0F * scale;
    }
    return tree_top;
}

std::optional<std::size_t> ToolSidebar::row_from_point(
    const UI::Editor::StudioEditorLayoutResult& layout, float py) const noexcept
{
    const float content_top =
        layout.tool_sidebar_bounds.y + header_height * layout.dpi_scale;
    if (py < content_top || py > layout.tool_sidebar_bounds.bottom()) return std::nullopt;
    const float relative_y = py - content_top;
    if (relative_y < 0.0F) return std::nullopt;

    const float row_h = row_height * layout.dpi_scale;
    if (row_h <= 0.0F) return std::nullopt;

    return static_cast<std::size_t>(relative_y / row_h) +
           m_model.get_scroll_offset();
}

std::optional<std::size_t> ToolSidebar::search_row_from_point(
    const UI::Editor::StudioEditorLayoutResult& layout, float point_y) const noexcept
{
    const float scale = layout.dpi_scale;
    const float tree_top = search_tree_top_y(layout);
    if (point_y < tree_top || point_y >= layout.tool_sidebar_bounds.bottom())
    {
        return std::nullopt;
    }
    const std::size_t row = static_cast<std::size_t>((point_y - tree_top) / (row_height * scale));
    const auto visible_rows = m_search_model.get_visible_rows();
    const std::size_t index = m_search_model.get_scroll_offset() + row;
    if (index < visible_rows.size())
    {
        return index;
    }
    return std::nullopt;
}

std::optional<std::size_t> ToolSidebar::source_control_row_from_point(
    const UI::Editor::StudioEditorLayoutResult& layout, float point_y) const noexcept
{
    const float scale = layout.dpi_scale;
    const float tree_top = source_control_tree_top_y(layout);
    if (point_y < tree_top || point_y >= layout.tool_sidebar_bounds.bottom())
    {
        return std::nullopt;
    }
    const std::size_t row = static_cast<std::size_t>((point_y - tree_top) / (row_height * scale));
    const auto& visible_rows = m_source_control_model.get_visible_rows();
    const std::size_t index = m_source_control_model.get_scroll_offset() + row;
    if (index < visible_rows.size())
    {
        return index;
    }
    return std::nullopt;
}

UI::Rect ToolSidebar::scrollbar_bounds(
    const UI::Editor::StudioEditorLayoutResult& layout) const noexcept
{
    return {
        layout.tool_sidebar_bounds.right() - 8.0F * layout.dpi_scale,
        layout.tool_sidebar_bounds.y + header_height * layout.dpi_scale,
        8.0F * layout.dpi_scale,
        layout.tool_sidebar_bounds.height - header_height * layout.dpi_scale
    };
}

std::vector<std::size_t> ToolSidebar::get_sticky_items() const {
    std::vector<std::size_t> sticky;
    if (m_model.get_active_icon() != UI::Editor::SidebarIcon::Project) return sticky;
    const auto& items = m_model.get_project_items();
    if (items.empty()) return sticky;

    const std::size_t first = m_model.get_scroll_offset();
    if (first >= items.size()) return sticky;

    std::size_t curr_depth = items[first].depth;
    for (std::ptrdiff_t i = static_cast<std::ptrdiff_t>(first); i >= 0; --i) {
        const auto& item = items[static_cast<std::size_t>(i)];
        if (item.depth < curr_depth && item.directory) {
            sticky.push_back(static_cast<std::size_t>(i));
            curr_depth = item.depth;
            if (curr_depth == 0) break;
        }
    }
    std::reverse(sticky.begin(), sticky.end());
    return sticky;
}

bool ToolSidebar::handle_search_move(
    const UI::Editor::StudioEditorLayoutResult& layout,
    float px, float py) noexcept
{
    const float scale = layout.dpi_scale;
    const UI::Rect panel = layout.tool_sidebar_bounds;
    const float top = panel.y;
    const float header_cy = top + header_height * 0.5F * scale;
    const float btn_size = 20.0F * scale;

    m_hover_search_collapse_all = UI::Rect{panel.right() - 24.0F * scale - btn_size * 2.0F, header_cy - btn_size * 0.5F, btn_size, btn_size}.contains(px, py);
    m_hover_search_clear = UI::Rect{panel.right() - 22.0F * scale - btn_size, header_cy - btn_size * 0.5F, btn_size, btn_size}.contains(px, py);
    m_hover_search_refresh = UI::Rect{panel.right() - 20.0F * scale, header_cy - btn_size * 0.5F, btn_size, btn_size}.contains(px, py);

    const float input_top = top + header_height * scale + 8.0F * scale;
    m_hover_search_chevron = UI::Rect{panel.x + 6.0F * scale, input_top + 3.0F * scale, 18.0F * scale, 20.0F * scale}.contains(px, py);

    const UI::Rect search_bounds{panel.x + 28.0F * scale, input_top, std::max(panel.width - 36.0F * scale, 0.0F), 26.0F * scale};
    const float opt_btn_w = 20.0F * scale;
    const float opt_btn_h = 20.0F * scale;
    const float opt_btn_y = input_top + 3.0F * scale;

    m_hover_search_use_regex = UI::Rect{search_bounds.right() - 22.0F * scale, opt_btn_y, opt_btn_w, opt_btn_h}.contains(px, py);
    m_hover_search_match_word = UI::Rect{search_bounds.right() - 44.0F * scale, opt_btn_y, opt_btn_w, opt_btn_h}.contains(px, py);
    m_hover_search_match_case = UI::Rect{search_bounds.right() - 66.0F * scale, opt_btn_y, opt_btn_w, opt_btn_h}.contains(px, py);

    if (m_search_model.is_replace_expanded()) {
        const float replace_top = input_top + 30.0F * scale;
        const UI::Rect replace_bounds{panel.x + 28.0F * scale, replace_top, std::max(panel.width - 64.0F * scale, 0.0F), 26.0F * scale};
        m_hover_search_preserve_case = UI::Rect{replace_bounds.right() - 22.0F * scale, replace_top + 3.0F * scale, opt_btn_w, opt_btn_h}.contains(px, py);
        m_hover_search_replace_all = UI::Rect{panel.right() - 32.0F * scale, replace_top + 2.0F * scale, 22.0F * scale, 22.0F * scale}.contains(px, py);
    } else {
        m_hover_search_preserve_case = false;
        m_hover_search_replace_all = false;
    }

    m_hovered_search_row = search_row_from_point(layout, py);
    return true;
}

bool ToolSidebar::handle_search_press(
    StudioWorkspaceRenderer& surface,
    const UI::Editor::StudioEditorLayoutResult& layout,
    float px, float py,
    std::optional<std::filesystem::path>& file_to_open,
    std::optional<std::size_t>& target_line,
    std::optional<std::size_t>& target_col)
{
    (void)surface;
    const float scale = layout.dpi_scale;
    const UI::Rect panel = layout.tool_sidebar_bounds;
    const float top = panel.y;
    const float header_cy = top + header_height * 0.5F * scale;
    const float btn_size = 20.0F * scale;

    // Header buttons
    if (UI::Rect{panel.right() - 24.0F * scale - btn_size * 2.0F, header_cy - btn_size * 0.5F, btn_size, btn_size}.contains(px, py)) {
        m_search_model.collapse_all();
        return true;
    }
    if (UI::Rect{panel.right() - 22.0F * scale - btn_size, header_cy - btn_size * 0.5F, btn_size, btn_size}.contains(px, py)) {
        m_search_model.clear_query();
        return true;
    }
    if (UI::Rect{panel.right() - 20.0F * scale, header_cy - btn_size * 0.5F, btn_size, btn_size}.contains(px, py)) {
        m_search_model.execute_search();
        return true;
    }

    const float input_top = top + header_height * scale + 8.0F * scale;
    if (UI::Rect{panel.x + 6.0F * scale, input_top + 3.0F * scale, 18.0F * scale, 20.0F * scale}.contains(px, py)) {
        m_search_model.toggle_replace_expanded();
        return true;
    }

    const UI::Rect search_bounds{panel.x + 28.0F * scale, input_top, std::max(panel.width - 36.0F * scale, 0.0F), 26.0F * scale};
    const float opt_btn_w = 20.0F * scale;
    const float opt_btn_h = 20.0F * scale;
    const float opt_btn_y = input_top + 3.0F * scale;

    if (UI::Rect{search_bounds.right() - 22.0F * scale, opt_btn_y, opt_btn_w, opt_btn_h}.contains(px, py)) {
        m_search_model.toggle_use_regex();
        return true;
    }
    if (UI::Rect{search_bounds.right() - 44.0F * scale, opt_btn_y, opt_btn_w, opt_btn_h}.contains(px, py)) {
        m_search_model.toggle_match_word();
        return true;
    }
    if (UI::Rect{search_bounds.right() - 66.0F * scale, opt_btn_y, opt_btn_w, opt_btn_h}.contains(px, py)) {
        m_search_model.toggle_match_case();
        return true;
    }

    if (search_bounds.contains(px, py)) {
        m_search_model.set_focused_input(UI::Editor::SearchInputFocus::Search);
        const std::string_view q = m_search_model.get_search_query();
        const float text_x = search_bounds.x + 6.0F * scale;
        const float char_w = 7.2F * scale;
        std::size_t idx = 0;
        if (px > text_x) {
            idx = static_cast<std::size_t>((px - text_x + char_w * 0.5F) / char_w);
            idx = std::min(idx, q.size());
        }
        m_search_model.set_caret_and_selection(idx, idx, idx);
        m_is_selecting_search_text = true;
        return true;
    }

    if (m_search_model.is_replace_expanded()) {
        const float replace_top = input_top + 30.0F * scale;
        const UI::Rect replace_bounds{panel.x + 28.0F * scale, replace_top, std::max(panel.width - 64.0F * scale, 0.0F), 26.0F * scale};
        if (UI::Rect{replace_bounds.right() - 22.0F * scale, replace_top + 3.0F * scale, opt_btn_w, opt_btn_h}.contains(px, py)) {
            m_search_model.toggle_preserve_case();
            return true;
        }
        if (UI::Rect{panel.right() - 32.0F * scale, replace_top + 2.0F * scale, 22.0F * scale, 22.0F * scale}.contains(px, py)) {
            m_search_model.replace_all();
            return true;
        }
        if (replace_bounds.contains(px, py)) {
            m_search_model.set_focused_input(UI::Editor::SearchInputFocus::Replace);
            const std::string_view q = m_search_model.get_replace_query();
            const float text_x = replace_bounds.x + 6.0F * scale;
            const float char_w = 7.2F * scale;
            std::size_t idx = 0;
            if (px > text_x) {
                idx = static_cast<std::size_t>((px - text_x + char_w * 0.5F) / char_w);
                idx = std::min(idx, q.size());
            }
            m_search_model.set_caret_and_selection(idx, idx, idx);
            m_is_selecting_search_text = true;
            return true;
        }
    }

    // Results Tree Row Activation
    if (auto row = search_row_from_point(layout, py)) {
        if (auto nav = m_search_model.activate_visible_row(*row)) {
            file_to_open = nav->path;
            target_line = nav->line;
            target_col = nav->column;
            return true;
        }
        return true;
    }

    return true;
}

bool ToolSidebar::handle_source_control_move(
    const UI::Editor::StudioEditorLayoutResult& layout,
    float px, float py) noexcept
{
    const float scale = layout.dpi_scale;
    const UI::Rect panel = layout.tool_sidebar_bounds;
    const float top = panel.y;
    const float header_cy = top + header_height * 0.5F * scale;
    const float btn_size = 20.0F * scale;

    m_hover_sc_more = UI::Rect{panel.right() - 20.0F * scale, header_cy - btn_size * 0.5F, btn_size, btn_size}.contains(px, py);

    m_hovered_sc_row = source_control_row_from_point(layout, py);

    m_hover_sc_action_row.reset();
    m_hover_sc_row_action_open_diff = false;
    m_hover_sc_row_action_stage = false;
    m_hover_sc_row_action_discard = false;
    m_hover_sc_stage_all = false;
    m_hover_sc_unstage_all = false;
    m_hover_sc_discard_all = false;
    m_hover_sc_repo_refresh = false;
    m_hover_sc_repo_commit = false;
    m_hover_sc_repo_discard = false;
    m_hover_sc_repo_more = false;
    m_hover_sc_graph_refresh = false;

    if (m_hovered_sc_row) {
        const auto& visible_rows = m_source_control_model.get_visible_rows();
        if (*m_hovered_sc_row < visible_rows.size()) {
            const auto& row = visible_rows[*m_hovered_sc_row];
            const float tree_top = source_control_tree_top_y(layout);
            const float row_y = tree_top + static_cast<float>(*m_hovered_sc_row - m_source_control_model.get_scroll_offset()) * row_height * scale;

            if (row.kind == UI::Editor::SourceControlRowKind::RepositoryHeader) {
                m_hover_sc_repo_more = UI::Rect{panel.right() - 20.0F * scale, row_y + 2.0F * scale, 18.0F * scale, 18.0F * scale}.contains(px, py);
                m_hover_sc_repo_discard = UI::Rect{panel.right() - 40.0F * scale, row_y + 2.0F * scale, 18.0F * scale, 18.0F * scale}.contains(px, py);
                m_hover_sc_repo_commit = UI::Rect{panel.right() - 60.0F * scale, row_y + 2.0F * scale, 18.0F * scale, 18.0F * scale}.contains(px, py);
                m_hover_sc_repo_refresh = UI::Rect{panel.right() - 80.0F * scale, row_y + 2.0F * scale, 18.0F * scale, 18.0F * scale}.contains(px, py);
            } else if (row.kind == UI::Editor::SourceControlRowKind::SectionHeaderStaged) {
                m_hover_sc_unstage_all = UI::Rect{panel.right() - 24.0F * scale, row_y + 2.0F * scale, 18.0F * scale, 18.0F * scale}.contains(px, py);
            } else if (row.kind == UI::Editor::SourceControlRowKind::SectionHeaderUnstaged) {
                m_hover_sc_discard_all = UI::Rect{panel.right() - 24.0F * scale, row_y + 2.0F * scale, 18.0F * scale, 18.0F * scale}.contains(px, py);
                m_hover_sc_stage_all = UI::Rect{panel.right() - 44.0F * scale, row_y + 2.0F * scale, 18.0F * scale, 18.0F * scale}.contains(px, py);
            } else if (row.kind == UI::Editor::SourceControlRowKind::GitGraphHeader) {
                m_hover_sc_graph_refresh = UI::Rect{panel.right() - 20.0F * scale, row_y + 2.0F * scale, 18.0F * scale, 18.0F * scale}.contains(px, py);
            } else if (row.kind == UI::Editor::SourceControlRowKind::StagedFile ||
                       row.kind == UI::Editor::SourceControlRowKind::UnstagedFile ||
                       row.kind == UI::Editor::SourceControlRowKind::UntrackedFile) {
                m_hover_sc_action_row = *m_hovered_sc_row;
                if (row.is_staged) {
                    m_hover_sc_row_action_stage = UI::Rect{panel.right() - 24.0F * scale, row_y + 2.0F * scale, 18.0F * scale, 18.0F * scale}.contains(px, py);
                    m_hover_sc_row_action_open_diff = UI::Rect{panel.right() - 44.0F * scale, row_y + 2.0F * scale, 18.0F * scale, 18.0F * scale}.contains(px, py);
                } else {
                    m_hover_sc_row_action_stage = UI::Rect{panel.right() - 24.0F * scale, row_y + 2.0F * scale, 18.0F * scale, 18.0F * scale}.contains(px, py);
                    m_hover_sc_row_action_discard = UI::Rect{panel.right() - 44.0F * scale, row_y + 2.0F * scale, 18.0F * scale, 18.0F * scale}.contains(px, py);
                    m_hover_sc_row_action_open_diff = UI::Rect{panel.right() - 64.0F * scale, row_y + 2.0F * scale, 18.0F * scale, 18.0F * scale}.contains(px, py);
                }
            }
        }
    }

    return true;
}

bool ToolSidebar::handle_source_control_press(
    StudioWorkspaceRenderer& surface,
    const UI::Editor::StudioEditorLayoutResult& layout,
    float px, float py,
    std::optional<std::filesystem::path>& file_to_open)
{
    (void)surface;
    const float scale = layout.dpi_scale;
    const UI::Rect panel = layout.tool_sidebar_bounds;
    const float top = panel.y;
    const float header_cy = top + header_height * 0.5F * scale;
    const float btn_size = 20.0F * scale;

    // Header More options
    if (UI::Rect{panel.right() - 20.0F * scale, header_cy - btn_size * 0.5F, btn_size, btn_size}.contains(px, py)) {
        m_source_control_model.refresh_status();
        return true;
    }

    if (!m_source_control_model.is_git_repository()) {
        const float init_btn_top = top + header_height * scale + 48.0F * scale;
        const UI::Rect init_bounds{panel.x + 16.0F * scale, init_btn_top, std::max(panel.width - 32.0F * scale, 0.0F), 28.0F * scale};
        if (init_bounds.contains(px, py)) {
            static_cast<void>(m_source_control_model.get_repository().init_repository(m_source_control_model.get_workspace_root()));
            m_source_control_model.refresh_status();
            return true;
        }
        return true;
    }

    const float input_top = top + header_height * scale + 6.0F * scale;
    const UI::Rect msg_bounds{panel.x + 12.0F * scale, input_top, std::max(panel.width - 24.0F * scale, 0.0F), 26.0F * scale};

    // Message box
    if (msg_bounds.contains(px, py)) {
        m_source_control_model.set_input_focused(true);
        const std::string& msg = m_source_control_model.get_commit_message();
        const float text_x = msg_bounds.x + 6.0F * scale;
        const float char_w = 7.2F * scale;
        std::size_t idx = 0;
        if (px > text_x) {
            idx = static_cast<std::size_t>((px - text_x + char_w * 0.5F) / char_w);
            idx = std::min(idx, msg.size());
        }
        m_source_control_model.set_caret_and_selection(idx, idx);
        m_is_selecting_sc_text = true;
        return true;
    }

    // Row clicks
    if (auto row_opt = source_control_row_from_point(layout, py)) {
        const auto& visible_rows = m_source_control_model.get_visible_rows();
        if (*row_opt < visible_rows.size()) {
            const auto& row = visible_rows[*row_opt];
            if (row.kind == UI::Editor::SourceControlRowKind::RepositoryHeader) {
                if (m_hover_sc_repo_refresh) {
                    m_source_control_model.refresh_status();
                } else if (m_hover_sc_repo_commit) {
                    static_cast<void>(m_source_control_model.commit());
                } else if (m_hover_sc_repo_discard) {
                    static_cast<void>(m_source_control_model.discard_changes(""));
                } else {
                    m_source_control_model.toggle_repo_collapsed();
                }
                return true;
            }
            if (row.kind == UI::Editor::SourceControlRowKind::SectionHeaderStaged) {
                if (m_hover_sc_unstage_all) {
                    static_cast<void>(m_source_control_model.unstage_all());
                } else {
                    m_source_control_model.toggle_staged_collapsed();
                }
                return true;
            }
            if (row.kind == UI::Editor::SourceControlRowKind::SectionHeaderUnstaged) {
                if (m_hover_sc_stage_all) {
                    static_cast<void>(m_source_control_model.stage_all());
                } else if (m_hover_sc_discard_all) {
                    static_cast<void>(m_source_control_model.discard_changes(""));
                } else {
                    m_source_control_model.toggle_unstaged_collapsed();
                }
                return true;
            }
            if (row.kind == UI::Editor::SourceControlRowKind::GitGraphHeader) {
                if (m_hover_sc_graph_refresh) {
                    m_source_control_model.refresh_status();
                } else {
                    m_source_control_model.toggle_git_graph_collapsed();
                }
                return true;
            }

            if (row.is_staged) {
                if (m_hover_sc_row_action_stage) {
                    static_cast<void>(m_source_control_model.unstage_file(row.path));
                    return true;
                }
            } else {
                if (m_hover_sc_row_action_stage) {
                    static_cast<void>(m_source_control_model.stage_file(row.path));
                    return true;
                }
                if (m_hover_sc_row_action_discard) {
                    static_cast<void>(m_source_control_model.discard_changes(row.path));
                    return true;
                }
            }

            // Open file
            file_to_open = m_source_control_model.activate_visible_row(*row_opt);
            return true;
        }
    }

    return true;
}

void ToolSidebar::render_source_control_panel(
    const StudioWorkspaceRenderer& surface,
    CGContextRef context,
    const UI::Editor::StudioEditorLayoutResult& layout) const
{
    const UI::Rect panel = layout.tool_sidebar_bounds;
    const float scale = layout.dpi_scale;

    // 1. Header: "Source Control", More (...)
    const float header_cy = panel.y + header_height * 0.5F * scale;
    if (surface.m_ui_font) {
        surface.draw_text(context, *surface.m_ui_font, "Source Control",
                          panel.x + 14.0F * scale,
                          header_cy,
                          surface.m_text.primary);
    }

    const float btn_size = 18.0F * scale;

    // More Options Button (...)
    const UI::Rect more_rect{panel.right() - 20.0F * scale, header_cy - btn_size * 0.5F, btn_size, btn_size};
    if (m_hover_sc_more) {
        const CGFloat hov_rgba[4] = {1.0, 1.0, 1.0, 0.08};
        surface.fill_rounded_rectangle(context, more_rect, hov_rgba, 3.0F * scale);
    }
    surface.draw_svg_icon(context, "vscode-codicons/icons/ellipsis.svg",
                          round_to_int(more_rect.x + btn_size * 0.5F),
                          round_to_int(more_rect.y + btn_size * 0.5F),
                          round_to_int(13.0F * scale),
                          m_hover_sc_more ? UI::Theme::Color{255, 255, 255, 255} : surface.m_palette.text_muted,
                          surface.m_palette.sidebar_background);

    surface.draw_line(context, round_to_int(panel.x),
                      round_to_int(panel.y + header_height * scale),
                      round_to_int(panel.right()),
                      round_to_int(panel.y + header_height * scale),
                      surface.m_colors.border);

    // If Not Git Repository
    if (!m_source_control_model.is_git_repository()) {
        const float msg_top = panel.y + header_height * scale + 24.0F * scale;
        if (surface.m_small_font) {
            surface.draw_text(context, *surface.m_small_font, "The folder is not a Git repository.",
                              panel.x + 16.0F * scale, msg_top,
                              surface.m_text.muted);
        }

        const UI::Rect init_bounds{panel.x + 16.0F * scale, msg_top + 24.0F * scale, std::max(panel.width - 32.0F * scale, 0.0F), 28.0F * scale};
        const CGFloat init_col[4] = {59.0/255.0, 130.0/255.0, 246.0/255.0, 0.85};
        surface.fill_rounded_rectangle(context, init_bounds, init_col, 4.0F * scale);
        if (surface.m_small_font) {
            surface.draw_text(context, *surface.m_small_font, "Initialize Repository",
                              init_bounds.x + 18.0F * scale, init_bounds.y + init_bounds.height * 0.5F,
                              "#ffffff");
        }
        return;
    }

    // 2. Commit Message Box
    const float input_top = panel.y + header_height * scale + 6.0F * scale;
    const UI::Rect msg_bounds{panel.x + 12.0F * scale, input_top, std::max(panel.width - 24.0F * scale, 0.0F), 26.0F * scale};

    const bool input_focused = m_source_control_model.is_input_focused();
    const bool caret_visible = (static_cast<std::uint64_t>([[NSDate date] timeIntervalSince1970] * 2) % 2) == 0;

    surface.fill_rounded_rectangle(context, msg_bounds, surface.m_colors.editor_background, 4.0F * scale);
    const CGFloat* msg_border = input_focused ? surface.m_colors.accent : surface.m_colors.border;
    surface.draw_rectangle(context, msg_bounds, msg_border);

    const std::string& msg = m_source_control_model.get_commit_message();
    if (msg.empty()) {
        if (surface.m_small_font) {
            surface.draw_text(context, *surface.m_small_font, "Message (Cmd+Enter to commit)",
                              msg_bounds.x + 6.0F * scale,
                              msg_bounds.y + msg_bounds.height * 0.5F,
                              surface.m_text.muted);
        }
        if (input_focused && caret_visible) {
            surface.draw_line(context, round_to_int(msg_bounds.x + 6.0F * scale),
                              round_to_int(msg_bounds.y + 4.0F * scale),
                              round_to_int(msg_bounds.x + 6.0F * scale),
                              round_to_int(msg_bounds.bottom() - 4.0F * scale),
                              surface.m_colors.text_primary);
        }
    } else {
        if (input_focused && m_source_control_model.has_selection()) {
            const auto [s_min, s_max] = m_source_control_model.get_selection_range();
            const int w_before = surface.m_small_font ? surface.m_small_font->getTextWidth(msg.substr(0, s_min)) : 0;
            const int w_sel = surface.m_small_font ? surface.m_small_font->getTextWidth(msg.substr(s_min, s_max - s_min)) : 0;
            const UI::Rect sel_rect{
                msg_bounds.x + 6.0F * scale + static_cast<float>(w_before),
                msg_bounds.y + 3.0F * scale,
                static_cast<float>(w_sel),
                msg_bounds.height - 6.0F * scale
            };
            const CGFloat sel_col[4] = {59.0/255.0, 130.0/255.0, 246.0/255.0, 0.40};
            surface.fill_rounded_rectangle(context, sel_rect, sel_col, 2.0F * scale);
        }

        if (surface.m_small_font) {
            surface.draw_text(context, *surface.m_small_font, msg,
                              msg_bounds.x + 6.0F * scale,
                              msg_bounds.y + msg_bounds.height * 0.5F,
                              surface.m_text.primary);
        }

        if (input_focused && caret_visible && !m_source_control_model.has_selection()) {
            const int text_w = surface.m_small_font ? surface.m_small_font->getTextWidth(msg.substr(0, m_source_control_model.get_caret())) : 0;
            const float caret_x = msg_bounds.x + 6.0F * scale + static_cast<float>(text_w);
            surface.draw_line(context, round_to_int(caret_x),
                              round_to_int(msg_bounds.y + 4.0F * scale),
                              round_to_int(caret_x),
                              round_to_int(msg_bounds.bottom() - 4.0F * scale),
                              surface.m_colors.text_primary);
        }
    }

    // Error Message (if any)
    if (!m_source_control_model.get_last_error().empty() && surface.m_small_font) {
        surface.draw_text(context, *surface.m_small_font, m_source_control_model.get_last_error(),
                          panel.x + 14.0F * scale, input_top + 28.0F * scale,
                          "#f87171");
    }

    // 3. Tree List (Repository Header, Sections, Changes, Git Graph)
    const float tree_top = source_control_tree_top_y(layout);
    const auto& visible_rows = m_source_control_model.get_visible_rows();

    if (visible_rows.empty()) {
        if (surface.m_small_font) {
            surface.draw_text(context, *surface.m_small_font, "No changes detected in repository.",
                              panel.x + 14.0F * scale, tree_top + 16.0F * scale,
                              surface.m_text.muted);
        }
        return;
    }

    const std::size_t first = m_source_control_model.get_scroll_offset();
    const std::size_t row_count = source_control_viewport_row_count(layout);
    const std::size_t end = std::min(visible_rows.size(), first + row_count + 1);

    CGContextSaveGState(context);
    CGRect clip_rect = CGRectMake(panel.x, tree_top, panel.width, std::max(panel.bottom() - tree_top, 0.0F));
    CGContextClipToRect(context, clip_rect);

    for (std::size_t item_index = first; item_index < end; ++item_index) {
        const std::size_t visible_row = item_index - first;
        const auto& row = visible_rows[item_index];
        const float row_y = tree_top + static_cast<float>(visible_row) * row_height * scale;
        const bool is_hovered = (m_hovered_sc_row && *m_hovered_sc_row == item_index);

        if (is_hovered) {
            const CGFloat hov_rgba[4] = {1.0, 1.0, 1.0, 0.06};
            surface.fill_rounded_rectangle(context,
                UI::Rect{panel.x + 4.0F * scale, row_y + 1.0F * scale, panel.width - 8.0F * scale, row_height * scale - 2.0F * scale},
                hov_rgba, 3.0F * scale);
        }

        if (row.kind == UI::Editor::SourceControlRowKind::RepositoryHeader) {
            // ⌄ ZDE-minimal   main*   [⟳] [✓] [↺] [...]
            const bool collapsed = m_source_control_model.is_repo_collapsed();
            const std::string chevron = collapsed ? "Assets/icons/chevron-right.svg" : "Assets/icons/chevron-down.svg";

            surface.draw_svg_icon(context, chevron,
                                  round_to_int(panel.x + 10.0F * scale),
                                  round_to_int(row_y + row_height * 0.5F * scale),
                                  std::max(round_to_int(9.0F * scale), 8),
                                  surface.m_palette.text_muted,
                                  surface.m_palette.sidebar_background);

            surface.draw_svg_icon(context, "Assets/icons/folder.svg",
                                  round_to_int(panel.x + 22.0F * scale),
                                  round_to_int(row_y + row_height * 0.5F * scale),
                                  round_to_int(13.0F * scale),
                                  surface.m_palette.text_primary,
                                  surface.m_palette.sidebar_background);

            if (surface.m_small_font) {
                surface.draw_text(context, *surface.m_small_font, row.label,
                                  panel.x + 36.0F * scale, row_y + row_height * 0.5F * scale,
                                  surface.m_text.primary);
            }

            const int repo_w = surface.m_small_font ? surface.m_small_font->getTextWidth(row.label) : 40;
            if (!row.commit_branch.empty() && surface.m_small_font) {
                surface.draw_text(context, *surface.m_small_font, row.commit_branch,
                                  panel.x + 42.0F * scale + static_cast<float>(repo_w),
                                  row_y + row_height * 0.5F * scale,
                                  surface.m_text.muted);
            }

            // Inline actions on hover: [⟳] [✓] [↺] [...]
            if (is_hovered) {
                const UI::Rect more_btn{panel.right() - 20.0F * scale, row_y + 2.0F * scale, 18.0F * scale, 18.0F * scale};
                const UI::Rect discard_btn{panel.right() - 40.0F * scale, row_y + 2.0F * scale, 18.0F * scale, 18.0F * scale};
                const UI::Rect commit_btn{panel.right() - 60.0F * scale, row_y + 2.0F * scale, 18.0F * scale, 18.0F * scale};
                const UI::Rect refresh_btn{panel.right() - 80.0F * scale, row_y + 2.0F * scale, 18.0F * scale, 18.0F * scale};

                if (m_hover_sc_repo_more) surface.fill_rounded_rectangle(context, more_btn, surface.m_colors.hover_background, 3.0F * scale);
                surface.draw_svg_icon(context, "vscode-codicons/icons/ellipsis.svg", round_to_int(more_btn.x + 9.0F * scale), round_to_int(more_btn.y + 9.0F * scale), round_to_int(11.0F * scale), surface.m_palette.text_muted, surface.m_palette.sidebar_background);

                if (m_hover_sc_repo_discard) surface.fill_rounded_rectangle(context, discard_btn, surface.m_colors.hover_background, 3.0F * scale);
                surface.draw_svg_icon(context, "Assets/icons/refresh.svg", round_to_int(discard_btn.x + 9.0F * scale), round_to_int(discard_btn.y + 9.0F * scale), round_to_int(11.0F * scale), surface.m_palette.text_muted, surface.m_palette.sidebar_background);

                if (m_hover_sc_repo_commit) surface.fill_rounded_rectangle(context, commit_btn, surface.m_colors.hover_background, 3.0F * scale);
                surface.draw_svg_icon(context, "vscode-codicons/icons/check.svg", round_to_int(commit_btn.x + 9.0F * scale), round_to_int(commit_btn.y + 9.0F * scale), round_to_int(12.0F * scale), surface.m_palette.text_muted, surface.m_palette.sidebar_background);

                if (m_hover_sc_repo_refresh) surface.fill_rounded_rectangle(context, refresh_btn, surface.m_colors.hover_background, 3.0F * scale);
                surface.draw_svg_icon(context, "Assets/icons/refresh.svg", round_to_int(refresh_btn.x + 9.0F * scale), round_to_int(refresh_btn.y + 9.0F * scale), round_to_int(11.0F * scale), surface.m_palette.text_muted, surface.m_palette.sidebar_background);
            }
        }
        else if (row.kind == UI::Editor::SourceControlRowKind::SectionHeaderStaged ||
                 row.kind == UI::Editor::SourceControlRowKind::SectionHeaderUnstaged) {
            const bool is_staged = (row.kind == UI::Editor::SourceControlRowKind::SectionHeaderStaged);
            const bool collapsed = is_staged ? m_source_control_model.is_staged_collapsed() : m_source_control_model.is_unstaged_collapsed();
            const std::string chevron = collapsed ? "Assets/icons/chevron-right.svg" : "Assets/icons/chevron-down.svg";

            surface.draw_svg_icon(context, chevron,
                                  round_to_int(panel.x + 18.0F * scale),
                                  round_to_int(row_y + row_height * 0.5F * scale),
                                  std::max(round_to_int(9.0F * scale), 8),
                                  surface.m_palette.text_muted,
                                  surface.m_palette.sidebar_background);

            if (surface.m_small_font) {
                surface.draw_text(context, *surface.m_small_font, row.label,
                                  panel.x + 30.0F * scale, row_y + row_height * 0.5F * scale,
                                  surface.m_text.primary);
            }

            // Count badge pill
            const std::size_t count = is_staged ? m_source_control_model.get_status().staged_items.size()
                                                : (m_source_control_model.get_status().unstaged_items.size() + m_source_control_model.get_status().untracked_items.size());
            const std::string count_str = std::to_string(count);
            const int count_w = surface.m_small_font ? surface.m_small_font->getTextWidth(count_str) : 10;
            const float pill_w = std::max(static_cast<float>(count_w) + 8.0F * scale, 18.0F * scale);
            const UI::Rect pill_bounds{panel.right() - pill_w - (is_hovered ? 48.0F * scale : 10.0F * scale), row_y + 3.0F * scale, pill_w, 16.0F * scale};

            const CGFloat badge_rgba[4] = {1.0, 1.0, 1.0, 0.12};
            surface.fill_rounded_rectangle(context, pill_bounds, badge_rgba, 8.0F * scale);
            if (surface.m_small_font) {
                surface.draw_text(context, *surface.m_small_font, count_str,
                                  pill_bounds.x + (pill_w - static_cast<float>(count_w)) * 0.5F,
                                  pill_bounds.y + pill_bounds.height * 0.5F,
                                  surface.m_text.muted);
            }

            // Section Action Buttons on hover
            if (is_hovered) {
                if (is_staged) {
                    const UI::Rect unstage_all_rect{panel.right() - 24.0F * scale, row_y + 2.0F * scale, 18.0F * scale, 18.0F * scale};
                    if (m_hover_sc_unstage_all) surface.fill_rounded_rectangle(context, unstage_all_rect, surface.m_colors.hover_background, 3.0F * scale);
                    surface.draw_svg_icon(context, "vscode-codicons/icons/close-minimal.svg", round_to_int(unstage_all_rect.x + 9.0F * scale), round_to_int(unstage_all_rect.y + 9.0F * scale), round_to_int(10.0F * scale), surface.m_palette.text_muted, surface.m_palette.sidebar_background);
                } else {
                    const UI::Rect discard_all_rect{panel.right() - 24.0F * scale, row_y + 2.0F * scale, 18.0F * scale, 18.0F * scale};
                    const UI::Rect stage_all_rect{panel.right() - 44.0F * scale, row_y + 2.0F * scale, 18.0F * scale, 18.0F * scale};

                    if (m_hover_sc_discard_all) surface.fill_rounded_rectangle(context, discard_all_rect, surface.m_colors.hover_background, 3.0F * scale);
                    surface.draw_svg_icon(context, "Assets/icons/refresh.svg", round_to_int(discard_all_rect.x + 9.0F * scale), round_to_int(discard_all_rect.y + 9.0F * scale), round_to_int(11.0F * scale), surface.m_palette.text_muted, surface.m_palette.sidebar_background);

                    if (m_hover_sc_stage_all) surface.fill_rounded_rectangle(context, stage_all_rect, surface.m_colors.hover_background, 3.0F * scale);
                    surface.draw_svg_icon(context, "vscode-codicons/icons/collapse-all.svg", round_to_int(stage_all_rect.x + 9.0F * scale), round_to_int(stage_all_rect.y + 9.0F * scale), round_to_int(11.0F * scale), surface.m_palette.text_muted, surface.m_palette.sidebar_background);
                }
            }
        }
        else if (row.kind == UI::Editor::SourceControlRowKind::StagedFile ||
                 row.kind == UI::Editor::SourceControlRowKind::UnstagedFile ||
                 row.kind == UI::Editor::SourceControlRowKind::UntrackedFile) {
            // File Row
            const std::string icon_asset = UI::Editor::file_icon_asset_for_path(row.path);
            surface.draw_svg_icon(context, "Assets/icons/" + icon_asset,
                                  round_to_int(panel.x + 28.0F * scale),
                                  round_to_int(row_y + row_height * 0.5F * scale),
                                  round_to_int(14.0F * scale),
                                  surface.m_palette.text_primary,
                                  surface.m_palette.sidebar_background,
                                  true);

            // File Name
            if (surface.m_small_font) {
                surface.draw_text(context, *surface.m_small_font, row.label,
                                  panel.x + 42.0F * scale, row_y + row_height * 0.5F * scale,
                                  surface.m_text.primary);
            }

            const int fname_w = surface.m_small_font ? surface.m_small_font->getTextWidth(row.label) : 30;

            // Directory Path in muted text
            if (!row.relative_dir.empty()) {
                const float dir_x = panel.x + 48.0F * scale + static_cast<float>(fname_w);
                const int avail_dir_w = round_to_int(panel.right() - dir_x - (is_hovered ? 68.0F * scale : 42.0F * scale));
                if (avail_dir_w > 14 && surface.m_small_font) {
                    const std::string dir_text = ellipsize(*surface.m_small_font, row.relative_dir, avail_dir_w);
                    surface.draw_text(context, *surface.m_small_font, dir_text,
                                      dir_x, row_y + row_height * 0.5F * scale,
                                      surface.m_text.muted);
                }
            }

            // Status Badge Letter on right (e.g. "9+, M" or "M")
            const std::string letter{Git::git_file_status_letter(row.status)};
            if (!letter.empty() && surface.m_small_font) {
                const char* letter_col = "#ffffff";
                if (row.status == Git::GitFileStatus::Modified) letter_col = "#eab308";
                else if (row.status == Git::GitFileStatus::Added || row.status == Git::GitFileStatus::Untracked) letter_col = "#22c55e";
                else if (row.status == Git::GitFileStatus::Deleted) letter_col = "#ef4444";

                const float badge_x = panel.right() - (is_hovered ? 66.0F * scale : 20.0F * scale);
                surface.draw_text(context, *surface.m_small_font, letter,
                                  badge_x, row_y + row_height * 0.5F * scale,
                                  letter_col);
            }

            // Inline Action Buttons on hover: [⎘] [↺] [+]
            if (is_hovered) {
                if (row.is_staged) {
                    const UI::Rect unstage_btn{panel.right() - 20.0F * scale, row_y + 2.0F * scale, 18.0F * scale, 18.0F * scale};
                    const UI::Rect diff_btn{panel.right() - 40.0F * scale, row_y + 2.0F * scale, 18.0F * scale, 18.0F * scale};

                    if (m_hover_sc_row_action_stage) surface.fill_rounded_rectangle(context, unstage_btn, surface.m_colors.hover_background, 3.0F * scale);
                    surface.draw_svg_icon(context, "vscode-codicons/icons/close-minimal.svg", round_to_int(unstage_btn.x + 9.0F * scale), round_to_int(unstage_btn.y + 9.0F * scale), round_to_int(10.0F * scale), surface.m_palette.text_muted, surface.m_palette.sidebar_background);

                    if (m_hover_sc_row_action_open_diff) surface.fill_rounded_rectangle(context, diff_btn, surface.m_colors.hover_background, 3.0F * scale);
                    surface.draw_svg_icon(context, "vscode-codicons/icons/file.svg", round_to_int(diff_btn.x + 9.0F * scale), round_to_int(diff_btn.y + 9.0F * scale), round_to_int(11.0F * scale), surface.m_palette.text_muted, surface.m_palette.sidebar_background);
                } else {
                    const UI::Rect stage_btn{panel.right() - 20.0F * scale, row_y + 2.0F * scale, 18.0F * scale, 18.0F * scale};
                    const UI::Rect discard_btn{panel.right() - 40.0F * scale, row_y + 2.0F * scale, 18.0F * scale, 18.0F * scale};
                    const UI::Rect diff_btn{panel.right() - 60.0F * scale, row_y + 2.0F * scale, 18.0F * scale, 18.0F * scale};

                    if (m_hover_sc_row_action_stage) surface.fill_rounded_rectangle(context, stage_btn, surface.m_colors.hover_background, 3.0F * scale);
                    surface.draw_svg_icon(context, "vscode-codicons/icons/collapse-all.svg", round_to_int(stage_btn.x + 9.0F * scale), round_to_int(stage_btn.y + 9.0F * scale), round_to_int(11.0F * scale), surface.m_palette.text_muted, surface.m_palette.sidebar_background);

                    if (m_hover_sc_row_action_discard) surface.fill_rounded_rectangle(context, discard_btn, surface.m_colors.hover_background, 3.0F * scale);
                    surface.draw_svg_icon(context, "Assets/icons/refresh.svg", round_to_int(discard_btn.x + 9.0F * scale), round_to_int(discard_btn.y + 9.0F * scale), round_to_int(11.0F * scale), surface.m_palette.text_muted, surface.m_palette.sidebar_background);

                    if (m_hover_sc_row_action_open_diff) surface.fill_rounded_rectangle(context, diff_btn, surface.m_colors.hover_background, 3.0F * scale);
                    surface.draw_svg_icon(context, "vscode-codicons/icons/file.svg", round_to_int(diff_btn.x + 9.0F * scale), round_to_int(diff_btn.y + 9.0F * scale), round_to_int(11.0F * scale), surface.m_palette.text_muted, surface.m_palette.sidebar_background);
                }
            }
        }
        else if (row.kind == UI::Editor::SourceControlRowKind::GitGraphHeader) {
            // ⌄ Git Graph   ZDE-minimal   Auto   ◎ ⤓ ⤒ ⟳
            const bool collapsed = m_source_control_model.is_git_graph_collapsed();
            const std::string chevron = collapsed ? "Assets/icons/chevron-right.svg" : "Assets/icons/chevron-down.svg";

            surface.draw_svg_icon(context, chevron,
                                  round_to_int(panel.x + 10.0F * scale),
                                  round_to_int(row_y + row_height * 0.5F * scale),
                                  std::max(round_to_int(9.0F * scale), 8),
                                  surface.m_palette.text_muted,
                                  surface.m_palette.sidebar_background);

            if (surface.m_small_font) {
                surface.draw_text(context, *surface.m_small_font, row.label,
                                  panel.x + 22.0F * scale, row_y + row_height * 0.5F * scale,
                                  surface.m_text.primary);
            }

            const int graph_w = surface.m_small_font ? surface.m_small_font->getTextWidth(row.label) : 50;

            if (!row.relative_dir.empty() && surface.m_small_font) {
                surface.draw_text(context, *surface.m_small_font, row.relative_dir,
                                  panel.x + 28.0F * scale + static_cast<float>(graph_w),
                                  row_y + row_height * 0.5F * scale,
                                  surface.m_text.muted);
            }

            // Buttons: Auto, Refresh
            if (surface.m_small_font) {
                surface.draw_text(context, *surface.m_small_font, "Auto",
                                  panel.right() - 56.0F * scale, row_y + row_height * 0.5F * scale,
                                  surface.m_text.muted);
            }
            surface.draw_svg_icon(context, "Assets/icons/refresh.svg",
                                  round_to_int(panel.right() - 16.0F * scale),
                                  round_to_int(row_y + row_height * 0.5F * scale),
                                  round_to_int(11.0F * scale),
                                  m_hover_sc_graph_refresh ? UI::Theme::Color{255, 255, 255, 255} : surface.m_palette.text_muted,
                                  surface.m_palette.sidebar_background);
        }
        else if (row.kind == UI::Editor::SourceControlRowKind::GitGraphCommit) {
            // Live Git Graph Timeline Node & Vertical Trunk Line
            const float trunk_x = panel.x + 16.0F * scale;
            const float node_y = row_y + row_height * 0.5F * scale;

            // Draw vertical track line
            const CGFloat line_col[4] = {0.0, 122.0/255.0, 204.0/255.0, 0.65};
            surface.draw_line(context, round_to_int(trunk_x), round_to_int(row_y),
                              round_to_int(trunk_x), round_to_int(row_y + row_height * scale),
                              line_col);

            // Draw node circle
            if (row.is_head) {
                // Hollow target ring ◎ for HEAD commit
                const UI::Rect ring_rect{trunk_x - 5.0F * scale, node_y - 5.0F * scale, 10.0F * scale, 10.0F * scale};
                const CGFloat cyan_col[4] = {56.0/255.0, 189.0/255.0, 248.0/255.0, 1.0};
                surface.fill_rounded_rectangle(context, ring_rect, cyan_col, 5.0F * scale);
                const UI::Rect inner_rect{trunk_x - 3.0F * scale, node_y - 3.0F * scale, 6.0F * scale, 6.0F * scale};
                surface.fill_rounded_rectangle(context, inner_rect, surface.m_colors.sidebar_background, 3.0F * scale);
                const UI::Rect dot_rect{trunk_x - 1.5F * scale, node_y - 1.5F * scale, 3.0F * scale, 3.0F * scale};
                surface.fill_rounded_rectangle(context, dot_rect, cyan_col, 1.5F * scale);
            } else {
                // Solid node ● for history commits
                const UI::Rect dot_rect{trunk_x - 3.5F * scale, node_y - 3.5F * scale, 7.0F * scale, 7.0F * scale};
                const CGFloat blue_col[4] = {2.0/255.0, 132.0/255.0, 199.0/255.0, 1.0};
                surface.fill_rounded_rectangle(context, dot_rect, blue_col, 3.5F * scale);
            }

            // Commit Message Summary
            if (surface.m_small_font) {
                surface.draw_text(context, *surface.m_small_font, row.label,
                                  panel.x + 28.0F * scale, node_y,
                                  row.is_head ? "#ffffff" : surface.m_text.primary);
            }

            const int msg_w = surface.m_small_font ? surface.m_small_font->getTextWidth(row.label) : 50;

            // Author Name in muted text
            if (!row.commit_author.empty() && surface.m_small_font) {
                surface.draw_text(context, *surface.m_small_font, row.commit_author,
                                  panel.x + 34.0F * scale + static_cast<float>(msg_w),
                                  node_y,
                                  surface.m_text.muted);
            }

            // Branch Badge Pill [ ⊚ main ] on HEAD
            if (!row.commit_branch.empty() && surface.m_small_font) {
                const std::string badge_label = "⊚ " + row.commit_branch;
                const int badge_w = surface.m_small_font->getTextWidth(badge_label);
                const float pill_w = static_cast<float>(badge_w) + 12.0F * scale;
                const UI::Rect pill_bounds{panel.right() - pill_w - 24.0F * scale, row_y + 3.0F * scale, pill_w, 16.0F * scale};

                const CGFloat branch_pill_bg[4] = {0.0, 122.0/255.0, 204.0/255.0, 0.35};
                surface.fill_rounded_rectangle(context, pill_bounds, branch_pill_bg, 4.0F * scale);
                surface.draw_rectangle(context, pill_bounds, surface.m_colors.accent);
                surface.draw_text(context, *surface.m_small_font, badge_label,
                                  pill_bounds.x + 6.0F * scale, pill_bounds.y + pill_bounds.height * 0.5F,
                                  "#38bdf8");

                // Cloud icon
                surface.draw_svg_icon(context, "vscode-codicons/icons/extensions.svg",
                                      round_to_int(panel.right() - 12.0F * scale),
                                      round_to_int(node_y),
                                      round_to_int(12.0F * scale),
                                      UI::Theme::Color{192, 132, 252, 255},
                                      surface.m_palette.sidebar_background);
            }
        }
    }

    CGContextRestoreGState(context);
}

void ToolSidebar::render_search_panel(
    const StudioWorkspaceRenderer& surface,
    CGContextRef context,
    const UI::Editor::StudioEditorLayoutResult& layout) const
{
    const UI::Rect panel = layout.tool_sidebar_bounds;
    const float scale = layout.dpi_scale;

    // 1. Header: "Search" with Action Buttons (Collapse All, Clear, Refresh)
    const float header_cy = panel.y + header_height * 0.5F * scale;
    if (surface.m_ui_font) {
        surface.draw_text(context, *surface.m_ui_font, "Search",
                          panel.x + 14.0F * scale,
                          header_cy,
                          surface.m_text.primary);
    }

    const float btn_size = 18.0F * scale;

    // Collapse All Icon
    const UI::Rect collapse_rect{panel.right() - 24.0F * scale - btn_size * 2.0F, header_cy - btn_size * 0.5F, btn_size, btn_size};
    if (m_hover_search_collapse_all) {
        const CGFloat hov_rgba[4] = {1.0, 1.0, 1.0, 0.08};
        surface.fill_rounded_rectangle(context, collapse_rect, hov_rgba, 3.0F * scale);
    }
    surface.draw_svg_icon(context, "Assets/icons/collapse-all.svg",
                          round_to_int(collapse_rect.x + btn_size * 0.5F),
                          round_to_int(collapse_rect.y + btn_size * 0.5F),
                          round_to_int(13.0F * scale),
                          m_hover_search_collapse_all ? UI::Theme::Color{255, 255, 255, 255} : surface.m_palette.text_muted,
                          surface.m_palette.sidebar_background);

    // Clear Search Icon
    const UI::Rect clear_rect{panel.right() - 22.0F * scale - btn_size, header_cy - btn_size * 0.5F, btn_size, btn_size};
    if (m_hover_search_clear) {
        const CGFloat hov_rgba[4] = {1.0, 1.0, 1.0, 0.08};
        surface.fill_rounded_rectangle(context, clear_rect, hov_rgba, 3.0F * scale);
    }
    surface.draw_svg_icon(context, "Assets/icons/close-minimal.svg",
                          round_to_int(clear_rect.x + btn_size * 0.5F),
                          round_to_int(clear_rect.y + btn_size * 0.5F),
                          round_to_int(12.0F * scale),
                          m_hover_search_clear ? UI::Theme::Color{255, 255, 255, 255} : surface.m_palette.text_muted,
                          surface.m_palette.sidebar_background);

    // Refresh Icon
    const UI::Rect refresh_rect{panel.right() - 20.0F * scale, header_cy - btn_size * 0.5F, btn_size, btn_size};
    if (m_hover_search_refresh) {
        const CGFloat hov_rgba[4] = {1.0, 1.0, 1.0, 0.08};
        surface.fill_rounded_rectangle(context, refresh_rect, hov_rgba, 3.0F * scale);
    }
    surface.draw_svg_icon(context, "Assets/icons/refresh.svg",
                          round_to_int(refresh_rect.x + btn_size * 0.5F),
                          round_to_int(refresh_rect.y + btn_size * 0.5F),
                          round_to_int(12.0F * scale),
                          m_hover_search_refresh ? UI::Theme::Color{255, 255, 255, 255} : surface.m_palette.text_muted,
                          surface.m_palette.sidebar_background);

    surface.draw_line(context, round_to_int(panel.x),
                      round_to_int(panel.y + header_height * scale),
                      round_to_int(panel.right()),
                      round_to_int(panel.y + header_height * scale),
                      surface.m_colors.border);

    // 2. Search Box & Replace Row
    const float input_top = panel.y + header_height * scale + 8.0F * scale;

    // Chevron Expand/Collapse Toggle on left
    const UI::Rect chevron_bounds{panel.x + 6.0F * scale, input_top + 3.0F * scale, 18.0F * scale, 20.0F * scale};
    if (m_hover_search_chevron) {
        const CGFloat hov_rgba[4] = {1.0, 1.0, 1.0, 0.08};
        surface.fill_rounded_rectangle(context, chevron_bounds, hov_rgba, 3.0F * scale);
    }
    const std::string search_chevron = m_search_model.is_replace_expanded() ? "Assets/icons/chevron-down.svg" : "Assets/icons/chevron-right.svg";
    surface.draw_svg_icon(
        context, search_chevron,
        round_to_int(chevron_bounds.x + chevron_bounds.width * 0.5F),
        round_to_int(chevron_bounds.y + chevron_bounds.height * 0.5F),
        std::max(round_to_int(9.0F * scale), 8),
        m_hover_search_chevron ? surface.m_palette.text_primary : surface.m_palette.text_muted,
        surface.m_palette.sidebar_background);

    // Search Input Box
    const UI::Rect search_bounds{
        panel.x + 28.0F * scale,
        input_top,
        std::max(panel.width - 36.0F * scale, 0.0F),
        26.0F * scale
    };
    const bool search_focused = (m_search_model.get_focused_input() == UI::Editor::SearchInputFocus::Search);
    const bool caret_visible = (static_cast<std::uint64_t>([[NSDate date] timeIntervalSince1970] * 2) % 2) == 0;

    surface.fill_rounded_rectangle(context, search_bounds, surface.m_colors.editor_background, 4.0F * scale);
    const CGFloat* search_border_col = search_focused
        ? surface.m_colors.accent
        : surface.m_colors.border;
    surface.draw_rectangle(context, search_bounds, search_border_col);

    // Search text or placeholder
    const std::string_view query = m_search_model.get_search_query();
    if (query.empty()) {
        if (surface.m_small_font) {
            surface.draw_text(context, *surface.m_small_font, "Search (e.g. search term)",
                              search_bounds.x + 6.0F * scale,
                              search_bounds.y + search_bounds.height * 0.5F,
                              surface.m_text.muted);
        }
        if (search_focused && caret_visible) {
            surface.draw_line(context, round_to_int(search_bounds.x + 6.0F * scale),
                              round_to_int(search_bounds.y + 4.0F * scale),
                              round_to_int(search_bounds.x + 6.0F * scale),
                              round_to_int(search_bounds.bottom() - 4.0F * scale),
                              surface.m_colors.text_primary);
        }
    } else {
        if (search_focused && m_search_model.has_selection()) {
            const auto [s_min, s_max] = m_search_model.get_selection_range();
            const int w_before = surface.m_small_font ? surface.m_small_font->getTextWidth(std::string{query.substr(0, s_min)}) : 0;
            const int w_sel = surface.m_small_font ? surface.m_small_font->getTextWidth(std::string{query.substr(s_min, s_max - s_min)}) : 0;
            const UI::Rect sel_rect{
                search_bounds.x + 6.0F * scale + static_cast<float>(w_before),
                search_bounds.y + 3.0F * scale,
                static_cast<float>(w_sel),
                search_bounds.height - 6.0F * scale
            };
            const CGFloat sel_col[4] = {59.0/255.0, 130.0/255.0, 246.0/255.0, 0.40};
            surface.fill_rounded_rectangle(context, sel_rect, sel_col, 2.0F * scale);
        }

        if (surface.m_small_font) {
            surface.draw_text(context, *surface.m_small_font, std::string{query},
                              search_bounds.x + 6.0F * scale,
                              search_bounds.y + search_bounds.height * 0.5F,
                              surface.m_text.primary);
        }
        if (search_focused && caret_visible && !m_search_model.has_selection()) {
            const int text_w = surface.m_small_font ? surface.m_small_font->getTextWidth(std::string{query.substr(0, m_search_model.get_search_caret())}) : 0;
            const float caret_x = search_bounds.x + 6.0F * scale + static_cast<float>(text_w);
            surface.draw_line(context, round_to_int(caret_x),
                              round_to_int(search_bounds.y + 4.0F * scale),
                              round_to_int(caret_x),
                              round_to_int(search_bounds.bottom() - 4.0F * scale),
                              surface.m_colors.text_primary);
        }
    }

    // Toggles inside Search Box: [Aa] [ab] [.*]
    const float opt_btn_w = 20.0F * scale;
    const float opt_btn_h = 20.0F * scale;
    const float opt_btn_y = input_top + 3.0F * scale;

    // [.*] (Use Regular Expression)
    const UI::Rect regex_bounds{search_bounds.right() - 22.0F * scale, opt_btn_y, opt_btn_w, opt_btn_h};
    if (m_search_model.is_use_regex()) {
        const CGFloat active_col[4] = {59.0/255.0, 130.0/255.0, 246.0/255.0, 0.30};
        surface.fill_rounded_rectangle(context, regex_bounds, active_col, 3.0F * scale);
        surface.draw_rectangle(context, regex_bounds, surface.m_colors.accent);
    } else if (m_hover_search_use_regex) {
        const CGFloat hov_col[4] = {1.0, 1.0, 1.0, 0.08};
        surface.fill_rounded_rectangle(context, regex_bounds, hov_col, 3.0F * scale);
    }
    if (surface.m_small_font) {
        surface.draw_text(context, *surface.m_small_font, ".*",
                          regex_bounds.x + 4.0F * scale, regex_bounds.y + regex_bounds.height * 0.5F,
                          m_search_model.is_use_regex() ? "#ffffff" : (m_hover_search_use_regex ? surface.m_text.primary : surface.m_text.muted));
    }

    // [ab] (Match Whole Word)
    const UI::Rect word_bounds{search_bounds.right() - 44.0F * scale, opt_btn_y, opt_btn_w, opt_btn_h};
    if (m_search_model.is_match_word()) {
        const CGFloat active_col[4] = {59.0/255.0, 130.0/255.0, 246.0/255.0, 0.30};
        surface.fill_rounded_rectangle(context, word_bounds, active_col, 3.0F * scale);
        surface.draw_rectangle(context, word_bounds, surface.m_colors.accent);
    } else if (m_hover_search_match_word) {
        const CGFloat hov_col[4] = {1.0, 1.0, 1.0, 0.08};
        surface.fill_rounded_rectangle(context, word_bounds, hov_col, 3.0F * scale);
    }
    if (surface.m_small_font) {
        surface.draw_text(context, *surface.m_small_font, "ab",
                          word_bounds.x + 3.0F * scale, word_bounds.y + word_bounds.height * 0.5F,
                          m_search_model.is_match_word() ? "#ffffff" : (m_hover_search_match_word ? surface.m_text.primary : surface.m_text.muted));
    }

    // [Aa] (Match Case)
    const UI::Rect case_bounds{search_bounds.right() - 66.0F * scale, opt_btn_y, opt_btn_w, opt_btn_h};
    if (m_search_model.is_match_case()) {
        const CGFloat active_col[4] = {59.0/255.0, 130.0/255.0, 246.0/255.0, 0.30};
        surface.fill_rounded_rectangle(context, case_bounds, active_col, 3.0F * scale);
        surface.draw_rectangle(context, case_bounds, surface.m_colors.accent);
    } else if (m_hover_search_match_case) {
        const CGFloat hov_col[4] = {1.0, 1.0, 1.0, 0.08};
        surface.fill_rounded_rectangle(context, case_bounds, hov_col, 3.0F * scale);
    }
    if (surface.m_small_font) {
        surface.draw_text(context, *surface.m_small_font, "Aa",
                          case_bounds.x + 3.0F * scale, case_bounds.y + case_bounds.height * 0.5F,
                          m_search_model.is_match_case() ? "#ffffff" : (m_hover_search_match_case ? surface.m_text.primary : surface.m_text.muted));
    }

    // Replace Row (when expanded)
    if (m_search_model.is_replace_expanded()) {
        const float replace_top = input_top + 30.0F * scale;
        const UI::Rect replace_bounds{
            panel.x + 28.0F * scale,
            replace_top,
            std::max(panel.width - 64.0F * scale, 0.0F),
            26.0F * scale
        };
        const bool replace_focused = (m_search_model.get_focused_input() == UI::Editor::SearchInputFocus::Replace);
        surface.fill_rounded_rectangle(context, replace_bounds, surface.m_colors.editor_background, 4.0F * scale);
        const CGFloat* replace_border_col = replace_focused
            ? surface.m_colors.accent
            : surface.m_colors.border;
        surface.draw_rectangle(context, replace_bounds, replace_border_col);

        const std::string_view replace_query = m_search_model.get_replace_query();
        if (replace_query.empty()) {
            if (surface.m_small_font) {
                surface.draw_text(context, *surface.m_small_font, "Replace",
                                  replace_bounds.x + 6.0F * scale,
                                  replace_bounds.y + replace_bounds.height * 0.5F,
                                  surface.m_text.muted);
            }
            if (replace_focused && caret_visible) {
                surface.draw_line(context, round_to_int(replace_bounds.x + 6.0F * scale),
                                  round_to_int(replace_bounds.y + 4.0F * scale),
                                  round_to_int(replace_bounds.x + 6.0F * scale),
                                  round_to_int(replace_bounds.bottom() - 4.0F * scale),
                                  surface.m_colors.text_primary);
            }
        } else {
            if (replace_focused && m_search_model.has_selection()) {
                const auto [s_min, s_max] = m_search_model.get_selection_range();
                const int w_before = surface.m_small_font ? surface.m_small_font->getTextWidth(std::string{replace_query.substr(0, s_min)}) : 0;
                const int w_sel = surface.m_small_font ? surface.m_small_font->getTextWidth(std::string{replace_query.substr(s_min, s_max - s_min)}) : 0;
                const UI::Rect sel_rect{
                    replace_bounds.x + 6.0F * scale + static_cast<float>(w_before),
                    replace_top + 3.0F * scale,
                    static_cast<float>(w_sel),
                    replace_bounds.height - 6.0F * scale
                };
                const CGFloat sel_col[4] = {59.0/255.0, 130.0/255.0, 246.0/255.0, 0.40};
                surface.fill_rounded_rectangle(context, sel_rect, sel_col, 2.0F * scale);
            }

            if (surface.m_small_font) {
                surface.draw_text(context, *surface.m_small_font, std::string{replace_query},
                                  replace_bounds.x + 6.0F * scale,
                                  replace_bounds.y + replace_bounds.height * 0.5F,
                                  surface.m_text.primary);
            }
            if (replace_focused && caret_visible && !m_search_model.has_selection()) {
                const int text_w = surface.m_small_font ? surface.m_small_font->getTextWidth(std::string{replace_query.substr(0, m_search_model.get_replace_caret())}) : 0;
                const float caret_x = replace_bounds.x + 6.0F * scale + static_cast<float>(text_w);
                surface.draw_line(context, round_to_int(caret_x),
                                  round_to_int(replace_bounds.y + 4.0F * scale),
                                  round_to_int(caret_x),
                                  round_to_int(replace_bounds.bottom() - 4.0F * scale),
                                  surface.m_colors.text_primary);
            }
        }

        // [AB] (Preserve Case)
        const UI::Rect preserve_bounds{replace_bounds.right() - 22.0F * scale, replace_top + 3.0F * scale, opt_btn_w, opt_btn_h};
        if (m_search_model.is_preserve_case()) {
            const CGFloat active_col[4] = {59.0/255.0, 130.0/255.0, 246.0/255.0, 0.30};
            surface.fill_rounded_rectangle(context, preserve_bounds, active_col, 3.0F * scale);
            surface.draw_rectangle(context, preserve_bounds, surface.m_colors.accent);
        } else if (m_hover_search_preserve_case) {
            const CGFloat hov_col[4] = {1.0, 1.0, 1.0, 0.08};
            surface.fill_rounded_rectangle(context, preserve_bounds, hov_col, 3.0F * scale);
        }
        if (surface.m_small_font) {
            surface.draw_text(context, *surface.m_small_font, "AB",
                              preserve_bounds.x + 3.0F * scale, preserve_bounds.y + preserve_bounds.height * 0.5F,
                              m_search_model.is_preserve_case() ? "#ffffff" : (m_hover_search_preserve_case ? surface.m_text.primary : surface.m_text.muted));
        }

        // Replace All Action Button
        const UI::Rect replace_all_bounds{panel.right() - 32.0F * scale, replace_top + 2.0F * scale, 22.0F * scale, 22.0F * scale};
        if (m_hover_search_replace_all) {
            const CGFloat hov_col[4] = {1.0, 1.0, 1.0, 0.08};
            surface.fill_rounded_rectangle(context, replace_all_bounds, hov_col, 3.0F * scale);
        }
        surface.draw_svg_icon(context, "Assets/icons/refresh.svg",
                              round_to_int(replace_all_bounds.x + 11.0F * scale),
                              round_to_int(replace_all_bounds.y + 11.0F * scale),
                              round_to_int(13.0F * scale),
                              m_hover_search_replace_all ? UI::Theme::Color{255, 255, 255, 255} : surface.m_palette.text_muted,
                              surface.m_palette.sidebar_background);
    }

    // 3. Results Summary Line
    const float summary_top = (m_search_model.is_replace_expanded() ? (input_top + 30.0F * scale + 30.0F * scale) : (input_top + 30.0F * scale)) + 4.0F * scale;

    if (!m_search_model.get_search_error().empty()) {
        if (surface.m_small_font) {
            surface.draw_text(context, *surface.m_small_font,
                              "Regex Error: " + m_search_model.get_search_error(),
                              panel.x + 14.0F * scale, summary_top + 10.0F * scale,
                              "#f87171");
        }
    } else if (!m_search_model.get_search_query().empty()) {
        std::string summary_text = std::to_string(m_search_model.get_total_match_count()) +
                                   " results in " +
                                   std::to_string(m_search_model.get_total_file_count()) + " files";
        if (surface.m_small_font) {
            surface.draw_text(context, *surface.m_small_font, summary_text,
                              panel.x + 14.0F * scale, summary_top + 10.0F * scale,
                              surface.m_text.primary);
        }
    }

    // 4. Tree Results View
    const float tree_top = search_tree_top_y(layout);
    const auto visible_rows = m_search_model.get_visible_rows();
    const auto& results = m_search_model.get_results();

    if (visible_rows.empty()) {
        if (m_search_model.get_search_query().empty()) {
            if (surface.m_ui_font) {
                surface.draw_text(context, *surface.m_ui_font, "Search across workspace",
                                  panel.x + 14.0F * scale, tree_top + 20.0F * scale,
                                  surface.m_text.primary);
            }
            if (surface.m_small_font) {
                surface.draw_text(context, *surface.m_small_font, "Search results will appear in this panel.",
                                  panel.x + 14.0F * scale, tree_top + 42.0F * scale,
                                  surface.m_text.muted);
            }
        } else if (!m_search_model.is_searching()) {
            if (surface.m_small_font) {
                surface.draw_text(context, *surface.m_small_font, "No results found.",
                                  panel.x + 14.0F * scale, tree_top + 14.0F * scale,
                                  surface.m_text.muted);
            }
        }
        return;
    }

    const std::size_t first = m_search_model.get_scroll_offset();
    const std::size_t row_count = search_viewport_row_count(layout);
    const std::size_t end = std::min(visible_rows.size(), first + row_count + 1);

    CGContextSaveGState(context);
    CGRect clip_rect = CGRectMake(panel.x, tree_top, panel.width, std::max(panel.bottom() - tree_top, 0.0F));
    CGContextClipToRect(context, clip_rect);

    for (std::size_t item_index = first; item_index < end; ++item_index) {
        const std::size_t visible_row = item_index - first;
        const auto& v_row = visible_rows[item_index];
        const float row_y = tree_top + static_cast<float>(visible_row) * row_height * scale;
        const bool is_hovered = (m_hovered_search_row && *m_hovered_search_row == item_index);

        if (is_hovered) {
            const CGFloat hov_rgba[4] = {1.0, 1.0, 1.0, 0.06};
            surface.fill_rounded_rectangle(context,
                UI::Rect{panel.x + 4.0F * scale, row_y + 1.0F * scale, panel.width - 8.0F * scale, row_height * scale - 2.0F * scale},
                hov_rgba, 3.0F * scale);
        }

        if (v_row.kind == UI::Editor::SearchRowKind::FileHeader) {
            const auto& file = results[v_row.file_index];

            const std::string file_chevron = file.expanded ? "Assets/icons/chevron-down.svg" : "Assets/icons/chevron-right.svg";
            surface.draw_svg_icon(context, file_chevron,
                                  round_to_int(panel.x + 12.0F * scale),
                                  round_to_int(row_y + row_height * 0.5F * scale),
                                  std::max(round_to_int(9.0F * scale), 8),
                                  surface.m_palette.text_muted,
                                  surface.m_palette.sidebar_background);

            const std::string icon_asset = UI::Editor::file_icon_asset_for_path(file.file_path);
            surface.draw_svg_icon(context, "Assets/icons/" + icon_asset,
                                  round_to_int(panel.x + 24.0F * scale),
                                  round_to_int(row_y + row_height * 0.5F * scale),
                                  round_to_int(14.0F * scale),
                                  surface.m_palette.text_primary,
                                  surface.m_palette.sidebar_background,
                                  true);

            if (surface.m_small_font) {
                surface.draw_text(context, *surface.m_small_font, file.file_name,
                                  panel.x + 36.0F * scale, row_y + row_height * 0.5F * scale,
                                  surface.m_text.primary);
            }

            const int fname_w = surface.m_small_font ? surface.m_small_font->getTextWidth(file.file_name) : 30;

            if (!file.relative_dir.empty()) {
                const float dir_x = panel.x + 42.0F * scale + static_cast<float>(fname_w);
                const int avail_dir_w = round_to_int(panel.right() - dir_x - 38.0F * scale);
                if (avail_dir_w > 20 && surface.m_small_font) {
                    const std::string dir_text = ellipsize(*surface.m_small_font, file.relative_dir, avail_dir_w);
                    surface.draw_text(context, *surface.m_small_font, dir_text,
                                      dir_x, row_y + row_height * 0.5F * scale,
                                      surface.m_text.muted);
                }
            }

            const std::string count_str = std::to_string(file.matches.size());
            const int count_w = surface.m_small_font ? surface.m_small_font->getTextWidth(count_str) : 12;
            const float pill_w = std::max(static_cast<float>(count_w) + 8.0F * scale, 18.0F * scale);
            const UI::Rect pill_bounds{panel.right() - pill_w - 8.0F * scale, row_y + 2.0F * scale, pill_w, 18.0F * scale};

            const CGFloat badge_rgba[4] = {1.0, 1.0, 1.0, 0.12};
            surface.fill_rounded_rectangle(context, pill_bounds, badge_rgba, 9.0F * scale);
            if (surface.m_small_font) {
                surface.draw_text(context, *surface.m_small_font, count_str,
                                  pill_bounds.x + (pill_w - static_cast<float>(count_w)) * 0.5F,
                                  pill_bounds.y + pill_bounds.height * 0.5F,
                                  surface.m_text.muted);
            }
        } else {
            const auto& file = results[v_row.file_index];
            const auto& match = file.matches[v_row.match_index];

            const std::string line_str = std::to_string(match.line_number);
            const int line_w = surface.m_small_font ? surface.m_small_font->getTextWidth(line_str) : 16;
            if (surface.m_small_font) {
                surface.draw_text(context, *surface.m_small_font, line_str,
                                  panel.x + 32.0F * scale, row_y + row_height * 0.5F * scale,
                                  surface.m_text.muted);
            }

            const float code_x = panel.x + 36.0F * scale + static_cast<float>(line_w) + 8.0F * scale;

            for (const auto& span : match.spans) {
                if (span.start < match.line_content.size() && surface.m_small_font) {
                    const int w_before = surface.m_small_font->getTextWidth(match.line_content.substr(0, span.start));
                    const int w_match = surface.m_small_font->getTextWidth(match.line_content.substr(span.start, span.length));
                    const UI::Rect hl_rect{
                        code_x + static_cast<float>(w_before),
                        row_y + 2.0F * scale,
                        static_cast<float>(w_match),
                        row_height * scale - 4.0F * scale
                    };
                    const CGFloat hl_col[4] = {234.0/255.0, 179.0/255.0, 8.0/255.0, 0.35};
                    surface.fill_rounded_rectangle(context, hl_rect, hl_col, 2.0F * scale);
                }
            }

            if (surface.m_small_font) {
                surface.draw_text(context, *surface.m_small_font, match.line_content,
                                  code_x, row_y + row_height * 0.5F * scale,
                                  surface.m_text.primary);
            }
        }
    }

    CGContextRestoreGState(context);
}

void ToolSidebar::render(
    const StudioWorkspaceRenderer& surface,
    CGContextRef context,
    const UI::Editor::StudioEditorLayoutResult& layout) const
{
    if (!is_visible()) return;

    const float scale = layout.dpi_scale;
    const UI::Rect panel = layout.tool_sidebar_bounds;

    // Background
    surface.fill_rectangle(context, panel, surface.m_colors.sidebar_background);

    if (m_model.get_active_icon() == UI::Editor::SidebarIcon::Search)
    {
        render_search_panel(surface, context, layout);
    }
    else if (m_model.get_active_icon() == UI::Editor::SidebarIcon::VersionControl)
    {
        render_source_control_panel(surface, context, layout);
    }
    else if (m_model.get_active_icon() == UI::Editor::SidebarIcon::Project)
    {
        // Project Header
        m_explorer_header.render(surface, context, layout, std::string{m_model.get_title()});

        // Content
        const std::span<const UI::Editor::ProjectTreeItem> items = m_model.get_project_items();
        const std::size_t first = m_model.get_scroll_offset();
        const std::size_t row_count = viewport_row_count(layout);
        const std::size_t end = std::min(items.size(), first + row_count + 1);
        const float tree_top = panel.y + header_height * scale;

        // Clip tree view strictly to sidebar bounds
        CGContextSaveGState(context);
        CGRect clip_rect = CGRectMake(panel.x, tree_top, panel.width, std::max(panel.bottom() - tree_top, 0.0F));
        CGContextClipToRect(context, clip_rect);

        for (std::size_t item_index = first; item_index < end; ++item_index)
        {
            const std::size_t visible_row = item_index - first;
            const UI::Editor::ProjectTreeItem& item = items[item_index];
            const UI::Rect row_bounds{
                panel.x,
                tree_top + static_cast<float>(visible_row) * row_height * scale,
                panel.width,
                row_height * scale,
            };
            const bool is_selected = m_model.is_selected(item.path);
            const bool is_hovered = (m_hovered_row.has_value() && *m_hovered_row == item_index);
            const bool is_drag_source = m_is_dragging_item && m_drag_source_row.has_value() && *m_drag_source_row == visible_row;
            const bool is_drop_target = m_is_dragging_item && m_drag_target_row.has_value() && *m_drag_target_row == visible_row;

            if (is_drop_target) {
                const CGFloat drop_rgba[4] = {1.0, 1.0, 1.0, 0.14};
                surface.fill_rectangle(context, row_bounds, drop_rgba);
                surface.draw_rectangle(context, row_bounds, drop_rgba);
            } else if (is_drag_source) {
                const CGFloat drag_rgba[4] = {1.0, 1.0, 1.0, 0.06};
                surface.fill_rectangle(context, row_bounds, drag_rgba);
            } else if (is_selected) {
                surface.fill_rectangle(context, row_bounds, surface.m_colors.tab_active_background);
                const UI::Rect left_bar{
                    panel.x, row_bounds.y + 2.0F * scale,
                    3.0F * scale, row_bounds.height - 4.0F * scale
                };
                surface.fill_rectangle(context, left_bar, surface.m_colors.accent);
            } else if (is_hovered) {
                surface.fill_rectangle(context, row_bounds, surface.m_colors.hover_background);
            }

            const float indent_x = panel.x + (10.0F + static_cast<float>(item.depth) * 16.0F) * scale;
            const int guide_y = round_to_int(row_bounds.y + row_bounds.height * 0.5F);

            // Indentation guides
            for (std::size_t level = 0; level < item.depth; ++level) {
                const int guide_x = round_to_int(panel.x + (17.0F + static_cast<float>(level) * 16.0F) * scale);
                surface.draw_line(context, guide_x, round_to_int(row_bounds.y), guide_x, round_to_int(row_bounds.bottom()), surface.m_colors.border);
            }

            // Chevron
            if (item.directory) {
                const std::string chevron_icon = item.expanded ? "Assets/icons/chevron-down.svg" : "Assets/icons/chevron-right.svg";
                surface.draw_svg_icon(context, chevron_icon,
                    round_to_int(indent_x), guide_y,
                    std::max(round_to_int(10.0F * scale), 8),
                    surface.m_palette.text_muted, surface.m_palette.sidebar_background);
            }

            // File/Folder icon
            const int icon_x = round_to_int(indent_x + 14.0F * scale);
            if (item.directory) {
                surface.draw_svg_icon(context, "Assets/icons/folder.svg", icon_x, guide_y,
                    std::max(round_to_int(14.0F * scale), 12),
                    surface.m_palette.text_primary, surface.m_palette.sidebar_background);
            } else {
                const std::string icon_asset = UI::Editor::file_icon_asset_for_path(item.path);
                surface.draw_svg_icon(context, "Assets/icons/" + icon_asset, icon_x, guide_y,
                    std::max(round_to_int(14.0F * scale), 12),
                    surface.m_palette.text_primary, surface.m_palette.sidebar_background,
                    true);
            }

            // Label
            if (surface.m_small_font) {
                surface.draw_text(context, *surface.m_small_font, item.label,
                    indent_x + 28.0F * scale,
                    row_bounds.y + row_bounds.height * 0.5F,
                    is_selected ? surface.m_text.primary : (is_hovered ? surface.m_text.primary : surface.m_text.primary));
            }
        }

        // Sticky Explorer Headers
        const auto sticky_indices = get_sticky_items();
        for (std::size_t i = 0; i < sticky_indices.size(); ++i) {
            const std::size_t item_index = sticky_indices[i];
            if (item_index >= items.size()) continue;

            const UI::Editor::ProjectTreeItem &item = items[item_index];
            const float sticky_y = tree_top + static_cast<float>(i) * row_height * scale;
            const UI::Rect sticky_bounds{
                panel.x,
                sticky_y,
                panel.width,
                row_height * scale,
            };

            const bool is_sticky_hovered = (m_hovered_sticky_index && *m_hovered_sticky_index == item_index);
            const CGFloat* sticky_bg = is_sticky_hovered 
                ? surface.m_colors.hover_background 
                : surface.m_colors.sidebar_background;

            surface.fill_rectangle(context, sticky_bounds, sticky_bg);
            if (i == sticky_indices.size() - 1) {
                surface.draw_line(context, round_to_int(panel.x), round_to_int(sticky_bounds.bottom()),
                                  round_to_int(panel.right()), round_to_int(sticky_bounds.bottom()),
                                  surface.m_colors.border);
            }

            const float indent_x = panel.x + (10.0F + static_cast<float>(item.depth) * 16.0F) * scale;
            const int guide_y = round_to_int(sticky_bounds.y + row_height * 0.5F * scale);

            // Chevron
            surface.draw_svg_icon(
                context, "Assets/icons/chevron-down.svg",
                round_to_int(indent_x),
                guide_y,
                std::max(round_to_int(10.0F * scale), 8),
                surface.m_palette.text_muted,
                is_sticky_hovered ? surface.m_palette.hover_background : surface.m_palette.sidebar_background);

            // Folder icon
            const int folder_x = round_to_int(indent_x + 14.0F * scale);
            surface.draw_svg_icon(
                context, "Assets/icons/folder.svg",
                folder_x,
                guide_y,
                std::max(round_to_int(14.0F * scale), 12),
                surface.m_palette.text_primary,
                is_sticky_hovered ? surface.m_palette.hover_background : surface.m_palette.sidebar_background);

            // Label
            if (surface.m_small_font) {
                const float label_x = indent_x + 28.0F * scale;
                const std::string label = ellipsize(*surface.m_small_font, item.label,
                    std::max(round_to_int(panel.right() - label_x - 8.0F * scale), 1));
                surface.draw_text(context, *surface.m_small_font, label,
                    label_x,
                    guide_y,
                    surface.m_text.primary);
            }
        }

        CGContextRestoreGState(context);

        // Scrollbar
        if (items.size() > row_count) {
            const UI::Rect track = scrollbar_bounds(layout);
            const float visible_ratio = std::min(static_cast<float>(row_count) / static_cast<float>(items.size()), 1.0F);
            const float thumb_height = std::max(track.height * visible_ratio, 20.0F * scale);
            const float max_offset = static_cast<float>(items.size() - row_count);
            const float progress = max_offset > 0.0F ? static_cast<float>(first) / max_offset : 0.0F;

            const UI::Rect thumb{
                track.x + track.width * 0.25F,
                track.y + progress * std::max(track.height - thumb_height, 0.0F),
                std::max(track.width * 0.5F, 1.0F),
                std::min(thumb_height, track.height),
            };

            CGFloat thumb_rgba[4];
            StudioWorkspaceRenderer::color_to_rgba(m_hovered_scrollbar ? surface.m_palette.accent : surface.m_palette.text_muted, thumb_rgba);
            surface.fill_rectangle(context, thumb, thumb_rgba);
        }

        // Ghost drag preview
        if (m_is_dragging_item && m_drag_source_row.has_value() && *m_drag_source_row < items.size()) {
            const auto& dragged = items[*m_drag_source_row];
            const std::string badge_label = dragged.label;
            const int text_w = surface.m_small_font ? surface.m_small_font->getTextWidth(badge_label) : 40;
            const float badge_w = static_cast<float>(text_w) + 36.0F * scale;
            const float badge_h = 24.0F * scale;
            const UI::Rect badge_rect{m_drag_current_x + 12.0F * scale, m_drag_current_y + 12.0F * scale, badge_w, badge_h};

            const UI::Rect shadow_rect{badge_rect.x + 2.0F * scale, badge_rect.y + 3.0F * scale, badge_w, badge_h};
            const CGFloat shadow_col[4] = {0.0, 0.0, 0.0, 0.40};
            surface.fill_rounded_rectangle(context, shadow_rect, shadow_col, 5.0F * scale);

            const CGFloat badge_bg[4] = {24.0/255.0, 25.0/255.0, 28.0/255.0, 0.95};
            const CGFloat badge_border[4] = {53.0/255.0, 132.0/255.0, 228.0/255.0, 0.90};
            surface.fill_rounded_rectangle(context, badge_rect, badge_bg, 5.0F * scale);
            surface.draw_rectangle(context, badge_rect, badge_border);

            const int badge_icon_x = round_to_int(badge_rect.x + 12.0F * scale);
            const int badge_icon_y = round_to_int(badge_rect.y + badge_h * 0.5F);
            if (dragged.directory) {
                surface.draw_svg_icon(context, "Assets/icons/folder.svg", badge_icon_x, badge_icon_y,
                    std::max(round_to_int(12.0F * scale), 10),
                    UI::Theme::Color{255, 255, 255, 255}, UI::Theme::Color{24, 25, 28, 255});
            } else {
                const std::string icon_asset = UI::Editor::file_icon_asset_for_path(dragged.path);
                surface.draw_svg_icon(context, "Assets/icons/" + icon_asset, badge_icon_x, badge_icon_y,
                    std::max(round_to_int(12.0F * scale), 10),
                    UI::Theme::Color{255, 255, 255, 255}, UI::Theme::Color{24, 25, 28, 255},
                    true);
            }

            if (surface.m_small_font) {
                surface.draw_text(context, *surface.m_small_font, badge_label,
                    badge_rect.x + 22.0F * scale,
                    badge_rect.y + badge_rect.height * 0.5F,
                    "#ffffff");
            }
        }
    }
    else
    {
        // Non-Project Panel Header
        const std::string title_str{m_model.get_title()};
        const float header_cy = panel.y + header_height * 0.5F * scale;
        if (surface.m_ui_font) {
            surface.draw_text(context, *surface.m_ui_font, title_str,
                              panel.x + 14.0F * scale,
                              header_cy,
                              surface.m_text.primary);
        }

        const int more_center_x = round_to_int(panel.right() - 17.0F * scale);
        surface.draw_svg_icon(
            context, "Assets/icons/ellipsis.svg", more_center_x, round_to_int(header_cy),
            std::max(round_to_int(15.0F * scale), 11), surface.m_palette.text_muted,
            surface.m_palette.sidebar_background);
        surface.draw_line(context, round_to_int(panel.x),
                          round_to_int(panel.y + header_height * scale),
                          round_to_int(panel.right()),
                          round_to_int(panel.y + header_height * scale),
                          surface.m_colors.border);

        // Content Area
        const float content_y = panel.y + (header_height + 22.0F) * scale;
        if (surface.m_ui_font) {
            surface.draw_text(context, *surface.m_ui_font,
                              m_model.get_content_heading(),
                              panel.x + 14.0F * scale,
                              content_y,
                              surface.m_text.primary);
        }
        if (surface.m_small_font) {
            surface.draw_text(context, *surface.m_small_font,
                              m_model.get_content_detail(),
                              panel.x + 14.0F * scale,
                              content_y + 20.0F * scale,
                              surface.m_text.muted);
        }
    }

    // Border (matches X11/Win32: accent when resize-hovered or resizing)
    const bool resize_active = m_resize_hovered || m_resizing;
    const auto& border_color = resize_active ? surface.m_colors.accent
                                             : surface.m_colors.border;
    surface.draw_line(context,
        round_to_int(layout.tool_sidebar_bounds.right() - scale),
        round_to_int(layout.tool_sidebar_bounds.y),
        round_to_int(layout.tool_sidebar_bounds.right() - scale),
        round_to_int(layout.tool_sidebar_bounds.bottom()),
        border_color);
    if (resize_active) {
        CGFloat accent_rgba[4];
        StudioWorkspaceRenderer::color_to_rgba(surface.m_palette.accent, accent_rgba);
        surface.fill_rectangle(context,
            UI::Rect{
                layout.tool_sidebar_bounds.right() - scale - scale,
                layout.tool_sidebar_bounds.y,
                std::max(2.0F * scale, 2.0F),
                layout.tool_sidebar_bounds.height},
            accent_rgba);
    }
}

} // namespace Zenvra::Platform::Cocoa::Components
