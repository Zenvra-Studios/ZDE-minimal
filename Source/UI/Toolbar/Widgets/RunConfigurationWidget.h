#pragma once

#include "UI/Geometry.h"
#include "UI/Toolbar/ToolbarTypes.h"

#include <string>
#include <string_view>
#include <vector>

namespace Zenvra::UI::Toolbar::Widgets
{

class RunConfigurationWidget
{
public:
    RunConfigurationWidget() = default;

    void set_state(RunConfigurationState state) noexcept;
    [[nodiscard]] const RunConfigurationState& get_state() const noexcept { return m_state; }

    void set_active_target(std::string_view target_name);
    void set_active_mode(BuildConfigurationMode mode) noexcept;
    void set_active_architecture(TargetArchitecture arch) noexcept;
    void set_active_preset(std::string_view preset_name);
    void set_execution_state(ExecutionState state) noexcept;

    void set_hovered(bool hovered) noexcept { m_hovered = hovered; }
    [[nodiscard]] bool is_hovered() const noexcept { return m_hovered; }

    void toggle_popover() noexcept { m_popover_open = !m_popover_open; }
    void close_popover() noexcept { m_popover_open = false; }
    [[nodiscard]] bool is_popover_open() const noexcept { return m_popover_open; }

    [[nodiscard]] std::string get_summary_label() const;
    [[nodiscard]] UI::Rect calculate_popover_bounds(const UI::Rect& combo_bounds, float dpi_scale) const noexcept;

private:
    RunConfigurationState m_state;
    bool m_hovered = false;
    bool m_popover_open = false;
};

} // namespace Zenvra::UI::Toolbar::Widgets
