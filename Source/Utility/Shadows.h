#pragma once

#include "UI/Geometry.h"
#include "UI/Theme/StudioTheme.h"
#include <array>
#include <cstdint>
#include <span>

namespace Zenvra::Utility {

struct ShadowLayer {
  float dx = 0.0F;
  float dy = 0.0F;
  float spread = 0.0F;
  uint8_t alpha = 0;
};

// macOS ultra-thin, soft diffuse ambient multi-layer acrylic shadow
inline constexpr std::array<ShadowLayer, 5> macos_card_shadows = {{
    {0.0F, 8.0F, 16.0F, 8},  // Ambient ultra-soft atmospheric haze
    {0.0F, 5.0F, 9.0F, 14},  // Soft outer glow
    {0.0F, 3.0F, 4.5F, 22},  // Soft mid-shadow
    {0.0F, 1.5F, 2.0F, 32},  // Soft near-shadow
    {0.0F, 0.5F, 0.8F, 42},  // Ultra-thin contact shadow
}};

// Subtle modal drop shadow
inline constexpr std::array<ShadowLayer, 4> macos_modal_shadows = {{
    {0.0F, 12.0F, 24.0F, 12},
    {0.0F, 8.0F, 16.0F, 20},
    {0.0F, 4.0F, 8.0F, 28},
    {0.0F, 1.0F, 2.0F, 40},
}};

template <typename Callback>
inline void for_each_shadow_layer(const UI::Rect &bounds, float radius,
                                  float dpi_scale,
                                  std::span<const ShadowLayer> layers,
                                  Callback &&callback) {
  for (const auto &layer : layers) {
    const float spread = layer.spread * dpi_scale;
    const UI::Rect layer_rect{
        bounds.x - spread + layer.dx * dpi_scale,
        bounds.y - spread + layer.dy * dpi_scale,
        bounds.width + spread * 2.0F,
        bounds.height + spread * 2.0F,
    };
    const float layer_radius = radius + spread;
    const UI::Theme::Color color{0, 0, 0, layer.alpha};
    callback(layer_rect, color, layer_radius);
  }
}

} // namespace Zenvra::Utility