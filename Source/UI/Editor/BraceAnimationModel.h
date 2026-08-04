#pragma once

#include "UI/Editor/TextDocumentModel.h"

#include <optional>

namespace Zenvra::UI::Editor
{

/// Model for driving a "zoom/pulse" animation on matched curly braces.
class BraceAnimationModel
{
public:
    /// Check if there is an active brace pair being highlighted/animated.
    [[nodiscard]] bool has_active_braces() const noexcept;

    /// The active opening brace position, if any.
    [[nodiscard]] std::optional<TextPosition> get_open_brace() const noexcept;

    /// The active closing brace position, if any.
    [[nodiscard]] std::optional<TextPosition> get_close_brace() const noexcept;

    /// Current visual scale of the highlight box (e.g. 1.0 to 1.3).
    [[nodiscard]] float get_pulse_scale() const noexcept;

    /// Set a new pair of active braces. Triggers a pulse animation if they differ
    /// from the previously active braces.
    void set_active_braces(std::optional<TextPosition> open, std::optional<TextPosition> close) noexcept;

    /// Clear active braces.
    void clear() noexcept;

    /// Advance the pulse animation. Returns true if still animating.
    [[nodiscard]] bool tick() noexcept;

private:
    std::optional<TextPosition> m_open_brace;
    std::optional<TextPosition> m_close_brace;

    float m_pulse_scale = 1.0F;
    bool m_pulsing_up = false;
    // Track time for frame-rate independent animation
    unsigned long long m_last_tick_ms = 0; 
    
    static constexpr float pulse_max_scale = 1.6F; // Increased from 1.25F for a more dramatic pop
    static constexpr float pulse_speed_up_per_sec = 4.0F; // reaches 1.6 in 0.15s
    static constexpr float pulse_speed_down_per_sec = 2.0F; // reaches 1.0 in 0.3s
};

} // namespace Zenvra::UI::Editor
