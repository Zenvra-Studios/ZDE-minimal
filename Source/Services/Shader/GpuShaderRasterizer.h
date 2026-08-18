#pragma once

#include "Drivers/Graphics/OpenGLFunctions.h"
#include "Drivers/Graphics/OpenGLShader.h"
#include "Services/Shader/ShaderTypes.h"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Zenvra::Services::Shader
{

class GpuShaderRasterizer
{
public:
    GpuShaderRasterizer();
    ~GpuShaderRasterizer();

    GpuShaderRasterizer(const GpuShaderRasterizer&) = delete;
    GpuShaderRasterizer& operator=(const GpuShaderRasterizer&) = delete;
    GpuShaderRasterizer(GpuShaderRasterizer&&) noexcept;
    GpuShaderRasterizer& operator=(GpuShaderRasterizer&&) noexcept;

    [[nodiscard]] bool initialize(int width, int height);
    void resize(int width, int height);

    [[nodiscard]] bool compile(std::string_view glsl_code, std::string& out_error);
    [[nodiscard]] bool render(const ShaderUniforms& uniforms, const std::array<ShaderChannel, 4>& channels);

    [[nodiscard]] bool is_supported() const noexcept;
    [[nodiscard]] bool is_valid() const noexcept;

    [[nodiscard]] std::span<const std::uint32_t> get_rendered_pixels() const noexcept
    {
        return m_pixel_buffer;
    }
    [[nodiscard]] int get_width() const noexcept { return m_width; }
    [[nodiscard]] int get_height() const noexcept { return m_height; }

    void destroy();

private:
    void ensure_fbo(int width, int height);
    void destroy_fbo();
    void update_channel_textures(const std::array<ShaderChannel, 4>& channels);
    static std::string build_full_fragment_shader(std::string_view user_code);

    Graphics::OpenGLShader m_shader;
    std::string m_active_code;
    std::string m_last_error;

    int m_width = 640;
    int m_height = 360;
    int m_fbo_width = 0;
    int m_fbo_height = 0;

    unsigned int m_fbo = 0;
    unsigned int m_color_texture = 0;
    unsigned int m_depth_rbo = 0;
    std::array<unsigned int, 4> m_channel_textures{0, 0, 0, 0};
    std::array<ChannelTextureKind, 4> m_loaded_channel_kinds{
        ChannelTextureKind::Empty, ChannelTextureKind::Empty,
        ChannelTextureKind::Empty, ChannelTextureKind::Empty
    };
    std::array<std::size_t, 4> m_loaded_channel_sizes{0, 0, 0, 0};

    std::vector<std::uint32_t> m_pixel_buffer;
    bool m_initialized = false;
    bool m_context_valid = false;

    struct ContextImpl;
    std::unique_ptr<ContextImpl> m_context;
};

} // namespace Zenvra::Services::Shader
