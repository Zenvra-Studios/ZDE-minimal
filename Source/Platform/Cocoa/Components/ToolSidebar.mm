#include "Platform/Cocoa/Components/ToolSidebar.h"
#include "Platform/Cocoa/Components/StudioWorkspaceRenderer.h"
#include "Utility/Fonts.h"
#include "UI/Editor/FileIconModel.h"

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
bool ToolSidebar::set_workspace_root(const std::filesystem::path& root) { return m_model.initialize(root); }
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
    std::optional<std::filesystem::path>& file_to_open)
{
    if (!is_visible() || !contains(layout, px, py)) return false;

    if (m_model.get_active_icon() == UI::Editor::SidebarIcon::Project)
    {
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
                    m_model.activate_project_item(*row);
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

    // Pointer left the sidebar (e.g. moved into the text editor):
    // clear every hover state so stale highlights do not persist.
    if (!layout.tool_sidebar_bounds.contains(px, py)) {
        bool changed = m_hovered_row.has_value() || m_hovered_icon.has_value()
                       || m_resize_hovered;
        m_hovered_row.reset();
        m_hovered_icon.reset();
        m_resize_hovered = false;
        changed |= m_explorer_header.handle_pointer_move(layout, px, py);
        return changed;
    }

    bool changed = false;
    if (m_model.get_active_icon() == UI::Editor::SidebarIcon::Project)
    {
        auto new_row = row_from_point(layout, py);
        changed |= new_row != m_hovered_row;
        m_hovered_row = new_row;
        changed |= m_explorer_header.handle_pointer_move(layout, px, py);
    }
    else
    {
        changed |= m_hovered_row.has_value();
        m_hovered_row.reset();
    }

    // Check sidebar icon hover
    const std::span<const UI::Editor::SidebarItem> items = UI::Editor::get_studio_sidebar_items();
    std::optional<UI::Editor::SidebarIcon> new_icon;
    if (auto index = UI::Editor::hit_test_studio_sidebar(layout, px, py)) {
        new_icon = items[*index].icon;
    }
    changed |= new_icon != m_hovered_icon;
    m_hovered_icon = new_icon;
    // Track resize handle hover (matches X11/Win32 border accent)
    const bool next_resize_hovered = is_resize_handle_point(layout, px, py);
    changed |= next_resize_hovered != m_resize_hovered;
    m_resize_hovered = next_resize_hovered;
    return changed;
}

bool ToolSidebar::handle_scroll(
    const UI::Editor::StudioEditorLayoutResult& layout, std::ptrdiff_t delta) noexcept
{
    return m_model.scroll(delta, viewport_row_count(layout));
}

bool ToolSidebar::handle_pointer_drag(
    const UI::Editor::StudioEditorLayoutResult& layout, float px, float py) noexcept
{
    if (m_resizing) {
        m_width = std::max(120.0F, m_drag_start_width + (px - m_drag_start_x));
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

std::size_t ToolSidebar::viewport_row_count(
    const UI::Editor::StudioEditorLayoutResult& layout) const noexcept
{
    const float available = std::max(layout.tool_sidebar_bounds.height -
                                         header_height * layout.dpi_scale,
                                     0.0F);
    return static_cast<std::size_t>(
        std::max(std::floor(available / (row_height * layout.dpi_scale)), 0.0F));
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

    // Check sticky header rows first so clicking a sticky header toggles that folder
    const auto sticky = get_sticky_items();
    if (!sticky.empty() && relative_y < static_cast<float>(sticky.size()) * row_h) {
        const std::size_t sticky_idx = static_cast<std::size_t>(relative_y / row_h);
        if (sticky_idx < sticky.size()) {
            return sticky[sticky_idx];
        }
    }

    return static_cast<std::size_t>(relative_y / row_h) +
           m_model.get_scroll_offset();
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
    const auto items = m_model.get_project_items();
    const std::size_t first = m_model.get_scroll_offset();
    if (first > 0 && first < items.size()) {
        std::size_t current_depth = items[first].depth;
        std::size_t search = first - 1;
        while (current_depth > 0) {
            if (items[search].depth < current_depth) {
                sticky.push_back(search);
                current_depth = items[search].depth;
            }
            if (search == 0) break;
            --search;
        }
        std::reverse(sticky.begin(), sticky.end());
    }
    return sticky;
}
void ToolSidebar::render(
    const StudioWorkspaceRenderer& surface,
    CGContextRef context,
    const UI::Editor::StudioEditorLayoutResult& layout) const
{
    if (!is_visible() || layout.tool_sidebar_bounds.is_empty()) return;

    const UI::Rect panel = layout.tool_sidebar_bounds;
    const float scale = surface.m_dpi_scale;

    surface.fill_rectangle(context, panel, surface.m_colors.sidebar_background);

    if (m_model.get_active_icon() == UI::Editor::SidebarIcon::Project)
    {
        // Explorer Header
        const std::string title = std::string{m_model.get_title()};
        m_explorer_header.render(surface, context, layout, title);

        // File tree
        const auto& items = m_model.get_project_items();
        const float content_top = layout.tool_sidebar_bounds.y + header_height * scale;
        const std::size_t scroll = m_model.get_scroll_offset();
        const std::size_t max_rows = viewport_row_count(layout);

        surface.push_clip(context, UI::Rect{
            layout.tool_sidebar_bounds.x, content_top,
            layout.tool_sidebar_bounds.width,
            std::max(0.0F, layout.tool_sidebar_bounds.height - header_height * scale)});

        for (std::size_t i = 0; i < max_rows; ++i) {
            const std::size_t item_index = scroll + i;
            if (item_index >= items.size()) break;
            const auto& item = items[item_index];
            const float y = content_top + static_cast<float>(i) * row_height * scale;
            const UI::Rect row_bounds{
                layout.tool_sidebar_bounds.x, y,
                layout.tool_sidebar_bounds.width, row_height * scale
            };
            const bool row_hovered = m_hovered_row == item_index;
            const bool is_selected = m_model.is_selected(item.path);
            const bool is_drag_source = m_is_dragging_item && m_drag_source_row.has_value() && *m_drag_source_row == item_index;
            const bool is_drop_target = m_is_dragging_item && m_drag_target_row.has_value() && *m_drag_target_row == item_index;

            const UI::Rect highlight_rect{
                layout.tool_sidebar_bounds.x + 6.0F * scale,
                y + 1.0F * scale,
                layout.tool_sidebar_bounds.width - 12.0F * scale,
                row_height * scale - 2.0F * scale
            };

            // Selection & Drag Highlight (matching X11 and Win32)
            if (is_drop_target) {
                surface.fill_rounded_rectangle(context, highlight_rect, surface.m_colors.tab_active_background, 4.0F * scale);
                surface.draw_rectangle(context, highlight_rect, surface.m_colors.accent);
            } else if (is_drag_source) {
                surface.fill_rounded_rectangle(context, highlight_rect, surface.m_colors.hover_background, 4.0F * scale);
            } else if (is_selected) {
                surface.fill_rounded_rectangle(context, highlight_rect, surface.m_colors.tab_active_background, 4.0F * scale);

                const UI::Rect left_bar{
                    highlight_rect.x, highlight_rect.y + 2.0F * scale,
                    2.5F * scale, highlight_rect.height - 4.0F * scale
                };
                surface.fill_rounded_rectangle(context, left_bar, surface.m_colors.accent, 1.25F * scale);
            } else if (row_hovered) {
                surface.fill_rounded_rectangle(context, highlight_rect, surface.m_colors.hover_background, 4.0F * scale);
            }

            // Tree indent guides + connectors
            const float indent_x =
                layout.tool_sidebar_bounds.x + (10.0F + static_cast<float>(item.depth) * 16.0F) * scale;
            const int guide_y = round_to_int(row_bounds.y + row_bounds.height * 0.5F);
            for (std::size_t level = 0; level < item.depth; ++level) {
                const int guide_x = round_to_int(
                    layout.tool_sidebar_bounds.x + (17.0F + static_cast<float>(level) * 16.0F) * scale);

                bool line_active = false;
                for (std::size_t next = item_index + 1; next < items.size(); ++next) {
                    if (items[next].depth <= level + 1) {
                        line_active = (items[next].depth == level + 1);
                        break;
                    }
                }

                if (level == item.depth - 1) {
                    surface.draw_line(
                        context, guide_x, round_to_int(row_bounds.y), guide_x,
                        line_active ? round_to_int(row_bounds.bottom()) : guide_y,
                        surface.m_colors.border);
                    surface.draw_line(
                        context, guide_x, guide_y,
                        round_to_int(indent_x + 3.0F * scale), guide_y,
                        surface.m_colors.border);
                } else if (line_active) {
                    surface.draw_line(
                        context, guide_x, round_to_int(row_bounds.y), guide_x,
                        round_to_int(row_bounds.bottom()),
                        surface.m_colors.border);
                }
            }

            // Folder chevron or file icon
            const float icon_x = indent_x + 4.0F * scale;
            const float center_y = row_bounds.y + row_bounds.height * 0.5F;
            const int folder_size = std::max(round_to_int(16.0F * scale), 14);
            const int file_icon_size = std::max(round_to_int(16.0F * scale), 14);
            const int chevron_size = std::max(round_to_int(10.0F * scale), 8);
            const bool highlight_text = is_selected || is_drop_target;

            if (item.directory) {
                const std::string chevron = item.expanded ? "chevron-down.svg" : "chevron-right.svg";
                surface.draw_svg_icon(
                    context, "Assets/icons/" + chevron,
                    round_to_int(icon_x), round_to_int(center_y),
                    chevron_size,
                    highlight_text ? surface.m_palette.text_primary : surface.m_palette.text_muted,
                    highlight_text ? surface.m_palette.accent : surface.m_palette.sidebar_background);

                const std::string folder_icon = item.expanded ? "folder-open.svg" : "folder.svg";
                surface.draw_svg_icon(
                    context, "Assets/icons/" + folder_icon,
                    round_to_int(icon_x + 15.0F * scale), round_to_int(center_y),
                    folder_size,
                    highlight_text ? surface.m_palette.text_primary : surface.m_palette.text_muted,
                    highlight_text ? surface.m_palette.accent : surface.m_palette.sidebar_background);
            } else {
                const std::string icon_asset = UI::Editor::file_icon_asset_for_path(item.path);
                surface.draw_svg_icon(
                    context, "Assets/icons/" + icon_asset,
                    round_to_int(icon_x + 14.0F * scale), round_to_int(center_y),
                    file_icon_size,
                    surface.m_palette.text_primary,
                    highlight_text ? surface.m_palette.accent : surface.m_palette.sidebar_background,
                    true);
            }

            // Label
            const float label_x = icon_x + (item.directory ? 32.0F : 30.0F) * scale;
            const float available_width =
                layout.tool_sidebar_bounds.right() - label_x - 10.0F * scale;
            if (available_width > 0.0F && surface.m_ui_font) {
                const std::string label = ellipsize(
                    *surface.m_ui_font, item.label,
                    round_to_int(available_width));
                surface.draw_text(context, *surface.m_ui_font, label,
                                  label_x,
                                  row_bounds.y + row_bounds.height * 0.5F,
                                  highlight_text ? "#ffffff" : surface.m_text.primary);
            }
        }

        surface.pop_clip(context);

        // Sticky headers
        std::vector<std::size_t> sticky = get_sticky_items();
        for (std::size_t i = 0; i < sticky.size(); ++i) {
            const std::size_t absolute_index = sticky[i];
            const auto& item = items[absolute_index];
            const float y = content_top + static_cast<float>(i) * row_height * scale;
            const UI::Rect row_bounds{
                layout.tool_sidebar_bounds.x, y,
                std::max(layout.tool_sidebar_bounds.width - 1.0F, 0.0F), row_height * scale
            };

            surface.fill_rectangle(context, row_bounds, surface.m_colors.sidebar_background);

            const float chevron_x = layout.tool_sidebar_bounds.x + 18.0F * scale + static_cast<float>(item.depth) * 12.0F * scale;
            const float center_y = y + row_height * 0.5F * scale;
            const int chevron_size = std::max(round_to_int(10.0F * scale), 8);
            const int folder_size = std::max(round_to_int(16.0F * scale), 14);

            surface.draw_svg_icon(
                context, "Assets/icons/chevron-down.svg",
                round_to_int(chevron_x), round_to_int(center_y), chevron_size,
                surface.m_palette.text_muted, surface.m_palette.sidebar_background);

            surface.draw_svg_icon(
                context, "Assets/icons/folder-open.svg",
                round_to_int(chevron_x + 15.0F * scale), round_to_int(center_y),
                folder_size, surface.m_palette.text_muted, surface.m_palette.sidebar_background);

            const float text_x = chevron_x + 30.0F * scale;
            if (surface.m_ui_font) {
                surface.draw_text(context, *surface.m_ui_font, item.label, text_x,
                                  center_y, surface.m_text.primary);
            }

            if (i == sticky.size() - 1) {
                surface.draw_line(
                    context,
                    round_to_int(row_bounds.x),
                    round_to_int(row_bounds.bottom()) - 1,
                    round_to_int(layout.tool_sidebar_bounds.right() - 1.0F),
                    round_to_int(row_bounds.bottom()) - 1,
                    surface.m_colors.border);
            }
        }

        // Scrollbar
        if (items.size() > max_rows) {
            const UI::Rect track = scrollbar_bounds(layout);
            const float visible_fraction = static_cast<float>(max_rows) / static_cast<float>(items.size());
            const float thumb_height = std::max(track.height * visible_fraction, 18.0F * scale);
            const std::size_t maximum_offset = items.size() - max_rows;
            const float progress = maximum_offset == 0 ? 0.0F : static_cast<float>(scroll) / static_cast<float>(maximum_offset);

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

        // Draw floating ghost badge when dragging a file/folder item (macOS drag animation preview)
        if (m_is_dragging_item && m_drag_source_row.has_value() && *m_drag_source_row < items.size()) {
            const auto& dragged = items[*m_drag_source_row];
            const std::string badge_label = dragged.label;
            const int text_w = surface.m_small_font ? surface.m_small_font->getTextWidth(badge_label) : 40;
            const float badge_w = static_cast<float>(text_w) + 36.0F * scale;
            const float badge_h = 24.0F * scale;
            const UI::Rect badge_rect{m_drag_current_x + 12.0F * scale, m_drag_current_y + 12.0F * scale, badge_w, badge_h};

            // Shadow
            const UI::Rect shadow_rect{badge_rect.x + 2.0F * scale, badge_rect.y + 3.0F * scale, badge_w, badge_h};
            const CGFloat shadow_col[4] = {0.0, 0.0, 0.0, 0.40};
            surface.fill_rounded_rectangle(context, shadow_rect, shadow_col, 5.0F * scale);

            // macOS Dark Glassmorphism Badge
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
        // Non-Project Panel Header (e.g. Search, Source Control, Run & Debug, Extensions, Settings, etc.)
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
        const float content_y = panel.y + (header_height + 16.0F) * scale;
        if (m_model.get_active_icon() == UI::Editor::SidebarIcon::Search)
        {
            const UI::Rect search_box{
                panel.x + 12.0F * scale,
                content_y,
                std::max(panel.width - 24.0F * scale, 0.0F),
                28.0F * scale
            };
            surface.fill_rectangle(context, search_box, surface.m_colors.editor_background);
            surface.draw_rectangle(context, search_box, surface.m_colors.border);
            surface.draw_svg_icon(
                context, "Assets/icons/search.svg",
                round_to_int(search_box.x + 13.0F * scale),
                round_to_int(search_box.y + search_box.height * 0.5F),
                std::max(round_to_int(13.0F * scale), 10),
                surface.m_palette.text_muted, surface.m_palette.editor_background);
            if (surface.m_small_font) {
                surface.draw_text(context, *surface.m_small_font, "Search files...",
                                  search_box.x + 26.0F * scale,
                                  search_box.y + search_box.height * 0.5F,
                                  surface.m_text.muted);
            }
        }

        const float message_y = content_y + (m_model.get_active_icon() == UI::Editor::SidebarIcon::Search ? 44.0F : 0.0F) * scale;
        if (surface.m_ui_font) {
            surface.draw_text(context, *surface.m_ui_font,
                              m_model.get_content_heading(),
                              panel.x + 14.0F * scale,
                              message_y,
                              surface.m_text.primary);
        }
        if (surface.m_small_font) {
            surface.draw_text(context, *surface.m_small_font,
                              m_model.get_content_detail(),
                              panel.x + 14.0F * scale,
                              message_y + 20.0F * scale,
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
