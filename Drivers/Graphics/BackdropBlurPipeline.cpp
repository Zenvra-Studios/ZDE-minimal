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
        auto& gl = get_gl_api();
        if (gl.DeleteBuffers != nullptr) {
            gl.DeleteBuffers(1, &m_vbo);
        }
        m_vbo = 0;
    }
    m_geometry_initialized = false;
    m_shader.destroy();
}

void BackdropBlurPipeline::apply_uniforms(const BlurUniforms& uniforms, int texture_unit) {
    if (!m_shader.is_valid()) {
        return;
    }

    m_shader.bind();
    m_shader.set_uniform_1i("uSource", texture_unit);
    m_shader.set_uniform_2f("uTexelSize", uniforms.texel_width, uniforms.texel_height);
    m_shader.set_uniform_1f("uRadius", uniforms.radius);
    m_shader.set_uniform_1f("uSaturation", uniforms.saturation);
    m_shader.set_uniform_4f("uTint", uniforms.tint[0], uniforms.tint[1], uniforms.tint[2], uniforms.tint[3]);
    m_shader.set_uniform_1f("uNoiseOpacity", uniforms.noise_opacity);
}

void BackdropBlurPipeline::render_fullscreen_quad() {
    if (!m_shader.is_valid()) {
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
