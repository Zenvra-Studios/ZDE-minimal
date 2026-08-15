#include "UI/Components/HoverTooltip.h"

namespace Zenvra::UI::Components
{

void HoverTooltip::show(std::string markdown_content, float x, float y)
{
    m_content = std::move(markdown_content);
    m_x = x;
    m_y = y;
    m_visible = !m_content.empty();
}

void HoverTooltip::hide() noexcept
{
    m_visible = false;
    m_content.clear();
}

Rect HoverTooltip::calculate_bounds(float content_width, float content_height) const noexcept
{
    return Rect{
        .x = m_x,
        .y = m_y - content_height - 6.0F,
        .width = content_width,
        .height = content_height
    };
}

} // namespace Zenvra::UI::Components
