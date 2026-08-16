#pragma once

#include "UI/Toolbar/ToolbarLayout.h"
#include "UI/Toolbar/Widgets/ActionButtonGroup.h"
#include "UI/Toolbar/Widgets/ProjectBranchWidget.h"
#include "UI/Toolbar/Widgets/QuickSearchWidget.h"
#include "UI/Toolbar/Widgets/RunConfigurationWidget.h"

namespace Zenvra::UI::Toolbar
{

class StudioMainToolbar
{
public:
    StudioMainToolbar() = default;

    void update_dpi_scale(float dpi_scale) noexcept { m_dpi_scale = dpi_scale; }
    [[nodiscard]] float get_dpi_scale() const noexcept { return m_dpi_scale; }

    [[nodiscard]] Widgets::RunConfigurationWidget& get_run_config_widget() noexcept { return m_run_config_widget; }
    [[nodiscard]] const Widgets::RunConfigurationWidget& get_run_config_widget() const noexcept { return m_run_config_widget; }

    [[nodiscard]] Widgets::ActionButtonGroup& get_action_button_group() noexcept { return m_action_button_group; }
    [[nodiscard]] const Widgets::ActionButtonGroup& get_action_button_group() const noexcept { return m_action_button_group; }

    [[nodiscard]] Widgets::ProjectBranchWidget& get_project_branch_widget() noexcept { return m_branch_widget; }
    [[nodiscard]] const Widgets::ProjectBranchWidget& get_project_branch_widget() const noexcept { return m_branch_widget; }

    [[nodiscard]] Widgets::QuickSearchWidget& get_quick_search_widget() noexcept { return m_search_widget; }
    [[nodiscard]] const Widgets::QuickSearchWidget& get_quick_search_widget() const noexcept { return m_search_widget; }

    [[nodiscard]] ToolbarLayoutResult layout(float container_width, float content_top) const noexcept;

private:
    float m_dpi_scale = 1.0F;
    Widgets::RunConfigurationWidget m_run_config_widget;
    Widgets::ActionButtonGroup m_action_button_group;
    Widgets::ProjectBranchWidget m_branch_widget;
    Widgets::QuickSearchWidget m_search_widget;
};

} // namespace Zenvra::UI::Toolbar
