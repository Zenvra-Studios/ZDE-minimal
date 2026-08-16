#pragma once

#include "UI/Geometry.h"
#include "UI/Toolbar/ToolbarTypes.h"

#include <optional>

namespace Zenvra::UI::Toolbar::Widgets
{

class ActionButtonGroup
{
public:
    ActionButtonGroup() = default;

    void set_execution_state(ExecutionState state) noexcept { m_execution_state = state; }
    [[nodiscard]] ExecutionState get_execution_state() const noexcept { return m_execution_state; }

    void set_hovered_action(std::optional<ToolbarActionType> action) noexcept { m_hovered_action = action; }
    [[nodiscard]] std::optional<ToolbarActionType> get_hovered_action() const noexcept { return m_hovered_action; }

private:
    ExecutionState m_execution_state = ExecutionState::Idle;
    std::optional<ToolbarActionType> m_hovered_action;
};

} // namespace Zenvra::UI::Toolbar::Widgets
