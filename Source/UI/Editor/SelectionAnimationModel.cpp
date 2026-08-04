#include "UI/Editor/SelectionAnimationModel.h"

#include <algorithm>
#include <cmath>

#include <chrono>

namespace Zenvra::UI::Editor
{

static unsigned long long get_current_time_ms()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

void SelectionAnimationModel::set_targets(const std::vector<UI::Rect>& targets)
{
    m_targets = targets;

    while (m_animated.size() < m_targets.size())
    {
        const UI::Rect& target = m_targets[m_animated.size()];
        m_animated.push_back(UI::Rect{
            target.x + target.width * 0.5F,
            target.y,
            0.0F,
            target.height,
        });
    }
}

void SelectionAnimationModel::clear() noexcept
{
    m_targets.clear();
}

bool SelectionAnimationModel::tick() noexcept
{
    const unsigned long long current_time = get_current_time_ms();
    if (m_last_tick_ms == 0) m_last_tick_ms = current_time;
    const float dt = static_cast<float>(current_time - m_last_tick_ms) / 1000.0F;
    m_last_tick_ms = current_time;

    bool still_animating = false;

    if (dt <= 0.0F) return m_animated.size() > m_targets.size();

    // Exp decay interpolation formula: val += (target - val) * (1 - exp(-speed * dt))
    // We approximate it with lerp for small dt, or just use the exact formula:
    const float lerp_amount = 1.0F - std::exp(-lerp_factor_per_sec * dt);

    const std::size_t count = m_animated.size();
    for (std::size_t index = 0; index < count; ++index)
    {
        UI::Rect target;
        if (index < m_targets.size())
        {
            target = m_targets[index];
        }
        else
        {
            target = UI::Rect{
                m_animated[index].x + m_animated[index].width * 0.5F,
                m_animated[index].y,
                0.0F,
                m_animated[index].height,
            };
        }

        auto lerp_component = [&](float& current, float goal) {
            const float delta = goal - current;
            if (std::abs(delta) > snap_threshold)
            {
                current += delta * lerp_amount;
                still_animating = true;
            }
            else
            {
                current = goal;
            }
        };

        lerp_component(m_animated[index].x, target.x);
        lerp_component(m_animated[index].y, target.y);
        lerp_component(m_animated[index].width, target.width);
        lerp_component(m_animated[index].height, target.height);
    }

    // Prune trailing rects that have fully shrunk and have no target.
    while (m_animated.size() > m_targets.size())
    {
        const UI::Rect& back = m_animated.back();
        if (back.width <= snap_threshold)
        {
            m_animated.pop_back();
        }
        else
        {
            break;
        }
    }

    return still_animating;
}

const std::vector<UI::Rect>& SelectionAnimationModel::get_animated_rects() const noexcept
{
    return m_animated;
}

bool SelectionAnimationModel::has_rects() const noexcept
{
    return !m_animated.empty();
}

} // namespace Zenvra::UI::Editor
