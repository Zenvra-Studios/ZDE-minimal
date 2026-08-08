#include "Platform/X11/Components/ExplorerHeader.h"
#include "Platform/X11/Components/StudioWorkspaceRenderer.h"
#include "UI/Editor/ActivityPanelModel.h"

#include <cmath>

namespace Zenvra::Platform::X11::Components
{

constexpr float header_height = 36.0F;
constexpr float icon_spacing = 22.0F;
constexpr float right_margin = 16.0F;

static inline int round_to_int(float f) { return static_cast<int>(std::lround(f)); }

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
    Drawable drawable,
    const UI::Editor::StudioEditorLayoutResult& layout,
    const std::string& title) const
{
    const UI::Rect panel = layout.tool_sidebar_bounds;
    const float scale = layout.dpi_scale;
    
    // Draw Title
    surface.draw_text(drawable, *surface.m_ui_font, title,
                      panel.x + 14.0F * scale,
                      panel.y + header_height * 0.5F * scale,
                      surface.m_text.primary);

    const int header_center_y = round_to_int(panel.y + header_height * 0.5F * scale);
    const int icon_size = std::max(round_to_int(14.0F * scale), 11);
    
    auto draw_icon = [&](ActionIcon icon_type, const char* path, float center_x) {
        const auto& color = (m_hovered_icon == icon_type) ? surface.m_palette.text_primary : surface.m_palette.text_muted;
        
        if (m_hovered_icon == icon_type) {
            // Draw hover background
            UI::Rect hover_bg{
                center_x - 12.0F * scale,
                panel.y + (header_height * 0.5F - 12.0F) * scale,
                24.0F * scale,
                24.0F * scale
            };
            surface.fill_rounded_rectangle(drawable, hover_bg, surface.m_pixels.editor_background, 4.0F * scale, surface.m_pixels.sidebar_background);
        }
        
        surface.draw_svg_icon(drawable, path, round_to_int(center_x), header_center_y, icon_size, color, surface.m_palette.sidebar_background);
    };

    float current_x = panel.right() - right_margin * scale;
    draw_icon(ActionIcon::CollapseAll, "Assets/icons/collapse-all.svg", current_x);
    current_x -= icon_spacing * scale;
    
    draw_icon(ActionIcon::Refresh, "Assets/icons/refresh.svg", current_x);
    current_x -= icon_spacing * scale;
    
    draw_icon(ActionIcon::NewFolder, "Assets/icons/new-folder.svg", current_x);
    current_x -= icon_spacing * scale;
    
    draw_icon(ActionIcon::NewFile, "Assets/icons/new-file.svg", current_x);
}

bool ExplorerHeader::handle_pointer_move(
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_x,
    float point_y) noexcept
{
    ActionIcon hovered = get_icon_at_point(layout, point_x, point_y);
    if (hovered != m_hovered_icon) {
        m_hovered_icon = hovered;
        return true; // request redraw
    }
    return false;
}

bool ExplorerHeader::handle_pointer_press(
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_x,
    float point_y,
    UI::Editor::ActivityPanelModel& model,
    std::optional<std::filesystem::path>& file_to_open)
{
    ActionIcon pressed = get_icon_at_point(layout, point_x, point_y);
    switch (pressed) {
        case ActionIcon::NewFile:
            // TODO: implement new file action
            return true;
        case ActionIcon::NewFolder:
            // TODO: implement new folder action
            return true;
        case ActionIcon::Refresh:
            model.refresh();
            return true;
        case ActionIcon::CollapseAll:
            model.collapse_all();
            return true;
        default:
            return false;
    }
}

} // namespace Zenvra::Platform::X11::Components
