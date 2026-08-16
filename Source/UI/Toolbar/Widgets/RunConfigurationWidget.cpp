#include "UI/Toolbar/Widgets/RunConfigurationWidget.h"

#include <sstream>

namespace Zenvra::UI::Toolbar::Widgets
{

void RunConfigurationWidget::set_state(RunConfigurationState state) noexcept
{
    m_state = std::move(state);
}

void RunConfigurationWidget::set_active_target(std::string_view target_name)
{
    m_state.active_target_name = std::string(target_name);
}

void RunConfigurationWidget::set_active_mode(BuildConfigurationMode mode) noexcept
{
    m_state.active_mode = mode;
}

void RunConfigurationWidget::set_active_architecture(TargetArchitecture arch) noexcept
{
    m_state.active_architecture = arch;
}

void RunConfigurationWidget::set_active_preset(std::string_view preset_name)
{
    m_state.active_preset_name = std::string(preset_name);
}

void RunConfigurationWidget::set_execution_state(ExecutionState state) noexcept
{
    m_state.execution_state = state;
}

std::string RunConfigurationWidget::get_summary_label() const
{
    std::ostringstream ss;
    ss << m_state.active_target_name << " | "
       << to_string(m_state.active_mode) << " | "
       << to_string(m_state.active_architecture);
    return ss.str();
}

UI::Rect RunConfigurationWidget::calculate_popover_bounds(const UI::Rect& combo_bounds, float dpi_scale) const noexcept
{
    const float pop_w = 300.0F * dpi_scale;
    const float pop_h = 240.0F * dpi_scale;
    return UI::Rect{combo_bounds.x, combo_bounds.bottom() + 4.0F * dpi_scale, pop_w, pop_h};
}

} // namespace Zenvra::UI::Toolbar::Widgets
