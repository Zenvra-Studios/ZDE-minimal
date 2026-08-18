#pragma once

#include "Services/Shader/CpuShaderRasterizer.h"
#include "Services/Shader/GpuShaderRasterizer.h"
#include "Services/Shader/ShaderCompiler.h"
#include "Services/Shader/ShaderTypes.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Zenvra::Services::Shader
{

class ShaderRuntimeEngine
{
public:
    ShaderRuntimeEngine();
    ~ShaderRuntimeEngine();

    void initialize();
    void resize(int viewport_width, int viewport_height);

    void set_source_code(std::string_view source_code);
    [[nodiscard]] const std::string& get_source_code() const noexcept { return m_source_code; }

    void play() noexcept;
    void pause() noexcept;
    void toggle_playback() noexcept;
    void reset_time() noexcept;
    void set_playback_speed(float speed) noexcept;
    [[nodiscard]] float get_playback_speed() const noexcept { return m_playback_speed; }

    void set_resolution_scale(ResolutionScale scale) noexcept;
    [[nodiscard]] ResolutionScale get_resolution_scale() const noexcept { return m_resolution_scale; }
    void cycle_resolution_scale() noexcept;

    void set_render_backend(RenderBackend backend) noexcept;
    [[nodiscard]] RenderBackend get_render_backend() const noexcept { return m_render_backend; }
    void toggle_render_backend() noexcept;
    [[nodiscard]] bool is_gpu_supported() const noexcept;

    void set_mouse(float x, float y, bool is_down);

    // Update time & render frame if playing or dirty
    [[nodiscard]] bool update_and_render();

    [[nodiscard]] bool is_playing() const noexcept { return m_is_playing; }
    [[nodiscard]] ShaderStatus get_status() const noexcept { return m_status; }
    [[nodiscard]] float get_time() const noexcept { return m_uniforms.time; }
    [[nodiscard]] int get_frame() const noexcept { return m_uniforms.frame; }
    [[nodiscard]] float get_fps() const noexcept { return m_current_fps; }
    [[nodiscard]] float get_frame_time_ms() const noexcept { return m_frame_time_ms; }
    [[nodiscard]] const std::vector<ShaderDiagnostic>& get_diagnostics() const noexcept { return m_diagnostics; }

    [[nodiscard]] std::span<const std::uint32_t> get_rendered_pixels() const noexcept;
    [[nodiscard]] int get_rendered_width() const noexcept;
    [[nodiscard]] int get_rendered_height() const noexcept;
    [[nodiscard]] int get_viewport_width() const noexcept { return m_viewport_width; }
    [[nodiscard]] int get_viewport_height() const noexcept { return m_viewport_height; }

    // Channel configuration
    void set_channel_texture(std::size_t channel_index, ChannelTextureKind kind);
    [[nodiscard]] const ShaderChannel& get_channel(std::size_t channel_index) const noexcept;

    // Export current frame to image file
    [[nodiscard]] bool export_snapshot_bmp(const std::string& filepath) const;

    // Preset loading
    void load_preset(std::size_t preset_index);
    [[nodiscard]] std::size_t get_active_preset_index() const noexcept { return m_active_preset_index; }

private:
    void trigger_compile_internal();

    std::string m_source_code;
    ShaderCompiler m_compiler;
    CpuShaderRasterizer m_rasterizer;
    GpuShaderRasterizer m_gpu_rasterizer;
    PixelShaderFunc m_active_shader;

    ShaderUniforms m_uniforms;
    std::array<ShaderChannel, 4> m_channels;

    ShaderStatus m_status = ShaderStatus::Idle;
    ResolutionScale m_resolution_scale = ResolutionScale::Full;
    RenderBackend m_render_backend = RenderBackend::Cpu;
    bool m_is_playing = true;
    bool m_is_dirty = true;
    float m_playback_speed = 1.0F;

    std::vector<ShaderDiagnostic> m_diagnostics;
    std::chrono::steady_clock::time_point m_last_frame_time;
    std::chrono::steady_clock::time_point m_fps_sample_time;
    int m_fps_frame_count = 0;
    float m_current_fps = 60.0F;
    float m_frame_time_ms = 16.6F;
    std::size_t m_active_preset_index = 0;
    int m_viewport_width = 320;
    int m_viewport_height = 240;

    void apply_effective_resolution();

    mutable std::mutex m_engine_mutex;
};

} // namespace Zenvra::Services::Shader
