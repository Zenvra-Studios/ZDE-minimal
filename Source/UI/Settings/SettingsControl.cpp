#include "UI/Settings/SettingsControl.h"
#include "Utility/Ascii/AsciiMascotRenderer.h"

namespace Zenvra::UI::Settings
{

bool SettingRowLayout::is_interactive_point(float x, float y) const noexcept
{
    if (!reset_btn_bounds.is_empty() && reset_btn_bounds.contains(x, y)) {
        return true;
    }
    if (!gear_btn_bounds.is_empty() && gear_btn_bounds.contains(x, y)) {
        return true;
    }
    if (!input_bounds.is_empty() && input_bounds.contains(x, y)) {
        return true;
    }
    if (!dropdown_btn_bounds.is_empty() && dropdown_btn_bounds.contains(x, y)) {
        return true;
    }
    if (!checkbox_bounds.is_empty() && checkbox_bounds.contains(x, y)) {
        return true;
    }
    if (!toggle_bounds.is_empty() && toggle_bounds.contains(x, y)) {
        return true;
    }
    if (!minus_btn_bounds.is_empty() && minus_btn_bounds.contains(x, y)) {
        return true;
    }
    if (!plus_btn_bounds.is_empty() && plus_btn_bounds.contains(x, y)) {
        return true;
    }
    if (!option_btn_bounds.is_empty() && option_btn_bounds.contains(x, y)) {
        return true;
    }
    if (!thumbnail_bounds.is_empty() && thumbnail_bounds.contains(x, y)) {
        return true;
    }
    if (!browse_btn_bounds.is_empty() && browse_btn_bounds.contains(x, y)) {
        return true;
    }
    if (!switch_default_btn_bounds.is_empty() && switch_default_btn_bounds.contains(x, y)) {
        return true;
    }
    if (!switch_ascii_btn_bounds.is_empty() && switch_ascii_btn_bounds.contains(x, y)) {
        return true;
    }
    return false;
}

bool SettingRowLayout::handle_pointer_move(float x, float y) noexcept
{
    bool changed = false;

    const bool new_gear = !gear_btn_bounds.is_empty() && gear_btn_bounds.contains(x, y);
    if (is_gear_hovered != new_gear) {
        is_gear_hovered = new_gear;
        changed = true;
    }

    const bool new_input = !input_bounds.is_empty() && input_bounds.contains(x, y);
    if (is_input_hovered != new_input) {
        is_input_hovered = new_input;
        changed = true;
    }

    const bool new_cb = !checkbox_bounds.is_empty() && checkbox_bounds.contains(x, y);
    if (is_checkbox_hovered != new_cb) {
        is_checkbox_hovered = new_cb;
        changed = true;
    }

    const bool new_toggle = !toggle_bounds.is_empty() && toggle_bounds.contains(x, y);
    if (is_toggle_hovered != new_toggle) {
        is_toggle_hovered = new_toggle;
        changed = true;
    }

    const bool new_minus = !minus_btn_bounds.is_empty() && minus_btn_bounds.contains(x, y);
    if (is_minus_hovered != new_minus) {
        is_minus_hovered = new_minus;
        changed = true;
    }

    const bool new_plus = !plus_btn_bounds.is_empty() && plus_btn_bounds.contains(x, y);
    if (is_plus_hovered != new_plus) {
        is_plus_hovered = new_plus;
        changed = true;
    }

    const bool new_opt = !option_btn_bounds.is_empty() && option_btn_bounds.contains(x, y);
    if (is_option_hovered != new_opt) {
        is_option_hovered = new_opt;
        changed = true;
    }

    const bool new_thumb = !thumbnail_bounds.is_empty() && thumbnail_bounds.contains(x, y);
    if (is_thumbnail_hovered != new_thumb) {
        is_thumbnail_hovered = new_thumb;
        changed = true;
    }

    const bool new_browse = !browse_btn_bounds.is_empty() && browse_btn_bounds.contains(x, y);
    if (is_browse_hovered != new_browse) {
        is_browse_hovered = new_browse;
        changed = true;
    }

    const bool new_reset = !reset_btn_bounds.is_empty() && reset_btn_bounds.contains(x, y);
    if (is_reset_hovered != new_reset) {
        is_reset_hovered = new_reset;
        changed = true;
    }

    const bool new_sw_def = !switch_default_btn_bounds.is_empty() && switch_default_btn_bounds.contains(x, y);
    if (is_switch_default_hovered != new_sw_def) {
        is_switch_default_hovered = new_sw_def;
        changed = true;
    }

    const bool new_sw_asc = !switch_ascii_btn_bounds.is_empty() && switch_ascii_btn_bounds.contains(x, y);
    if (is_switch_ascii_hovered != new_sw_asc) {
        is_switch_ascii_hovered = new_sw_asc;
        changed = true;
    }

    return changed;
}

bool SettingRowLayout::handle_pointer_press(
    float x,
    float y,
    const Zenvra::Settings::SettingDefinition& def,
    Zenvra::Settings::SettingsService& service,
    Zenvra::Settings::SettingsScope scope)
{
    // 1. Reset / Gear button clicked
    if ((is_modified && reset_btn_bounds.contains(x, y)) || (!gear_btn_bounds.is_empty() && gear_btn_bounds.contains(x, y))) {
        service.reset(def.id, scope);
        return true;
    }

    // 2. Mascot mode switch (Default vs ASCII)
    if (def.id == "workbench.mascot.renderMode" || def.id == "workbench.mascot.image") {
        if (!switch_default_btn_bounds.is_empty() && switch_default_btn_bounds.contains(x, y)) {
            Utility::Ascii::AsciiMascotRenderer::clear_bitmap_cache();
            Utility::Ascii::AsciiArtConverter::clear_cache();
            service.set("workbench.mascot.renderMode", std::string("default"), scope);
            return true;
        }
        if (!switch_ascii_btn_bounds.is_empty() && switch_ascii_btn_bounds.contains(x, y)) {
            Utility::Ascii::AsciiMascotRenderer::clear_bitmap_cache();
            Utility::Ascii::AsciiArtConverter::clear_cache();
            service.set("workbench.mascot.renderMode", std::string("ascii"), scope);
            return true;
        }
    }

    // 2. Input box clicked
    if (!input_bounds.is_empty() && input_bounds.contains(x, y)) {
        return true;
    }

    // 2. Boolean toggle / checkbox
    if (def.type == Zenvra::Settings::SettingType::Boolean) {
        if (checkbox_bounds.contains(x, y) || toggle_bounds.contains(x, y) || control_bounds.contains(x, y)) {
            const bool current = service.get<bool>(def.id);
            service.set(def.id, !current, scope);
            return true;
        }
    }

    // 3. Integer stepper
    if (def.type == Zenvra::Settings::SettingType::Integer) {
        const double step = def.step.value_or(1.0);
        if (minus_btn_bounds.contains(x, y)) {
            const int64_t current = service.get<int64_t>(def.id);
            service.set(def.id, current - static_cast<int64_t>(step), scope);
            return true;
        }
        if (plus_btn_bounds.contains(x, y)) {
            const int64_t current = service.get<int64_t>(def.id);
            service.set(def.id, current + static_cast<int64_t>(step), scope);
            return true;
        }
    }

    // 4. Float stepper
    if (def.type == Zenvra::Settings::SettingType::Float) {
        const double step = def.step.value_or(0.1);
        if (minus_btn_bounds.contains(x, y)) {
            const double current = service.get<double>(def.id);
            service.set(def.id, current - step, scope);
            return true;
        }
        if (plus_btn_bounds.contains(x, y)) {
            const double current = service.get<double>(def.id);
            service.set(def.id, current + step, scope);
            return true;
        }
    }

    // 5. Enum cycle
    if (def.type == Zenvra::Settings::SettingType::Enum && !def.enum_values.empty()) {
        if (option_btn_bounds.contains(x, y)) {
            const std::string current = service.get<std::string>(def.id);
            std::size_t curr_idx = 0;
            for (std::size_t i = 0; i < def.enum_values.size(); ++i) {
                if (def.enum_values[i].value == current) {
                    curr_idx = i;
                    break;
                }
            }
            const std::size_t next_idx = (curr_idx + 1) % def.enum_values.size();
            service.set(def.id, def.enum_values[next_idx].value, scope);
            return true;
        }
    }

    return false;
}

} // namespace Zenvra::UI::Settings
