#pragma once

#include "Settings/SettingsDefinition.h"
#include "Settings/SettingsService.h"
#include "UI/Geometry.h"
#include "UI/Theme/StudioTheme.h"

#include <string>

namespace Zenvra::UI::Settings
{

struct SettingRowLayout
{
    Rect bounds;
    Rect indicator_bar_bounds;
    Rect label_bounds;
    Rect modified_tag_bounds;
    Rect id_bounds;
    Rect description_bounds;
    Rect control_bounds;
    Rect reset_btn_bounds;

    // Sub-control bounding boxes
    Rect focus_box_bounds;
    Rect gear_btn_bounds;
    Rect input_bounds;
    Rect dropdown_btn_bounds;
    Rect checkbox_bounds;
    Rect toggle_bounds;
    Rect minus_btn_bounds;
    Rect plus_btn_bounds;
    Rect value_label_bounds;
    Rect option_btn_bounds;
    Rect thumbnail_bounds;
    Rect browse_btn_bounds;

    const Zenvra::Settings::SettingDefinition* def = nullptr;

    bool is_focused = false;
    bool is_gear_hovered = false;
    bool is_input_hovered = false;
    bool is_checkbox_hovered = false;
    bool is_toggle_hovered = false;
    bool is_minus_hovered = false;
    bool is_plus_hovered = false;
    bool is_option_hovered = false;
    bool is_thumbnail_hovered = false;
    bool is_browse_hovered = false;
    bool is_reset_hovered = false;
    bool is_modified = false;
    bool is_enum_dropdown_open = false;

    [[nodiscard]] bool is_interactive_point(float x, float y) const noexcept;
    bool handle_pointer_move(float x, float y) noexcept;
    bool handle_pointer_press(
        float x,
        float y,
        const Zenvra::Settings::SettingDefinition& def,
        Zenvra::Settings::SettingsService& service,
        Zenvra::Settings::SettingsScope scope);
};

} // namespace Zenvra::UI::Settings
