#include "Drivers/Graphics/BackdropBlurPipeline.h"

namespace Zenvra::Graphics {

BackdropBlurPipeline::~BackdropBlurPipeline() {
    destroy();
}

bool BackdropBlurPipeline::initialize() {
    destroy();
    return m_shader.compile_from_source(
        Shaders::BlurVertexShader,
        Shaders::BlurFragmentShader
    );
}

void BackdropBlurPipeline::destroy() {
    if (m_vbo != 0) {
        if (has_active_gl_context()) {
            auto& gl = get_gl_api();
            if (gl.DeleteBuffers != nullptr) {
                gl.DeleteBuffers(1, &m_vbo);
            }
        }
        m_vbo = 0;
    }
    m_geometry_initialized = false;
    m_shader.destroy();
}

void BackdropBlurPipeline::apply_uniforms(const BlurUniforms& uniforms, int texture_unit) {
    if (!m_shader.is_valid() || !has_active_gl_context()) {
        return;
    }

    m_shader.bind();
    m_shader.set_uniform_1i("uSource", texture_unit);
    m_shader.set_uniform_2f("uTexelSize", uniforms.texel_width, uniforms.texel_height);
    m_shader.set_uniform_1f("uRadius", uniforms.radius);
    m_shader.set_uniform_1f("uSaturation", uniforms.saturation);
    m_shader.set_uniform_4f("uTint", uniforms.tint[0], uniforms.tint[1], uniforms.tint[2], uniforms.tint[3]);
    m_shader.set_uniform_1f("uNoiseOpacity", uniforms.noise_opacity);

    // Modern card geometry (Explorer, Editor, Terminal, Shader Sandbox) to preserve solid regions
    m_shader.set_uniform_4f("uExplorerCard", uniforms.explorer_card[0], uniforms.explorer_card[1], uniforms.explorer_card[2], uniforms.explorer_card[3]);
    m_shader.set_uniform_4f("uEditorCard", uniforms.editor_card[0], uniforms.editor_card[1], uniforms.editor_card[2], uniforms.editor_card[3]);
    m_shader.set_uniform_4f("uTerminalCard", uniforms.terminal_card[0], uniforms.terminal_card[1], uniforms.terminal_card[2], uniforms.terminal_card[3]);
    m_shader.set_uniform_4f("uShaderCard", uniforms.shader_card[0], uniforms.shader_card[1], uniforms.shader_card[2], uniforms.shader_card[3]);
    m_shader.set_uniform_4f("uPopupCard", uniforms.popup_card[0], uniforms.popup_card[1], uniforms.popup_card[2], uniforms.popup_card[3]);
    m_shader.set_uniform_1f("uCardRadius", uniforms.card_radius);

    // Optional solid color overrides
    m_shader.set_uniform_4f("uExplorerColor", uniforms.explorer_color[0], uniforms.explorer_color[1], uniforms.explorer_color[2], uniforms.explorer_color[3]);
    m_shader.set_uniform_4f("uEditorColor", uniforms.editor_color[0], uniforms.editor_color[1], uniforms.editor_color[2], uniforms.editor_color[3]);
    m_shader.set_uniform_4f("uTerminalColor", uniforms.terminal_color[0], uniforms.terminal_color[1], uniforms.terminal_color[2], uniforms.terminal_color[3]);
    m_shader.set_uniform_4f("uShaderColor", uniforms.shader_color[0], uniforms.shader_color[1], uniforms.shader_color[2], uniforms.shader_color[3]);

    m_shader.set_uniform_1i("uEnableBlur", uniforms.enable_blur);
}

void BackdropBlurPipeline::set_card_regions(const std::array<float, 4>& explorer,
                                            const std::array<float, 4>& editor,
                                            const std::array<float, 4>& terminal,
                                            const std::array<float, 4>& shader,
                                            float card_radius) {
    if (!m_shader.is_valid() || !has_active_gl_context()) {
        return;
    }
    m_shader.bind();
    m_shader.set_uniform_4f("uExplorerCard", explorer[0], explorer[1], explorer[2], explorer[3]);
    m_shader.set_uniform_4f("uEditorCard", editor[0], editor[1], editor[2], editor[3]);
    m_shader.set_uniform_4f("uTerminalCard", terminal[0], terminal[1], terminal[2], terminal[3]);
    m_shader.set_uniform_4f("uShaderCard", shader[0], shader[1], shader[2], shader[3]);
    m_shader.set_uniform_1f("uCardRadius", card_radius);
}

void BackdropBlurPipeline::set_blur_enabled(bool enabled) {
    if (!m_shader.is_valid() || !has_active_gl_context()) {
        return;
    }
    m_shader.bind();
    m_shader.set_uniform_1i("uEnableBlur", enabled ? 1 : 0);
}

void BackdropBlurPipeline::render_fullscreen_quad() {
    if (!m_shader.is_valid() || !has_active_gl_context()) {
        return;
    }

    m_shader.bind();

    // Standard OpenGL 1.20 immediate mode quad / fallback
    glBegin(GL_QUADS);
    glTexCoord2f(0.0F, 0.0F); glVertex2f(-1.0F, -1.0F);
    glTexCoord2f(1.0F, 0.0F); glVertex2f( 1.0F, -1.0F);
    glTexCoord2f(1.0F, 1.0F); glVertex2f( 1.0F,  1.0F);
    glTexCoord2f(0.0F, 1.0F); glVertex2f(-1.0F,  1.0F);
    glEnd();

    m_shader.unbind();
}

void BackdropBlurPipeline::render_quad(float x, float y, float width, float height) {
    if (!m_shader.is_valid()) {
        return;
    }

    m_shader.bind();

    glBegin(GL_QUADS);
    glTexCoord2f(0.0F, 0.0F); glVertex2f(x, y);
    glTexCoord2f(1.0F, 0.0F); glVertex2f(x + width, y);
    glTexCoord2f(1.0F, 1.0F); glVertex2f(x + width, y + height);
    glTexCoord2f(0.0F, 1.0F); glVertex2f(x, y + height);
    glEnd();

    m_shader.unbind();
}

} // namespace Zenvra::Graphics
