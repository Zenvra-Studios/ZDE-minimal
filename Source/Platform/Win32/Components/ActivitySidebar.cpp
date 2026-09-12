#include "Platform/Win32/Components/ActivitySidebar.h"
#include "Platform/Win32/Components/StudioWorkspaceRenderer.h"
#include "Plugins/PluginManager.h"
#include "Utility/MathUtil.h"

#include <algorithm>
#include <cmath>
#include <iostream>

/**
 * 
 **/
namespace Zenvra::Platform::Win32::Components
{

    using Zenvra::Utility::round_to_int;

/**
 * 
 **/
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
    const bool is_modern = surface.m_palette.is_modern || surface.m_theme.is_modern || surface.m_theme.enable_os_blur;

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
            : item.icon == UI::Editor::SidebarIcon::Shader
                ? surface.m_shader_sandbox_panel.is_visible()
                : surface.m_tool_sidebar.is_active(item.icon);
        const bool hovered = surface.m_tool_sidebar.is_hovered(item.icon);
        const float item_h = UI::Editor::StudioEditorMetrics::sidebar_item_height * surface.m_dpi_scale;
        const UI::Rect box_rect{
            layout.activity_bar_bounds.x + 4.0F * surface.m_dpi_scale,
            center_y - item_h * 0.5F + 3.0F * surface.m_dpi_scale,
            layout.activity_bar_bounds.width - 8.0F * surface.m_dpi_scale,
            item_h - 6.0F * surface.m_dpi_scale,
        };
        const float box_radius = 5.0F * surface.m_dpi_scale;

        if (active)
        {
            const UI::Theme::Color active_bg = surface.m_palette.is_dark
                ? UI::Theme::Color{255, 255, 255, 28}
                : UI::Theme::Color{0, 102, 204, 28};
            surface.fill_rounded_rectangle(device_context, box_rect, active_bg, box_radius);

            if (!is_modern)
            {
                const UI::Rect pill_rect{
                    layout.activity_bar_bounds.x,
                    center_y - 12.0F * surface.m_dpi_scale,
                    2.5F * surface.m_dpi_scale,
                    24.0F * surface.m_dpi_scale,
                };
                surface.fill_rounded_rectangle(
                    device_context,
                    pill_rect,
                    surface.m_palette.is_dark ? surface.m_palette.text_primary : surface.m_palette.accent,
                    1.25F * surface.m_dpi_scale);
            }
        }
        else if (hovered)
        {
            const UI::Theme::Color hover_bg = surface.m_palette.is_dark
                ? UI::Theme::Color{255, 255, 255, 18}
                : UI::Theme::Color{0, 0, 0, 16};
            surface.fill_rounded_rectangle(device_context, box_rect, hover_bg, box_radius);
        }
        draw_icon(surface, device_context, item.icon, center_x, round_to_int(center_y), active, hovered);
    }

    if (!is_modern) {
        surface.draw_line(
            device_context,
            round_to_int(layout.activity_bar_bounds.right() - 1.0F),
            round_to_int(layout.activity_bar_bounds.y),
            round_to_int(layout.activity_bar_bounds.right() - 1.0F),
            round_to_int(layout.activity_bar_bounds.bottom()),
            surface.m_palette.border);
    }
}

/**
 * 
 **/
void ActivitySidebar::draw_icon(
    const StudioWorkspaceRenderer& surface,
    HDC device_context,
    UI::Editor::SidebarIcon icon,
    int center_x,
    int center_y,
    bool active,
    bool hovered) const
{
    const int size = std::max(round_to_int(UI::Editor::StudioEditorMetrics::sidebar_icon_size * surface.m_dpi_scale), 14);
    std::string_view asset_name;
    switch (icon)
    {
    case UI::Editor::SidebarIcon::Project:
        asset_name = "folder.svg";
        break;
    case UI::Editor::SidebarIcon::VersionControl:
        asset_name = "vscode-codicons/icons/source-control.svg";
        break;
    case UI::Editor::SidebarIcon::Search:
        asset_name = "vscode-codicons/icons/search.svg";
        break;
    case UI::Editor::SidebarIcon::Services:
        asset_name = "vscode-codicons/icons/extensions.svg";
        break;
    case UI::Editor::SidebarIcon::Shader:
        asset_name = "material-icon-theme/shader.svg";
        break;
    case UI::Editor::SidebarIcon::ToolPlugin: {
        auto& pm = Zenvra::Plugins::PluginManager::instance();
        auto tool = pm.get_active_tool_plugin();
        std::string tool_name = tool ? tool->get_name() : "";
        std::string tool_id = tool ? tool->get_id() : "";
        std::string tool_lower = tool_name;
        for (char& c : tool_lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        if (tool_lower.find("docker") != std::string::npos || tool_id.find("docker") != std::string::npos) {
            asset_name = "material-icon-theme/docker.svg";
        } else if (tool_lower.find("qemu") != std::string::npos || tool_id.find("qemu") != std::string::npos) {
            asset_name = "vscode-codicons/icons/chip.svg";
        } else if (tool_lower.find("gdb") != std::string::npos || tool_lower.find("debug") != std::string::npos) {
            asset_name = "vscode-codicons/icons/debug-alt.svg";
        } else if (tool_lower.find("wasm") != std::string::npos) {
            asset_name = "vscode-codicons/icons/server.svg";
        } else if (tool_lower.find("cmake") != std::string::npos || tool_id.find("cmake") != std::string::npos) {
            asset_name = "Assets/icons/cmake.svg";
        } else {
            asset_name = "vscode-codicons/icons/tools.svg";
        }
        break;
    }
    case UI::Editor::SidebarIcon::Run:
        asset_name = "Assets/icons/play.svg";
        break;
    case UI::Editor::SidebarIcon::Terminal:
        asset_name = "Assets/icons/terminal.svg";
        break;
    case UI::Editor::SidebarIcon::Problems:
        asset_name = "Assets/icons/bug.svg";
        break;
    case UI::Editor::SidebarIcon::More:
        asset_name = "vscode-codicons/icons/ellipsis.svg";
        break;
    }

    if (!asset_name.empty())
    {
        // ToolPlugin icons (Docker etc.) use material-icon-theme SVGs which have
        // more internal padding than vscode-codicons. Scale them up so they
        // appear visually equal to the other sidebar icons.
        const int draw_size = (icon == UI::Editor::SidebarIcon::ToolPlugin)
            ? std::max(round_to_int(UI::Editor::StudioEditorMetrics::sidebar_icon_size * 1.33F * surface.m_dpi_scale), 18)
            : size;

        const UI::Theme::Color icon_color = active
            ? (surface.m_palette.is_dark ? UI::Theme::Color{255, 255, 255, 255} : surface.m_palette.accent)
            : (hovered ? surface.m_palette.text_primary : surface.m_palette.text_muted);
        const UI::Theme::Color bg_color = active
            ? (surface.m_palette.is_dark ? UI::Theme::Color{255, 255, 255, 28} : UI::Theme::Color{0, 102, 204, 28})
            : (hovered
                ? (surface.m_palette.is_dark ? UI::Theme::Color{255, 255, 255, 18} : UI::Theme::Color{0, 0, 0, 16})
                : UI::Theme::Color{0, 0, 0, 0});
        surface.draw_svg_icon(
            device_context,
            asset_name,
            center_x,
            center_y,
            draw_size,
            icon_color,
            bg_color,
            false);
    }
}

} // namespace Zenvra::Platform::Win32::Components
