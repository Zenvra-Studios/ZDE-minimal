#include "UI/Editor/BraceAnimationModel.h"

#include <cmath>

#include <chrono>

namespace Zenvra::UI::Editor
{

static unsigned long long get_current_time_ms()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

bool BraceAnimationModel::has_active_braces() const noexcept
{
    return m_open_brace.has_value() || m_close_brace.has_value();
}

std::optional<TextPosition> BraceAnimationModel::get_open_brace() const noexcept
{
    return m_open_brace;
}

std::optional<TextPosition> BraceAnimationModel::get_close_brace() const noexcept
{
    return m_close_brace;
}

float BraceAnimationModel::get_pulse_scale() const noexcept
{
    return m_pulse_scale;
}

void BraceAnimationModel::set_active_braces(
    std::optional<TextPosition> open,
    std::optional<TextPosition> close) noexcept
{
    bool changed = false;
    if (m_open_brace != open || m_close_brace != close)
    {
        changed = true;
    }

    m_open_brace = open;
    m_close_brace = close;

    if (changed && has_active_braces())
    {
        m_pulsing_up = true;
        m_last_tick_ms = get_current_time_ms();
    }
}

void BraceAnimationModel::clear() noexcept
{
    m_open_brace.reset();
    m_close_brace.reset();
    m_pulse_scale = 1.0F;
    m_pulsing_up = false;
    m_last_tick_ms = 0;
}

bool BraceAnimationModel::tick() noexcept
{
    if (!has_active_braces())
    {
        m_pulse_scale = 1.0F;
        m_pulsing_up = false;
        m_last_tick_ms = 0;
        return false;
    }

    const unsigned long long current_time = get_current_time_ms();
    if (m_last_tick_ms == 0) m_last_tick_ms = current_time;
    
    const float dt = static_cast<float>(current_time - m_last_tick_ms) / 1000.0F;
    m_last_tick_ms = current_time;
    
    if (dt <= 0.0F) return m_pulsing_up || m_pulse_scale > 1.0F;

    bool animating = false;

    if (m_pulsing_up)
    {
        m_pulse_scale += pulse_speed_up_per_sec * dt;
        if (m_pulse_scale >= pulse_max_scale)
        {
            m_pulse_scale = pulse_max_scale;
            m_pulsing_up = false;
        }
        animating = true;
    }
    else if (m_pulse_scale > 1.0F)
    {
        m_pulse_scale -= pulse_speed_down_per_sec * dt;
        if (m_pulse_scale <= 1.0F)
        {
            m_pulse_scale = 1.0F;
        }
        else
        {
            animating = true;
        }
    }

    return animating;
}

} // namespace Zenvra::UI::Editor
