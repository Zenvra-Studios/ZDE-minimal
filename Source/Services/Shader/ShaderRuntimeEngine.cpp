#include "Services/Shader/ShaderRuntimeEngine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <mutex>
#include <span>
#include <vector>

namespace Zenvra::Services::Shader
{

ShaderRuntimeEngine::ShaderRuntimeEngine()
{
    m_last_frame_time = std::chrono::steady_clock::now();
    m_fps_sample_time = m_last_frame_time;
}

ShaderRuntimeEngine::~ShaderRuntimeEngine() = default;

void ShaderRuntimeEngine::initialize()
{
    // Initialize GPU and CPU rasterizers
    static_cast<void>(m_gpu_rasterizer.initialize(640, 360));

    // Setup default noise channels
    set_channel_texture(0, ChannelTextureKind::PerlinNoise);
    set_channel_texture(1, ChannelTextureKind::VoronoiCells);

    // Load initial starter preset
    const auto presets = ShaderCompiler::get_starter_presets();
    if (!presets.empty())
    {
        load_preset(0);
    }
}

void ShaderRuntimeEngine::apply_effective_resolution()
{
    float factor = 1.0F;
    switch (m_resolution_scale)
    {
    case ResolutionScale::Full:
        factor = 1.0F;
        break;
    case ResolutionScale::Half:
        factor = 0.5F;
        break;
    case ResolutionScale::Quarter:
        factor = 0.25F;
        break;
    }

    const int effective_w = std::max(static_cast<int>(static_cast<float>(m_viewport_width) * factor), 16);
    const int effective_h = std::max(static_cast<int>(static_cast<float>(m_viewport_height) * factor), 16);

    m_uniforms.resolution[0] = static_cast<float>(effective_w);
    m_uniforms.resolution[1] = static_cast<float>(effective_h);

    m_rasterizer.resize(effective_w, effective_h);
    m_gpu_rasterizer.resize(effective_w, effective_h);
}

void ShaderRuntimeEngine::resize(int viewport_width, int viewport_height)
{
    std::lock_guard<std::mutex> lock(m_engine_mutex);
    m_viewport_width = std::max(viewport_width, 32);
    m_viewport_height = std::max(viewport_height, 32);
    apply_effective_resolution();
    m_is_dirty = true;
}

void ShaderRuntimeEngine::set_source_code(std::string_view source_code)
{
    {
        std::lock_guard<std::mutex> lock(m_engine_mutex);
        if (m_source_code == source_code)
        {
            return;
        }
        m_source_code = std::string(source_code);
    }
    trigger_compile_internal();
}

void ShaderRuntimeEngine::play() noexcept
{
    m_is_playing = true;
    m_last_frame_time = std::chrono::steady_clock::now();
}

void ShaderRuntimeEngine::pause() noexcept
{
    m_is_playing = false;
    m_status = ShaderStatus::Paused;
}

void ShaderRuntimeEngine::toggle_playback() noexcept
{
    if (m_is_playing)
    {
        pause();
    }
    else
    {
        play();
    }
}

void ShaderRuntimeEngine::reset_time() noexcept
{
    std::lock_guard<std::mutex> lock(m_engine_mutex);
    m_uniforms.time = 0.0F;
    m_uniforms.frame = 0;
    m_last_frame_time = std::chrono::steady_clock::now();
    m_is_dirty = true;
}

void ShaderRuntimeEngine::set_playback_speed(float speed) noexcept
{
    m_playback_speed = std::clamp(speed, 0.1F, 10.0F);
}

void ShaderRuntimeEngine::set_resolution_scale(ResolutionScale scale) noexcept
{
    std::lock_guard<std::mutex> lock(m_engine_mutex);
    m_resolution_scale = scale;
    apply_effective_resolution();
    m_is_dirty = true;
}

void ShaderRuntimeEngine::cycle_resolution_scale() noexcept
{
    switch (m_resolution_scale)
    {
    case ResolutionScale::Full:
        set_resolution_scale(ResolutionScale::Half);
        break;
    case ResolutionScale::Half:
        set_resolution_scale(ResolutionScale::Quarter);
        break;
    case ResolutionScale::Quarter:
        set_resolution_scale(ResolutionScale::Full);
        break;
    }
}

void ShaderRuntimeEngine::set_render_backend(RenderBackend backend) noexcept
{
    std::lock_guard<std::mutex> lock(m_engine_mutex);
    m_render_backend = backend;
    m_is_dirty = true;
}

void ShaderRuntimeEngine::toggle_render_backend() noexcept
{
    std::lock_guard<std::mutex> lock(m_engine_mutex);
    m_render_backend = (m_render_backend == RenderBackend::Cpu) ? RenderBackend::Gpu : RenderBackend::Cpu;
    m_is_dirty = true;
}

bool ShaderRuntimeEngine::is_gpu_supported() const noexcept
{
    return m_gpu_rasterizer.is_supported();
}

void ShaderRuntimeEngine::set_mouse(float x, float y, bool is_down)
{
    std::lock_guard<std::mutex> lock(m_engine_mutex);
    m_uniforms.mouse[0] = x;
    m_uniforms.mouse[1] = m_uniforms.resolution[1] - y; // Flip to Shadertoy Y coordinate
    if (is_down)
    {
        m_uniforms.mouse[2] = x;
        m_uniforms.mouse[3] = m_uniforms.resolution[1] - y;
    }
    else
    {
        m_uniforms.mouse[2] = -std::abs(m_uniforms.mouse[2]);
        m_uniforms.mouse[3] = -std::abs(m_uniforms.mouse[3]);
    }
    m_is_dirty = true;
}

bool ShaderRuntimeEngine::update_and_render()
{
    const auto now = std::chrono::steady_clock::now();
    const float dt = std::chrono::duration<float>(now - m_last_frame_time).count();

    // Support smooth high-refresh rates up to 240 FPS while preventing runaway CPU spinning
    constexpr float min_frame_interval = 1.0F / 240.0F;
    if (m_is_playing && dt < min_frame_interval && !m_is_dirty)
    {
        return false;
    }

    m_last_frame_time = now;

    bool needs_redraw = false;

    if (m_is_playing)
    {
        const float clamped_dt = std::min(dt, 0.1F);
        m_uniforms.time_delta = clamped_dt * m_playback_speed;
        m_uniforms.time += m_uniforms.time_delta;
        m_uniforms.frame++;
        needs_redraw = true;
    }

    if (!needs_redraw && !m_is_dirty)
    {
        return false;
    }
    m_is_dirty = false;

    const auto render_start = std::chrono::steady_clock::now();

    {
        std::lock_guard<std::mutex> lock(m_engine_mutex);
        if (m_render_backend == RenderBackend::Gpu)
        {
            if (!m_gpu_rasterizer.is_valid() && !m_source_code.empty())
            {
                std::string err;
                static_cast<void>(m_gpu_rasterizer.compile(m_source_code, err));
            }

            if (m_gpu_rasterizer.is_valid())
            {
                const bool gpu_ok = m_gpu_rasterizer.render(m_uniforms, m_channels);
                if (gpu_ok)
                {
                    m_status = m_is_playing ? ShaderStatus::Running : ShaderStatus::Paused;
                }
            }
            else if (m_active_shader)
            {
                m_rasterizer.render_frame(
                    m_active_shader,
                    m_uniforms,
                    m_channels,
                    m_resolution_scale);
                m_status = m_is_playing ? ShaderStatus::Running : ShaderStatus::Paused;
            }
        }
        else if (m_active_shader)
        {
            m_rasterizer.render_frame(
                m_active_shader,
                m_uniforms,
                m_channels,
                m_resolution_scale);
            m_status = m_is_playing ? ShaderStatus::Running : ShaderStatus::Paused;
        }
    }

    const auto render_end = std::chrono::steady_clock::now();
    m_frame_time_ms = std::chrono::duration<float, std::milli>(render_end - render_start).count();

    m_fps_frame_count++;
    const float fps_duration = std::chrono::duration<float>(now - m_fps_sample_time).count();
    if (fps_duration >= 0.5F)
    {
        m_current_fps = static_cast<float>(m_fps_frame_count) / fps_duration;
        m_fps_frame_count = 0;
        m_fps_sample_time = now;
    }

    return true;
}

std::span<const std::uint32_t> ShaderRuntimeEngine::get_rendered_pixels() const noexcept
{
    if (m_render_backend == RenderBackend::Gpu && m_gpu_rasterizer.is_valid())
    {
        const auto gpu_px = m_gpu_rasterizer.get_rendered_pixels();
        if (!gpu_px.empty())
        {
            return gpu_px;
        }
    }
    return m_rasterizer.get_pixel_buffer();
}

int ShaderRuntimeEngine::get_rendered_width() const noexcept
{
    if (m_render_backend == RenderBackend::Gpu && m_gpu_rasterizer.is_valid())
    {
        return m_gpu_rasterizer.get_width();
    }
    return m_rasterizer.get_width();
}

int ShaderRuntimeEngine::get_rendered_height() const noexcept
{
    if (m_render_backend == RenderBackend::Gpu && m_gpu_rasterizer.is_valid())
    {
        return m_gpu_rasterizer.get_height();
    }
    return m_rasterizer.get_height();
}

void ShaderRuntimeEngine::set_channel_texture(std::size_t channel_index, ChannelTextureKind kind)
{
    if (channel_index >= m_channels.size())
    {
        return;
    }
    std::lock_guard<std::mutex> lock(m_engine_mutex);
    ShaderChannel& ch = m_channels[channel_index];
    ch.kind = kind;
    ch.width = 256;
    ch.height = 256;

    switch (kind)
    {
    case ChannelTextureKind::PerlinNoise:
        CpuShaderRasterizer::generate_perlin_noise(ch.pixels, ch.width, ch.height);
        break;
    case ChannelTextureKind::VoronoiCells:
        CpuShaderRasterizer::generate_voronoi_texture(ch.pixels, ch.width, ch.height);
        break;
    case ChannelTextureKind::Checkerboard:
        CpuShaderRasterizer::generate_checkerboard(ch.pixels, ch.width, ch.height);
        break;
    case ChannelTextureKind::Empty:
    case ChannelTextureKind::SimplexNoise:
    case ChannelTextureKind::AudioSpectrum:
        ch.pixels.clear();
        break;
    }
    m_is_dirty = true;
}

const ShaderChannel& ShaderRuntimeEngine::get_channel(std::size_t channel_index) const noexcept
{
    static const ShaderChannel empty_channel;
    if (channel_index >= m_channels.size())
    {
        return empty_channel;
    }
    return m_channels[channel_index];
}

bool ShaderRuntimeEngine::export_snapshot_bmp(const std::string& filepath) const
{
    std::lock_guard<std::mutex> lock(m_engine_mutex);
    const int w = get_rendered_width();
    const int h = get_rendered_height();
    const auto pixels = get_rendered_pixels();
    if (pixels.empty() || w <= 0 || h <= 0)
    {
        return false;
    }

    std::ofstream file(filepath, std::ios::binary);
    if (!file.is_open())
    {
        return false;
    }

    const std::uint32_t file_header_size = 14;
    const std::uint32_t info_header_size = 40;
    const std::uint32_t bytes_per_pixel = 3;
    const std::uint32_t row_stride = (static_cast<std::uint32_t>(w) * bytes_per_pixel + 3) & ~3U;
    const std::uint32_t image_size = row_stride * static_cast<std::uint32_t>(h);
    const std::uint32_t file_size = file_header_size + info_header_size + image_size;

    // BMP File Header
    const std::uint8_t file_header[14] = {
        'B', 'M',
        static_cast<std::uint8_t>(file_size),
        static_cast<std::uint8_t>(file_size >> 8),
        static_cast<std::uint8_t>(file_size >> 16),
        static_cast<std::uint8_t>(file_size >> 24),
        0, 0, 0, 0,
        static_cast<std::uint8_t>(file_header_size + info_header_size),
        static_cast<std::uint8_t>((file_header_size + info_header_size) >> 8),
        static_cast<std::uint8_t>((file_header_size + info_header_size) >> 16),
        static_cast<std::uint8_t>((file_header_size + info_header_size) >> 24)
    };
    file.write(reinterpret_cast<const char*>(file_header), sizeof(file_header));

    // DIB Info Header (BITMAPINFOHEADER)
    const std::uint8_t info_header[40] = {
        static_cast<std::uint8_t>(info_header_size), 0, 0, 0,
        static_cast<std::uint8_t>(w), static_cast<std::uint8_t>(w >> 8), static_cast<std::uint8_t>(w >> 16), static_cast<std::uint8_t>(w >> 24),
        static_cast<std::uint8_t>(h), static_cast<std::uint8_t>(h >> 8), static_cast<std::uint8_t>(h >> 16), static_cast<std::uint8_t>(h >> 24),
        1, 0, // Planes
        24, 0, // BitCount (24-bit RGB)
        0, 0, 0, 0, // Compression (BI_RGB)
        static_cast<std::uint8_t>(image_size), static_cast<std::uint8_t>(image_size >> 8), static_cast<std::uint8_t>(image_size >> 16), static_cast<std::uint8_t>(image_size >> 24),
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };
    file.write(reinterpret_cast<const char*>(info_header), sizeof(info_header));

    std::vector<std::uint8_t> row_buffer(row_stride, 0);
    for (int y = h - 1; y >= 0; --y)
    {
        for (int x = 0; x < w; ++x)
        {
            const std::uint32_t px = pixels[static_cast<std::size_t>(y * w + x)];
            const auto r = static_cast<std::uint8_t>((px >> 16) & 0xFF);
            const auto g = static_cast<std::uint8_t>((px >> 8) & 0xFF);
            const auto b = static_cast<std::uint8_t>(px & 0xFF);
            row_buffer[static_cast<std::size_t>(x * 3 + 0)] = b;
            row_buffer[static_cast<std::size_t>(x * 3 + 1)] = g;
            row_buffer[static_cast<std::size_t>(x * 3 + 2)] = r;
        }
        file.write(reinterpret_cast<const char*>(row_buffer.data()), static_cast<std::streamsize>(row_stride));
    }

    return true;
}

void ShaderRuntimeEngine::load_preset(std::size_t preset_index)
{
    const auto presets = ShaderCompiler::get_starter_presets();
    if (preset_index >= presets.size())
    {
        return;
    }
    m_active_preset_index = preset_index;
    set_source_code(presets[preset_index].source_code);
}

void ShaderRuntimeEngine::trigger_compile_internal()
{
    std::lock_guard<std::mutex> lock(m_engine_mutex);
    m_status = ShaderStatus::Compiling;

    std::vector<ShaderDiagnostic> diagnostics;
    auto compiled = m_compiler.compile(m_source_code, diagnostics);
    m_diagnostics = std::move(diagnostics);

    // Also compile with GPU rasterizer
    std::string gpu_err;
    const bool gpu_ok = m_gpu_rasterizer.compile(m_source_code, gpu_err);
    if (!gpu_ok && !gpu_err.empty() && m_render_backend == RenderBackend::Gpu)
    {
        m_diagnostics.push_back(ShaderDiagnostic{1, 1, "[GPU] " + gpu_err, true});
    }

    if (compiled || gpu_ok)
    {
        if (compiled)
        {
            m_active_shader = std::move(*compiled);
        }
        m_status = m_is_playing ? ShaderStatus::Running : ShaderStatus::Paused;
    }
    else
    {
        m_status = ShaderStatus::Error;
    }
    m_is_dirty = true;
}

} // namespace Zenvra::Services::Shader
