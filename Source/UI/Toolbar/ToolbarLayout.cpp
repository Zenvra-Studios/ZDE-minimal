#include "UI/Toolbar/ToolbarLayout.h"

#include <algorithm>

namespace Zenvra::UI::Toolbar
{

ToolbarLayoutResult ToolbarLayoutCalculator::compute_layout(
    float available_width,
    float content_top,
    float dpi_scale,
    const ToolbarLayoutMetrics& metrics) noexcept
{
    const float safe_scale = std::max(0.5F, dpi_scale);
    const float tb_h = metrics.toolbar_height * safe_scale;
    const float pad_h = metrics.padding_horizontal * safe_scale;
    const float spacing = metrics.item_spacing * safe_scale;
    const float btn_sz = metrics.button_size * safe_scale;

    ToolbarLayoutResult res{};
    res.dpi_scale = safe_scale;
    res.toolbar_bounds = UI::Rect{0.0F, content_top, available_width, tb_h};

    const float btn_y = content_top + (tb_h - btn_sz) * 0.5F;

    // 1. Left Section (Project / Branch)
    const float left_width = std::min(220.0F * safe_scale, available_width * 0.25F);
    res.left_section_bounds = UI::Rect{pad_h, content_top, left_width, tb_h};
    res.project_widget_bounds = UI::Rect{pad_h, btn_y, left_width, btn_sz};

    // 2. Right Section (Search Pill)
    const float search_w = std::min(metrics.search_pill_width * safe_scale, available_width * 0.25F);
    const float search_x = std::max(res.left_section_bounds.right() + spacing, available_width - pad_h - search_w);
    res.search_pill_bounds = UI::Rect{search_x, btn_y, search_w, btn_sz};
    res.right_section_bounds = UI::Rect{search_x, content_top, search_w, tb_h};

    // 3. Center Section (Target Combo + Action Buttons)
    const float target_combo_w = std::clamp(220.0F * safe_scale, metrics.target_combo_min_width * safe_scale, metrics.target_combo_max_width * safe_scale);
    const float action_buttons_w = 4.0F * btn_sz + 3.0F * spacing; // Run, Debug, Build, Stop
    const float center_total_w = target_combo_w + spacing + action_buttons_w;

    float center_x = (available_width - center_total_w) * 0.5F;
    if (center_x < res.project_widget_bounds.right() + spacing)
    {
        center_x = res.project_widget_bounds.right() + spacing;
    }

    res.center_section_bounds = UI::Rect{center_x, content_top, center_total_w, tb_h};
    res.target_combo_bounds = UI::Rect{center_x, btn_y, target_combo_w, btn_sz};

    float cur_btn_x = res.target_combo_bounds.right() + spacing;
    res.run_button_bounds = UI::Rect{cur_btn_x, btn_y, btn_sz, btn_sz};
    cur_btn_x += btn_sz + spacing;
    res.debug_button_bounds = UI::Rect{cur_btn_x, btn_y, btn_sz, btn_sz};
    cur_btn_x += btn_sz + spacing;
    res.build_button_bounds = UI::Rect{cur_btn_x, btn_y, btn_sz, btn_sz};
    cur_btn_x += btn_sz + spacing;
    res.stop_button_bounds = UI::Rect{cur_btn_x, btn_y, btn_sz, btn_sz};

    return res;
}

} // namespace Zenvra::UI::Toolbar
