#include "UI/Editor/BraceAnimationModel.h"

#include <algorithm>
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

std::optional<TextPosition> BraceAnimationModel::get_pulsing_brace() const noexcept
{
    if (m_stage == Stage::PrimaryUp || m_stage == Stage::PrimaryDown)
    {
        return m_primary_brace;
    }
    if (m_stage == Stage::SecondaryUp || m_stage == Stage::SecondaryDown)
    {
        return m_secondary_brace;
    }
    return std::nullopt;
}

float BraceAnimationModel::get_brace_scale(const TextPosition& pos) const noexcept
{
    if (m_primary_brace && *m_primary_brace == pos)
    {
        return m_primary_scale;
    }
    if (m_secondary_brace && *m_secondary_brace == pos)
    {
        return m_secondary_scale;
    }
    return 1.0F;
}

float BraceAnimationModel::get_pulse_scale() const noexcept
{
    return std::max(m_primary_scale, m_secondary_scale);
}

void BraceAnimationModel::set_active_braces(
    std::optional<TextPosition> open,
    std::optional<TextPosition> close,
    std::optional<TextPosition> primary) noexcept
{
    const bool changed = (m_open_brace != open || m_close_brace != close || m_primary_brace != primary);

    m_open_brace = open;
    m_close_brace = close;
    m_primary_brace = primary;

    if (primary && open && *primary == *open)
    {
        m_secondary_brace = close;
    }
    else if (primary && close && *primary == *close)
    {
        m_secondary_brace = open;
    }
    else
    {
        m_secondary_brace = std::nullopt;
    }

    if (changed && has_active_braces())
    {
        m_primary_scale = 1.0F;
        m_secondary_scale = 1.0F;
        m_stage = Stage::PrimaryUp;
        m_last_tick_ms = get_current_time_ms();
    }
    else if (!has_active_braces())
    {
        clear();
    }
}

void BraceAnimationModel::clear() noexcept
{
    m_open_brace.reset();
    m_close_brace.reset();
    m_primary_brace.reset();
    m_secondary_brace.reset();
    m_primary_scale = 1.0F;
    m_secondary_scale = 1.0F;
    m_stage = Stage::Idle;
    m_last_tick_ms = 0;
}

bool BraceAnimationModel::tick() noexcept
{
    if (!has_active_braces() || m_stage == Stage::Idle)
    {
        m_primary_scale = 1.0F;
        m_secondary_scale = 1.0F;
        m_stage = Stage::Idle;
        m_last_tick_ms = 0;
        return false;
    }

    const unsigned long long current_time = get_current_time_ms();
    if (m_last_tick_ms == 0)
    {
        m_last_tick_ms = current_time;
    }

    const float dt = static_cast<float>(current_time - m_last_tick_ms) / 1000.0F;
    m_last_tick_ms = current_time;

    if (dt <= 0.0F)
    {
        return m_stage != Stage::Idle;
    }

    switch (m_stage)
    {
    case Stage::PrimaryUp:
        m_primary_scale += pulse_speed_up_per_sec * dt;
        if (m_primary_scale >= pulse_max_scale)
        {
            m_primary_scale = pulse_max_scale;
            m_stage = Stage::PrimaryDown;
        }
        break;

    case Stage::PrimaryDown:
        m_primary_scale -= pulse_speed_down_per_sec * dt;
        if (m_primary_scale <= 1.0F)
        {
            m_primary_scale = 1.0F;
            if (m_secondary_brace.has_value() && *m_secondary_brace != *m_primary_brace)
            {
                m_stage = Stage::SecondaryUp;
            }
            else
            {
                m_stage = Stage::Idle;
            }
        }
        break;

    case Stage::SecondaryUp:
        m_secondary_scale += pulse_speed_up_per_sec * dt;
        if (m_secondary_scale >= pulse_max_scale)
        {
            m_secondary_scale = pulse_max_scale;
            m_stage = Stage::SecondaryDown;
        }
        break;

    case Stage::SecondaryDown:
        m_secondary_scale -= pulse_speed_down_per_sec * dt;
        if (m_secondary_scale <= 1.0F)
        {
            m_secondary_scale = 1.0F;
            m_stage = Stage::Idle;
        }
        break;

    case Stage::Idle:
    default:
        break;
    }

    return m_stage != Stage::Idle;
}

} // namespace Zenvra::UI::Editor
