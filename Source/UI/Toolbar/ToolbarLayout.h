#pragma once

#include "UI/Geometry.h"
#include "UI/Toolbar/ToolbarTypes.h"

namespace Zenvra::UI::Toolbar
{

struct ToolbarLayoutMetrics
{
    float toolbar_height = 36.0F;
    float item_spacing = 6.0F;
    float padding_horizontal = 8.0F;
    float button_size = 28.0F;
    float target_combo_min_width = 160.0F;
    float target_combo_max_width = 300.0F;
    float search_pill_width = 220.0F;
};

struct ToolbarLayoutResult
{
    UI::Rect toolbar_bounds;
    UI::Rect left_section_bounds;
    UI::Rect project_widget_bounds;
    UI::Rect center_section_bounds;
    UI::Rect target_combo_bounds;
    UI::Rect run_button_bounds;
    UI::Rect debug_button_bounds;
    UI::Rect build_button_bounds;
    UI::Rect stop_button_bounds;
    UI::Rect right_section_bounds;
    UI::Rect search_pill_bounds;
    float dpi_scale = 1.0F;
};

class ToolbarLayoutCalculator
{
public:
    [[nodiscard]] static ToolbarLayoutResult compute_layout(
        float available_width,
        float content_top,
        float dpi_scale,
        const ToolbarLayoutMetrics& metrics = {}) noexcept;
};

} // namespace Zenvra::UI::Toolbar
