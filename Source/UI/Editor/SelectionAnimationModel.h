#pragma once

#include "UI/Geometry.h"

#include <cstddef>
#include <vector>

namespace Zenvra::UI::Editor
{

/// Drives smooth Xcode-style animated selection rectangles.
///
/// Each visible selection line is represented by a target rect. The model
/// lerps an animated rect toward each target every tick, producing a
/// fluid stretching/shrinking effect.
class SelectionAnimationModel
{
public:
    /// Replace the current target rects.  Any new entries animate from a
    /// zero-width sliver; removed entries shrink out on the next tick.
    void set_targets(const std::vector<UI::Rect>& targets);

    /// Instantly snap animated rects to target rects without animation delay.
    void snap_to_targets() noexcept;

    /// Clear all targets (selection dismissed).
    void clear() noexcept;

    /// Advance each animated rect toward its target.
    /// @return true while any rect is still animating.
    [[nodiscard]] bool tick() noexcept;

    /// Animated rects ready for drawing.
    [[nodiscard]] const std::vector<UI::Rect>& get_animated_rects() const noexcept;

    /// @return true if the model holds any rects (target or dying).
    [[nodiscard]] bool has_rects() const noexcept;

private:
    static constexpr float lerp_factor_per_sec = 42.0F;
    static constexpr float snap_threshold = 0.5F;
    unsigned long long m_last_tick_ms = 0;

    std::vector<UI::Rect> m_targets;
    std::vector<UI::Rect> m_animated;
};

} // namespace Zenvra::UI::Editor
