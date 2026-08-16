#pragma once

#include "UI/Geometry.h"

#include <string>
#include <string_view>

namespace Zenvra::UI::Toolbar::Widgets
{

class QuickSearchWidget
{
public:
    QuickSearchWidget() = default;

    void set_hint(std::string_view hint) { m_hint = std::string(hint); }
    [[nodiscard]] std::string_view get_hint() const noexcept { return m_hint; }

    void set_hovered(bool hovered) noexcept { m_hovered = hovered; }
    [[nodiscard]] bool is_hovered() const noexcept { return m_hovered; }

private:
    std::string m_hint = "Search Everywhere (⌘K)";
    bool m_hovered = false;
};

} // namespace Zenvra::UI::Toolbar::Widgets
