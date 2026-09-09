#pragma once

#include "UI/Editor/TextDocumentModel.h"

#include <optional>

namespace Zenvra::UI::Editor {

/// Model for driving a sequential "zoom/pulse" animation on matched delimiters
/// (Xcode style).
class BraceAnimationModel {
public:
  enum class Stage { Idle, PrimaryUp, PrimaryDown, SecondaryUp, SecondaryDown };

  /// Check if there is an active brace pair being highlighted/animated.
  [[nodiscard]] bool has_active_braces() const noexcept;

  /// The active opening brace position, if any.
  [[nodiscard]] std::optional<TextPosition> get_open_brace() const noexcept;

  /// The active closing brace position, if any.
  [[nodiscard]] std::optional<TextPosition> get_close_brace() const noexcept;

  /// The active bracket that is currently pulsing its scale, if any.
  [[nodiscard]] std::optional<TextPosition> get_pulsing_brace() const noexcept;

  /// Check if animation is actively running.
  [[nodiscard]] bool is_animating() const noexcept {
    return m_stage != Stage::Idle;
  }

  /// Current visual scale of a specific brace position (1.0F to 1.28F).
  [[nodiscard]] float get_brace_scale(const TextPosition &pos) const noexcept;

  /// Overall pulse scale for backward compatibility.
  [[nodiscard]] float get_pulse_scale() const noexcept;

  /// Set a new pair of active braces. Triggers sequential pulse animation if
  /// changed. Primary brace (where caret is) pulses first, then secondary brace
  /// (partner) pulses at the end.
  void set_active_braces(
      std::optional<TextPosition> open, std::optional<TextPosition> close,
      std::optional<TextPosition> primary = std::nullopt) noexcept;

  /// Clear active braces.
  void clear() noexcept;

  /// Advance the pulse animation. Returns true if still animating.
  [[nodiscard]] bool tick() noexcept;

private:
  std::optional<TextPosition> m_open_brace;
  std::optional<TextPosition> m_close_brace;
  std::optional<TextPosition> m_primary_brace;
  std::optional<TextPosition> m_secondary_brace;

  float m_primary_scale = 1.0F;
  float m_secondary_scale = 1.0F;
  Stage m_stage = Stage::Idle;

  unsigned long long m_last_tick_ms = 0;

  static constexpr float pulse_max_scale = 1.28F;
  static constexpr float pulse_speed_up_per_sec = 4.5F;
  static constexpr float pulse_speed_down_per_sec = 3.2F;
};

} // namespace Zenvra::UI::Editor
