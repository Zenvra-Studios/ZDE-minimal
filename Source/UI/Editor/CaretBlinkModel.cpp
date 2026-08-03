#include "UI/Editor/CaretBlinkModel.h"

namespace Zenvra::UI::Editor
{

void CaretBlinkModel::reset() noexcept
{
    m_visible = true;
    m_last_toggle = std::chrono::steady_clock::now();
}

bool CaretBlinkModel::tick() noexcept
{
    const auto now = std::chrono::steady_clock::now();
    if (now - m_last_toggle < blink_interval)
    {
        return false;
    }
    const auto elapsed = now - m_last_toggle;
    const auto interval_count = elapsed / blink_interval;
    if ((interval_count % 2) != 0)
    {
        m_visible = !m_visible;
    }
    m_last_toggle += blink_interval * interval_count;
    return true;
}

bool CaretBlinkModel::is_visible() const noexcept
{
    return m_visible;
}

} // namespace Zenvra::UI::Editor
