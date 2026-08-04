#include "Platform/X11/Components/ActivitySidebar.h"
#include "Platform/X11/Components/StudioWorkspaceRenderer.h"

#include <algorithm>
#include <cmath>

namespace Zenvra::Platform::X11::Components
{

namespace
{

int round_to_int(float value)
{
    return static_cast<int>(std::lround(value));
}

} // namespace

void ActivitySidebar::render(
    const StudioWorkspaceRenderer& surface,
    Drawable drawable,
    const UI::Editor::StudioEditorLayoutResult& layout) const
{
    const int center_x = round_to_int(layout.activity_bar_bounds.x + layout.activity_bar_bounds.width * 0.5F);
    std::size_t top_index = 0;
    std::size_t bottom_index = 0;
    const std::span<const UI::Editor::SidebarItem> items = UI::Editor::get_studio_sidebar_items();
    const std::size_t bottom_count = static_cast<std::size_t>(std::count_if(
        items.begin(), items.end(), [](const UI::Editor::SidebarItem& item) {
            return item.placement == UI::Editor::SidebarPlacement::Bottom;
        }));

    for (const UI::Editor::SidebarItem& item : items)
    {
        float center_y = 0.0F;
        if (item.placement == UI::Editor::SidebarPlacement::Top)
        {
            const bool tabs_are_in_titlebar =
                layout.tab_bar_bounds.bottom() <= layout.activity_bar_bounds.y;
            center_y = tabs_are_in_titlebar
                ? layout.activity_bar_bounds.y +
                    (UI::Editor::StudioEditorMetrics::tab_height * 0.5F +
                        static_cast<float>(top_index) *
                            UI::Editor::StudioEditorMetrics::sidebar_item_spacing) *
                        surface.m_dpi_scale
                : top_index == 0
                    ? layout.tab_bar_bounds.y + layout.tab_bar_bounds.height * 0.5F
                    : layout.editor_bounds.y +
                        (UI::Editor::StudioEditorMetrics::sidebar_top_offset +
                            static_cast<float>(top_index - 1) *
                                UI::Editor::StudioEditorMetrics::sidebar_item_spacing) *
                            surface.m_dpi_scale;
            ++top_index;
        }
        else
        {
            const std::size_t reverse_index = bottom_count - bottom_index;
            center_y = layout.status_bar_bounds.y -
                (UI::Editor::StudioEditorMetrics::sidebar_bottom_offset +
                    static_cast<float>(reverse_index - 1) *
                        UI::Editor::StudioEditorMetrics::sidebar_item_spacing) *
                    surface.m_dpi_scale;
            ++bottom_index;
        }

        const bool active = item.icon == UI::Editor::SidebarIcon::Terminal
            ? surface.m_terminal_panel.is_visible()
            : surface.m_tool_sidebar.is_active(item.icon);
        const bool hovered = surface.m_tool_sidebar.is_hovered(item.icon);
        if (active || hovered)
        {
            surface.fill_rectangle(
                drawable,
                UI::Rect{
                    layout.activity_bar_bounds.x,
                    center_y - UI::Editor::StudioEditorMetrics::sidebar_item_height *
                        0.5F * surface.m_dpi_scale,
                    layout.activity_bar_bounds.width,
                    UI::Editor::StudioEditorMetrics::sidebar_item_height * surface.m_dpi_scale,
                },
                surface.m_pixels.tab_active_background);
        }
        if (active)
        {
            surface.fill_rectangle(
                drawable,
                UI::Rect{
                    layout.activity_bar_bounds.x,
                    center_y - 13.0F * surface.m_dpi_scale,
                    2.0F * surface.m_dpi_scale,
                    26.0F * surface.m_dpi_scale,
                },
                surface.m_pixels.accent);
        }
        draw_icon(surface, drawable, item.icon, center_x, round_to_int(center_y), active);
    }

    surface.draw_line(
        drawable,
        round_to_int(layout.activity_bar_bounds.right() - 1.0F),
        round_to_int(layout.activity_bar_bounds.y),
        round_to_int(layout.activity_bar_bounds.right() - 1.0F),
        round_to_int(layout.activity_bar_bounds.bottom()),
        surface.m_pixels.border);
}

void ActivitySidebar::draw_icon(
    const StudioWorkspaceRenderer& surface,
    Drawable drawable,
    UI::Editor::SidebarIcon icon,
    int center_x,
    int center_y,
    bool active) const
{
    const int size = std::max(round_to_int(
        UI::Editor::StudioEditorMetrics::sidebar_icon_size * surface.m_dpi_scale), 14);
    std::string svg_path;
    switch (icon)
    {
    case UI::Editor::SidebarIcon::Project:
        svg_path = "Assets/icons/folder.svg";
        break;
    case UI::Editor::SidebarIcon::VersionControl:
        svg_path = "Assets/icons/git-branch.svg";
        break;
    case UI::Editor::SidebarIcon::Search:
        svg_path = "Assets/icons/search.svg";
        break;
    case UI::Editor::SidebarIcon::Services:
        svg_path = "Assets/icons/puzzle.svg";
        break;
    case UI::Editor::SidebarIcon::Run:
        svg_path = "Assets/icons/play.svg";
        break;
    case UI::Editor::SidebarIcon::Terminal:
        svg_path = "Assets/icons/terminal.svg";
        break;
    case UI::Editor::SidebarIcon::Problems:
        svg_path = "Assets/icons/bug.svg";
        break;
    case UI::Editor::SidebarIcon::More:
        svg_path = "Assets/icons/menu.svg";
        break;
    }

    if (!svg_path.empty())
    {
        surface.draw_svg_icon(
            drawable,
            svg_path,
            center_x,
            center_y,
            size,
            active ? surface.m_palette.text_primary : surface.m_palette.text_muted,
            active ? surface.m_palette.tab_active_background :
                     surface.m_palette.sidebar_background);
    }
}

} // namespace Zenvra::Platform::X11::Components
