#include "Platform/Win32/Components/ExplorerHeader.h"
#include "Platform/Win32/Components/StudioWorkspaceRenderer.h"
#include "UI/Editor/ActivityPanelModel.h"
#include "Utility/MathUtil.h"

#include <cmath>

namespace Zenvra::Platform::Win32::Components
{

using Zenvra::Utility::round_to_int;

constexpr float header_height = UI::Editor::StudioEditorMetrics::tab_height;
constexpr float icon_spacing = 22.0F;
constexpr float right_margin = 16.0F;

ExplorerHeader::ActionIcon ExplorerHeader::get_icon_at_point(
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_x,
    float point_y) const noexcept
{
    const UI::Rect panel = layout.tool_sidebar_bounds;
    const float scale = layout.dpi_scale;
    
    if (point_y < panel.y || point_y > panel.y + header_height * scale) {
        return ActionIcon::NoneAction;
    }
    
    float current_x = panel.right() - right_margin * scale;
    const float hit_radius = 10.0F * scale;
    
    // More (ellipsis)
    if (std::abs(point_x - current_x) <= hit_radius) return ActionIcon::More;
    current_x -= icon_spacing * scale;

    // Collapse All
    if (std::abs(point_x - current_x) <= hit_radius) return ActionIcon::CollapseAll;
    current_x -= icon_spacing * scale;
    
    // Refresh
    if (std::abs(point_x - current_x) <= hit_radius) return ActionIcon::Refresh;
    current_x -= icon_spacing * scale;
    
    // New Folder
    if (std::abs(point_x - current_x) <= hit_radius) return ActionIcon::NewFolder;
    current_x -= icon_spacing * scale;
    
    // New File
    if (std::abs(point_x - current_x) <= hit_radius) return ActionIcon::NewFile;
    
    return ActionIcon::NoneAction;
}

void ExplorerHeader::render(
    const StudioWorkspaceRenderer& surface,
    HDC device_context,
    const UI::Editor::StudioEditorLayoutResult& layout,
    const std::string& title) const
{
    const UI::Rect panel = layout.tool_sidebar_bounds;
    const float scale = layout.dpi_scale;
    const UI::Rect header_bounds{
        panel.x, panel.y, panel.width, header_height * scale
    };
    
    surface.fill_rectangle(device_context, header_bounds, surface.m_palette.sidebar_background);
    const float center_y = header_bounds.y + header_bounds.height * 0.5F;
    
    // Draw Title
    surface.draw_text(device_context, *surface.m_ui_font, title,
                      header_bounds.x + 14.0F * scale,
                      center_y, surface.m_palette.text_primary);

    const int header_center_y = round_to_int(center_y);
    const int icon_size = std::max(round_to_int(14.0F * scale), 11);
    
    auto draw_icon = [&](ActionIcon icon_type, const char* path, float center_x) {
        const auto& color = (m_hovered_icon == icon_type) ? surface.m_palette.text_primary : surface.m_palette.text_muted;
        
        if (m_hovered_icon == icon_type) {
            UI::Rect hover_bg{
                center_x - 11.0F * scale,
                panel.y + (header_height * 0.5F - 11.0F) * scale,
                22.0F * scale,
                22.0F * scale
            };
            surface.fill_rectangle(device_context, hover_bg, surface.m_palette.hover_background);
        }
        
        surface.draw_svg_icon(device_context, path, round_to_int(center_x), header_center_y, icon_size, color, surface.m_palette.sidebar_background);
    };

    float current_x = panel.right() - right_margin * scale;
    draw_icon(ActionIcon::More, "ellipsis.svg", current_x);
    current_x -= icon_spacing * scale;

    draw_icon(ActionIcon::CollapseAll, "collapse-all.svg", current_x);
    current_x -= icon_spacing * scale;
    
    draw_icon(ActionIcon::Refresh, "refresh.svg", current_x);
    current_x -= icon_spacing * scale;
    
    draw_icon(ActionIcon::NewFolder, "new-folder.svg", current_x);
    current_x -= icon_spacing * scale;
    
    draw_icon(ActionIcon::NewFile, "new-file.svg", current_x);

    surface.draw_line(device_context,
        round_to_int(header_bounds.x),
        round_to_int(header_bounds.bottom() - 1.0F),
        round_to_int(header_bounds.right()),
        round_to_int(header_bounds.bottom() - 1.0F),
        surface.m_palette.border);
}

bool ExplorerHeader::handle_pointer_move(
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_x,
    float point_y) noexcept
{
    ActionIcon hovered = get_icon_at_point(layout, point_x, point_y);
    if (hovered != m_hovered_icon) {
        m_hovered_icon = hovered;
        return true;
    }
    return false;
}

bool ExplorerHeader::handle_pointer_press(
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_x,
    float point_y,
    UI::Editor::ActivityPanelModel& model,
    HeaderAction& action_out)
{
    ActionIcon pressed = get_icon_at_point(layout, point_x, point_y);
    switch (pressed) {
        case ActionIcon::NewFile:
            action_out = HeaderAction::NewFile;
            return true;
        case ActionIcon::NewFolder:
            action_out = HeaderAction::NewFolder;
            return true;
        case ActionIcon::Refresh:
            static_cast<void>(model.refresh());
            action_out = HeaderAction::Refresh;
            return true;
        case ActionIcon::CollapseAll:
            model.collapse_all();
            action_out = HeaderAction::CollapseAll;
            return true;
        case ActionIcon::More:
            action_out = HeaderAction::More;
            return true;
        default:
            action_out = HeaderAction::None;
            return false;
    }
}

} // namespace Zenvra::Platform::Win32::Components
