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

COLORREF to_color_ref(const UI::Theme::Color& color)
{
    return RGB(color.red, color.green, color.blue);
}

} // namespace

void ActivitySidebar::render(
    const StudioWorkspaceRenderer& surface,
    HDC device_context,
    const UI::Editor::StudioEditorLayoutResult& layout) const
{
    const int center_x = round_to_int(
        layout.activity_bar_bounds.x + layout.activity_bar_bounds.width * 0.5F);
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
            center_y = top_index == 0
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
    const int size = std::max(round_to_int(
        UI::Editor::StudioEditorMetrics::sidebar_icon_size * surface.m_dpi_scale), 14);
    const int half = size / 2;
    const UI::Theme::Color icon_color = active
        ? surface.m_palette.text_primary
        : surface.m_palette.text_muted;
    HPEN pen = CreatePen(PS_SOLID, 1, to_color_ref(icon_color));
    HGDIOBJ previous_pen = SelectObject(device_context, pen);
    HGDIOBJ previous_brush = SelectObject(device_context, GetStockObject(HOLLOW_BRUSH));
    switch (icon)
    {
    case UI::Editor::SidebarIcon::Project:
        Rectangle(device_context, center_x - half, center_y - half + 2,
            center_x + half + 1, center_y + half + 1);
        MoveToEx(device_context, center_x - half + 1, center_y - half + 1, nullptr);
        LineTo(device_context, center_x - 1, center_y - half + 1);
        break;
    case UI::Editor::SidebarIcon::VersionControl:
        Ellipse(device_context, center_x - half, center_y - half,
            center_x - half + 5, center_y - half + 5);
        Ellipse(device_context, center_x + half - 5, center_y - half,
            center_x + half, center_y - half + 5);
        Ellipse(device_context, center_x - 2, center_y + half - 5,
            center_x + 3, center_y + half);
        MoveToEx(device_context, center_x - half + 2, center_y - half + 5, nullptr);
        LineTo(device_context, center_x, center_y + half - 5);
        MoveToEx(device_context, center_x + half - 2, center_y - half + 5, nullptr);
        LineTo(device_context, center_x, center_y + half - 5);
        break;
    case UI::Editor::SidebarIcon::Search:
        Ellipse(device_context, center_x - half, center_y - half,
            center_x + half - 2, center_y + half - 2);
        MoveToEx(device_context, center_x + 2, center_y + 2, nullptr);
        LineTo(device_context, center_x + half + 1, center_y + half + 1);
        break;
    case UI::Editor::SidebarIcon::Services:
        for (int row = 0; row < 2; ++row)
        {
            for (int column = 0; column < 2; ++column)
            {
                const int left = center_x - half + column * (half + 1);
                const int top = center_y - half + row * (half + 1);
                Rectangle(device_context, left, top, left + half - 1, top + half - 1);
            }
        }
        break;
    case UI::Editor::SidebarIcon::Run:
    {
        POINT points[]{
            POINT{center_x - half + 2, center_y - half},
            POINT{center_x + half, center_y},
            POINT{center_x - half + 2, center_y + half},
        };
        Polygon(device_context, points, 3);
        break;
    }
    case UI::Editor::SidebarIcon::Terminal:
        Rectangle(device_context, center_x - half, center_y - half,
            center_x + half + 1, center_y + half + 1);
        MoveToEx(device_context, center_x - half + 3, center_y - 3, nullptr);
        LineTo(device_context, center_x, center_y);
        LineTo(device_context, center_x - half + 3, center_y + 3);
        break;
    case UI::Editor::SidebarIcon::Problems:
        Ellipse(device_context, center_x - half, center_y - half,
            center_x + half + 1, center_y + half + 1);
        MoveToEx(device_context, center_x, center_y - 3, nullptr);
        LineTo(device_context, center_x, center_y + 2);
        SetPixel(device_context, center_x, center_y + 5, to_color_ref(icon_color));
        break;
    case UI::Editor::SidebarIcon::More:
        SelectObject(device_context, previous_brush);
        previous_brush = SelectObject(
            device_context,
            CreateSolidBrush(to_color_ref(icon_color)));
        for (int offset = -4; offset <= 4; offset += 4)
        {
            Ellipse(device_context, center_x + offset - 1, center_y - 1,
                center_x + offset + 2, center_y + 2);
        }
        break;
    }
    HGDIOBJ active_brush = SelectObject(device_context, previous_brush);
    if (icon == UI::Editor::SidebarIcon::More)
    {
        DeleteObject(active_brush);
    }
    SelectObject(device_context, previous_pen);
    DeleteObject(pen);
}

} // namespace Zenvra::Platform::Win32::Components
