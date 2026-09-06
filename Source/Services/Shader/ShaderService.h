#pragma once

#include "Services/Shader/ShaderRuntimeEngine.h"
#include "Services/Shader/VirtualSurfaceBuffer.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace Zenvra::Services::Shader
{

class ShaderService
{
public:
    ShaderService();
    ~ShaderService();

    ShaderService(const ShaderService&) = delete;
    ShaderService& operator=(const ShaderService&) = delete;

    void initialize(int default_width = 320, int default_height = 240);

    // Surface management & mapping (Android-like virtual surface mapping)
    void resize_surface(int width, int height);
    [[nodiscard]] MappedSurfaceLock acquire_mapped_surface() const;
    [[nodiscard]] SurfaceDescriptor get_surface_descriptor() const;
    [[nodiscard]] const VirtualSurfaceBuffer& get_surface_buffer() const noexcept { return m_surface_buffer; }

    // Headless / Background service runner
    void start_background_service(int target_fps = 60);
    void stop_background_service();
    [[nodiscard]] bool is_background_service_running() const noexcept { return m_is_service_running.load(); }

    // Execute a single frame step offscreen & update virtual surface
    bool step_frame();

    // Dynamic shader code handling & compilation (Toolbar / Editor sync)
    void set_shader_source(std::string_view source_code);
    [[nodiscard]] const std::string& get_shader_source() const noexcept;
    bool compile_and_render();

    [[nodiscard]] static bool is_shader_source_candidate(
        std::string_view source_code,
        std::string_view extension) noexcept;

    // Playback & controls
    void play() noexcept;
    void pause() noexcept;
    void toggle_playback() noexcept;
    void reset_time() noexcept;

    void set_resolution_scale(ResolutionScale scale) noexcept;
    [[nodiscard]] ResolutionScale get_resolution_scale() const noexcept;
    void cycle_resolution_scale() noexcept;

    void set_render_backend(RenderBackend backend) noexcept;
    [[nodiscard]] RenderBackend get_render_backend() const noexcept;
    void toggle_render_backend() noexcept;

    void set_mouse(float x, float y, bool is_down);

    void load_preset(std::size_t index);
    [[nodiscard]] std::size_t get_active_preset_index() const noexcept;

    [[nodiscard]] ShaderStatus get_status() const noexcept;
    [[nodiscard]] float get_time() const noexcept;
    [[nodiscard]] float get_fps() const noexcept;
    [[nodiscard]] float get_frame_time_ms() const noexcept;
    [[nodiscard]] const std::vector<ShaderDiagnostic>& get_diagnostics() const noexcept;

    [[nodiscard]] bool export_snapshot_bmp(const std::string& filepath) const;

    // Frame listener notification
    void register_frame_listener(std::function<void(const SurfaceDescriptor&)> callback);

    [[nodiscard]] ShaderRuntimeEngine& get_engine() noexcept { return m_engine; }
    [[nodiscard]] const ShaderRuntimeEngine& get_engine() const noexcept { return m_engine; }

private:
    void background_worker_loop(int target_fps);
    void publish_surface_frame();

    ShaderRuntimeEngine m_engine;
    VirtualSurfaceBuffer m_surface_buffer;

    std::atomic<bool> m_is_service_running{false};
    std::thread m_worker_thread;
    mutable std::mutex m_service_mutex;
    std::condition_variable m_cv_service;

    std::vector<std::function<void(const SurfaceDescriptor&)>> m_frame_listeners;
    std::mutex m_listener_mutex;
};

} // namespace Zenvra::Services::Shader
