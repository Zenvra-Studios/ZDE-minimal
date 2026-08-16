#include "Platform/Cocoa/Components/ExplorerHeader.h"
#include "Platform/Cocoa/Components/CocoaPromptDialog.h"
#include "Platform/Cocoa/Components/StudioWorkspaceRenderer.h"
#include "Utility/Fonts.h"

#include <cmath>
#include <fstream>

namespace Zenvra::Platform::Cocoa::Components
{

constexpr float header_height = UI::Editor::StudioEditorMetrics::tab_height;
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
    CGContextRef context,
    const UI::Editor::StudioEditorLayoutResult& layout,
    const std::string& title) const
{
    const UI::Rect panel = layout.tool_sidebar_bounds;
    const float scale = surface.m_dpi_scale;
    const UI::Rect header_bounds{
        panel.x, panel.y, panel.width, header_height * scale
    };
    
    surface.fill_rectangle(context, header_bounds, surface.m_colors.sidebar_background);
    const float center_y = header_bounds.y + header_bounds.height * 0.5F;
    
    // Draw Title
    surface.draw_text(context, *surface.m_ui_font, title,
                      header_bounds.x + 14.0F * scale,
                      center_y, surface.m_text.primary);

    const int header_center_y = round_to_int(center_y);
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
            surface.fill_rounded_rectangle(context, hover_bg, surface.m_colors.hover_background, 4.0F * scale);
        }
        
        surface.draw_svg_icon(context, path, round_to_int(center_x), header_center_y, icon_size, color, surface.m_palette.sidebar_background);
    };

    float current_x = panel.right() - right_margin * scale;
    draw_icon(ActionIcon::More, "Assets/icons/ellipsis.svg", current_x);
    current_x -= icon_spacing * scale;

    draw_icon(ActionIcon::CollapseAll, "Assets/icons/collapse-all.svg", current_x);
    current_x -= icon_spacing * scale;
    
    draw_icon(ActionIcon::Refresh, "Assets/icons/refresh.svg", current_x);
    current_x -= icon_spacing * scale;
    
    draw_icon(ActionIcon::NewFolder, "Assets/icons/new-folder.svg", current_x);
    current_x -= icon_spacing * scale;
    
    draw_icon(ActionIcon::NewFile, "Assets/icons/new-file.svg", current_x);

    surface.draw_line(context,
        round_to_int(header_bounds.x),
        round_to_int(header_bounds.bottom() - 1.0F),
        round_to_int(header_bounds.right()),
        round_to_int(header_bounds.bottom() - 1.0F),
        surface.m_colors.border);
}

bool ExplorerHeader::handle_pointer_move(
    const UI::Editor::StudioEditorLayoutResult& layout,
    float px, float py) noexcept
{
    auto new_icon = get_icon_at_point(layout, px, py);
    bool changed = new_icon != m_hovered_icon;
    m_hovered_icon = new_icon;
    return changed;
}

bool ExplorerHeader::handle_pointer_press(
    StudioWorkspaceRenderer& surface,
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_x, float point_y,
    UI::Editor::ActivityPanelModel& model,
    std::optional<std::filesystem::path>& file_to_open)
{
    ActionIcon pressed = get_icon_at_point(layout, point_x, point_y);
    switch (pressed) {
        case ActionIcon::NewFile: {
            const std::filesystem::path target_dir = model.get_target_directory_for_creation();
            surface.get_prompt_dialog().open_new_file(
                target_dir,
                [&model, &file_to_open](const std::string& input, const std::string& content) {
                    if (input.empty()) return;
                    std::filesystem::path out_path;
                    if (model.create_file(input, out_path)) {
                        if (!content.empty()) {
                            std::ofstream out(out_path, std::ios::binary);
                            if (out.is_open()) {
                                out.write(content.data(), content.size());
                                out.close();
                            }
                        }
                        file_to_open = out_path;
                    }
                });
            return true;
        }
        case ActionIcon::NewFolder: {
            const std::filesystem::path target_dir = model.get_target_directory_for_creation();
            surface.get_prompt_dialog().open_new_folder(
                target_dir,
                [&model](const std::string& input) {
                    if (input.empty()) return;
                    std::filesystem::path out_path;
                    model.create_directory(input, out_path);
                });
            return true;
        }
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

} // namespace Zenvra::Platform::Cocoa::Components
