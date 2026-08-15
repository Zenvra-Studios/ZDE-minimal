#include "UI/Components/SignatureHelpWidget.h"

namespace Zenvra::UI::Components
{

void SignatureHelpWidget::show(Language::Protocol::SignatureHelp help, float x, float y)
{
    m_help = std::move(help);
    m_x = x;
    m_y = y;
    m_visible = !m_help.signatures.empty();
}

void SignatureHelpWidget::hide() noexcept
{
    m_visible = false;
    m_help.signatures.clear();
}

Rect SignatureHelpWidget::calculate_bounds(float width, float height) const noexcept
{
    return Rect{
        .x = m_x,
        .y = m_y - height - 6.0F,
        .width = width,
        .height = height
    };
}

} // namespace Zenvra::UI::Components
