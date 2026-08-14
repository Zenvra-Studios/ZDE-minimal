#include "Platform/Win32/Components/ActivitySidebar.h"
#include "Platform/Win32/Components/StudioWorkspaceRenderer.h"

#include <algorithm>
#include <cmath>

namespace Zenvra::Platform::Win32::Components
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
    HDC device_context,
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
            const bool tabs_are_in_titlebar = layout.tab_bar_bounds.bottom() <= layout.activity_bar_bounds.y;
            center_y = tabs_are_in_titlebar
                ? layout.activity_bar_bounds.y +
                    (UI::Editor::StudioEditorMetrics::tab_height * 0.5F +
                        static_cast<float>(top_index) * UI::Editor::StudioEditorMetrics::sidebar_item_spacing) *
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
                device_context,
                UI::Rect{
                    layout.activity_bar_bounds.x,
                    center_y - UI::Editor::StudioEditorMetrics::sidebar_item_height *
                        0.5F * surface.m_dpi_scale,
                    layout.activity_bar_bounds.width,
                    UI::Editor::StudioEditorMetrics::sidebar_item_height * surface.m_dpi_scale,
                },
                surface.m_palette.tab_active_background);
        }
        if (active)
        {
            surface.fill_rectangle(
                device_context,
                UI::Rect{
                    layout.activity_bar_bounds.x,
                    center_y - 13.0F * surface.m_dpi_scale,
                    2.0F * surface.m_dpi_scale,
                    26.0F * surface.m_dpi_scale,
                },
                surface.m_palette.accent);
        }
        draw_icon(surface, device_context, item.icon, center_x, round_to_int(center_y), active);
    }

    surface.draw_line(
        device_context,
        round_to_int(layout.activity_bar_bounds.right() - 1.0F),
        round_to_int(layout.activity_bar_bounds.y),
        round_to_int(layout.activity_bar_bounds.right() - 1.0F),
        round_to_int(layout.activity_bar_bounds.bottom()),
        surface.m_palette.border);
}

void ActivitySidebar::draw_icon(
    const StudioWorkspaceRenderer& surface,
    HDC device_context,
    UI::Editor::SidebarIcon icon,
    int center_x,
    int center_y,
    bool active) const
{
    const int size = std::max(round_to_int(UI::Editor::StudioEditorMetrics::sidebar_icon_size * surface.m_dpi_scale), 14);
    std::string_view asset_name;
    switch (icon)
    {
    case UI::Editor::SidebarIcon::Project:
        asset_name = "folder.svg";
        break;
    case UI::Editor::SidebarIcon::VersionControl:
        asset_name = "git_branch.svg";
        break;
    case UI::Editor::SidebarIcon::Search:
        asset_name = "search.svg";
        break;
    case UI::Editor::SidebarIcon::Services:
        asset_name = "puzzle.svg";
        break;
    case UI::Editor::SidebarIcon::Run:
        asset_name = "play.svg";
        break;
    case UI::Editor::SidebarIcon::Terminal:
        asset_name = "terminal.svg";
        break;
    case UI::Editor::SidebarIcon::Problems:
        asset_name = "bug.svg";
        break;
    case UI::Editor::SidebarIcon::More:
        asset_name = "ellipsis.svg";
        break;
    }

    if (!asset_name.empty())
    {
        surface.draw_svg_icon(
            device_context,
            asset_name,
            center_x,
            center_y,
            size,
            active ? surface.m_palette.text_primary : surface.m_palette.text_muted,
            active ? surface.m_palette.tab_active_background
                   : surface.m_palette.sidebar_background);
    }
}

} // namespace Zenvra::Platform::Win32::Components
