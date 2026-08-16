#pragma once

#include "UI/Geometry.h"

#include <string>
#include <string_view>

namespace Zenvra::UI::Toolbar::Widgets
{

class ProjectBranchWidget
{
public:
    ProjectBranchWidget() = default;

    void set_project_name(std::string_view name) { m_project_name = std::string(name); }
    [[nodiscard]] std::string_view get_project_name() const noexcept { return m_project_name; }

    void set_branch_name(std::string_view branch) { m_branch_name = std::string(branch); }
    [[nodiscard]] std::string_view get_branch_name() const noexcept { return m_branch_name; }

    void set_hovered(bool hovered) noexcept { m_hovered = hovered; }
    [[nodiscard]] bool is_hovered() const noexcept { return m_hovered; }

private:
    std::string m_project_name = "ZDE-minimal";
    std::string m_branch_name = "main";
    bool m_hovered = false;
};

} // namespace Zenvra::UI::Toolbar::Widgets
