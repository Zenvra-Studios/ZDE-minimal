#include "Platform/Cocoa/Components/ActivitySidebar.h"
#include "Platform/Cocoa/Components/StudioWorkspaceRenderer.h"
#include "Utility/Fonts.h"

#include <algorithm>
#include <cmath>

namespace Zenvra::Platform::Cocoa::Components
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
    CGContextRef context,
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
            : (item.icon == UI::Editor::SidebarIcon::Shader
                ? surface.m_shader_sandbox_panel.is_visible()
                : surface.m_tool_sidebar.is_active(item.icon));
        const bool hovered = surface.m_tool_sidebar.is_hovered(item.icon);
        if (active || hovered)
        {
            surface.fill_rectangle(
                context,
                UI::Rect{
                    layout.activity_bar_bounds.x,
                    center_y - UI::Editor::StudioEditorMetrics::sidebar_item_height *
                        0.5F * surface.m_dpi_scale,
                    layout.activity_bar_bounds.width,
                    UI::Editor::StudioEditorMetrics::sidebar_item_height * surface.m_dpi_scale,
                },
                surface.m_colors.tab_active_background);
        }
        if (active)
        {
            surface.fill_rectangle(
                context,
                UI::Rect{
                    layout.activity_bar_bounds.x,
                    center_y - 13.0F * surface.m_dpi_scale,
                    2.0F * surface.m_dpi_scale,
                    26.0F * surface.m_dpi_scale,
                },
                surface.m_colors.text_primary);
        }
        draw_icon(surface, context, item.icon, center_x, round_to_int(center_y), active, hovered);

        if (item.icon == UI::Editor::SidebarIcon::VersionControl)
        {
            const std::size_t count = surface.m_tool_sidebar.get_source_control_model().get_total_changes_count();
            if (count > 0)
            {
                const std::string badge_str = count > 999 ? (std::to_string(count / 1000) + "K+") : std::to_string(count);
                const int str_w = surface.m_small_font ? surface.m_small_font->getTextWidth(badge_str) : 12;
                const float pill_w = std::max(static_cast<float>(str_w) + 6.0F * surface.m_dpi_scale, 14.0F * surface.m_dpi_scale);
                const float pill_h = 13.0F * surface.m_dpi_scale;
                const UI::Rect badge_rect{
                    static_cast<float>(center_x) + 2.0F * surface.m_dpi_scale,
                    center_y - 12.0F * surface.m_dpi_scale,
                    pill_w,
                    pill_h
                };
                const CGFloat badge_bg[4] = {0.0, 122.0/255.0, 204.0/255.0, 1.0};
                surface.fill_rounded_rectangle(context, badge_rect, badge_bg, pill_h * 0.5F);
                if (surface.m_small_font)
                {
                    surface.draw_text(context, *surface.m_small_font, badge_str,
                                      badge_rect.x + (pill_w - static_cast<float>(str_w)) * 0.5F,
                                      badge_rect.y + pill_h * 0.5F,
                                      "#ffffff");
                }
            }
        }
    }

    surface.draw_line(
        context,
        round_to_int(layout.activity_bar_bounds.right() - 1.0F),
        round_to_int(layout.activity_bar_bounds.y),
        round_to_int(layout.activity_bar_bounds.right() - 1.0F),
        round_to_int(layout.activity_bar_bounds.bottom()),
        surface.m_colors.border);
}

void ActivitySidebar::draw_icon(
    const StudioWorkspaceRenderer& surface,
    CGContextRef context,
    UI::Editor::SidebarIcon icon,
    int center_x, int center_y,
    bool active,
    bool hovered) const
{
    const int size = std::max(static_cast<int>(std::lround(
        UI::Editor::StudioEditorMetrics::sidebar_icon_size * surface.m_dpi_scale)), 14);
    std::string svg_path;
    switch (icon)
    {
    case UI::Editor::SidebarIcon::Project:
        svg_path = "vscode-codicons/icons/folder.svg";
        break;
    case UI::Editor::SidebarIcon::VersionControl:
        svg_path = "vscode-codicons/icons/source-control.svg";
        break;
    case UI::Editor::SidebarIcon::Search:
        svg_path = "vscode-codicons/icons/search.svg";
        break;
    case UI::Editor::SidebarIcon::Services:
        svg_path = "vscode-codicons/icons/extensions.svg";
        break;
    case UI::Editor::SidebarIcon::Shader:
        svg_path = "material-icon-theme/shader.svg";
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
        svg_path = "vscode-codicons/icons/ellipsis.svg";
        break;
    }

    if (!svg_path.empty())
    {
        const UI::Theme::Color icon_color = active
            ? UI::Theme::Color{255, 255, 255, 255}
            : (hovered ? surface.m_palette.text_primary : surface.m_palette.text_muted);
        const UI::Theme::Color bg_color = active
            ? surface.m_palette.tab_active_background
            : (hovered ? surface.m_palette.hover_background : surface.m_palette.sidebar_background);
        surface.draw_svg_icon(
            context, svg_path, center_x, center_y, size,
            icon_color,
            bg_color,
            false);
    }
}

} // namespace Zenvra::Platform::Cocoa::Components
