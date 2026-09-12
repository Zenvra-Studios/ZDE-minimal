#pragma once

#include "Drivers/Graphics/OpenGLShader.h"
#include "Drivers/Graphics/effect/BackdropBlur.h"

#include <array>

namespace Zenvra::Graphics {

struct BlurUniforms {
    float radius = 24.0F;
    float saturation = 1.15F;
    std::array<float, 4> tint = {0.059F, 0.059F, 0.071F, 0.627F}; // RGBA [0..1]
    float noise_opacity = 0.035F;
    float texel_width = 1.0F / 1920.0F;
    float texel_height = 1.0F / 1080.0F;

    // Modern card geometry (x, y, width, height in window pixels)
    std::array<float, 4> explorer_card = {0.0F, 0.0F, 0.0F, 0.0F};
    std::array<float, 4> editor_card = {0.0F, 0.0F, 0.0F, 0.0F};
    std::array<float, 4> terminal_card = {0.0F, 0.0F, 0.0F, 0.0F};
    std::array<float, 4> shader_card = {0.0F, 0.0F, 0.0F, 0.0F};
    std::array<float, 4> popup_card = {0.0F, 0.0F, 0.0F, 0.0F};
    float card_radius = 8.0F;

    // Optional solid card colors (RGBA)
    std::array<float, 4> explorer_color = {0.0F, 0.0F, 0.0F, 0.0F};
    std::array<float, 4> editor_color = {0.0F, 0.0F, 0.0F, 0.0F};
    std::array<float, 4> terminal_color = {0.0F, 0.0F, 0.0F, 0.0F};
    std::array<float, 4> shader_color = {0.0F, 0.0F, 0.0F, 0.0F};

    int enable_blur = 1; // 1 = blur triggered, 0 = pass-through
};

class BackdropBlurPipeline {
public:
    BackdropBlurPipeline() = default;
    ~BackdropBlurPipeline();

    /// Compiles the vertex and fragment shaders from BackdropBlur.h and creates pipeline program.
    bool initialize();

    /// Binds the blur shader and applies uniform parameters.
    void apply_uniforms(const BlurUniforms& uniforms, int texture_unit = 0);

    /// Updates the solid card geometries (Explorer, Editor, Terminal, Shader Sandbox) to exclude from blur
    void set_card_regions(const std::array<float, 4>& explorer,
                          const std::array<float, 4>& editor,
                          const std::array<float, 4>& terminal,
                          const std::array<float, 4>& shader,
                          float card_radius = 8.0F);

    /// Toggles whether backdrop blur is active (triggered) or bypassed
    void set_blur_enabled(bool enabled);

    /// Render a textured quad representing the backdrop blur region.
    void render_quad(float x, float y, float width, float height);

    /// Render a normalized full-screen quad ([-1, 1] device coordinates).
    void render_fullscreen_quad();

    /// Releases shader and geometry buffers.
    void destroy();

    [[nodiscard]] bool is_ready() const noexcept { return m_shader.is_valid(); }
    [[nodiscard]] const OpenGLShader& get_shader() const noexcept { return m_shader; }

private:
    void setup_quad_geometry();

    OpenGLShader m_shader;
    unsigned int m_vbo = 0;
    bool m_geometry_initialized = false;
};

} // namespace Zenvra::Graphics
