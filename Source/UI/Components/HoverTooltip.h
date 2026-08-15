#pragma once

#include "UI/Geometry.h"
#include "UI/Theme/StudioTheme.h"

#include <string>
#include <string_view>

namespace Zenvra::UI::Components
{

class HoverTooltip
{
public:
    HoverTooltip() = default;

    void show(std::string markdown_content, float x, float y);
    void hide() noexcept;
    [[nodiscard]] bool is_visible() const noexcept { return m_visible; }

    [[nodiscard]] const std::string& get_content() const noexcept { return m_content; }
    [[nodiscard]] float get_x() const noexcept { return m_x; }
    [[nodiscard]] float get_y() const noexcept { return m_y; }

    [[nodiscard]] Rect calculate_bounds(float content_width = 300.0F, float content_height = 80.0F) const noexcept;

private:
    bool m_visible = false;
    float m_x = 0.0F;
    float m_y = 0.0F;
    std::string m_content;
};

} // namespace Zenvra::UI::Components
