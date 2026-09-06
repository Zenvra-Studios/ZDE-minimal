#include "Services/Shader/ShaderService.h"

#include <chrono>
#include <iostream>

namespace Zenvra::Services::Shader
{

ShaderService::ShaderService() = default;

ShaderService::~ShaderService()
{
    stop_background_service();
}

void ShaderService::initialize(int default_width, int default_height)
{
    m_engine.initialize();
    m_engine.resize(default_width, default_height);
    m_surface_buffer.resize(default_width, default_height);
    static_cast<void>(step_frame());
}

void ShaderService::resize_surface(int width, int height)
{
    std::lock_guard<std::mutex> lock(m_service_mutex);
    if (width > 0 && height > 0 &&
        (m_engine.get_viewport_width() != width || m_engine.get_viewport_height() != height))
    {
        m_engine.resize(width, height);
        m_surface_buffer.resize(width, height);
    }
}

MappedSurfaceLock ShaderService::acquire_mapped_surface() const
{
    return m_surface_buffer.acquire_mapped_surface();
}

SurfaceDescriptor ShaderService::get_surface_descriptor() const
{
    return m_surface_buffer.get_descriptor();
}

void ShaderService::start_background_service(int target_fps)
{
    if (m_is_service_running.load())
    {
        return;
    }

    m_is_service_running.store(true);
    m_worker_thread = std::thread([this, target_fps]() {
        background_worker_loop(target_fps);
    });
}

void ShaderService::stop_background_service()
{
    if (!m_is_service_running.load())
    {
        return;
    }

    m_is_service_running.store(false);
    m_cv_service.notify_all();

    if (m_worker_thread.joinable())
    {
        m_worker_thread.join();
    }
}

void ShaderService::background_worker_loop(int target_fps)
{
    const int effective_fps = target_fps > 0 ? target_fps : 60;
    const auto frame_duration = std::chrono::microseconds(1000000 / effective_fps);

    while (m_is_service_running.load())
    {
        const auto start_time = std::chrono::steady_clock::now();

        if (m_engine.is_playing())
        {
            step_frame();
        }

        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start_time);

        if (elapsed < frame_duration)
        {
            std::unique_lock<std::mutex> lock(m_service_mutex);
            m_cv_service.wait_for(lock, frame_duration - elapsed, [this]() {
                return !m_is_service_running.load();
            });
        }
    }
}

bool ShaderService::step_frame()
{
    std::lock_guard<std::mutex> lock(m_service_mutex);
    const bool rendered = m_engine.update_and_render();
    publish_surface_frame();
    return rendered;
}

void ShaderService::publish_surface_frame()
{
    const auto pixels = m_engine.get_rendered_pixels();
    const int width = m_engine.get_rendered_width();
    const int height = m_engine.get_rendered_height();

    if (pixels.empty() || width <= 0 || height <= 0)
    {
        return;
    }

    SurfaceDescriptor desc{
        .width = width,
        .height = height,
        .stride = width * static_cast<int>(sizeof(std::uint32_t)),
        .frame_index = static_cast<std::uint64_t>(m_engine.get_frame()),
        .timestamp_sec = m_engine.get_time(),
        .frame_time_ms = m_engine.get_frame_time_ms(),
        .fps = m_engine.get_fps(),
        .backend = m_engine.get_render_backend(),
        .status = m_engine.get_status(),
    };

    m_surface_buffer.post_frame(pixels, desc);

    // Notify listeners if registered
    std::lock_guard<std::mutex> listener_lock(m_listener_mutex);
    for (const auto& listener : m_frame_listeners)
    {
        if (listener)
        {
            listener(desc);
        }
    }
}

void ShaderService::set_shader_source(std::string_view source_code)
{
    std::lock_guard<std::mutex> lock(m_service_mutex);
    m_engine.set_source_code(source_code);
}

const std::string& ShaderService::get_shader_source() const noexcept
{
    return m_engine.get_source_code();
}

bool ShaderService::compile_and_render()
{
    std::lock_guard<std::mutex> lock(m_service_mutex);
    const bool ok = m_engine.update_and_render();
    publish_surface_frame();
    return ok;
}

bool ShaderService::is_shader_source_candidate(
    std::string_view source_code,
    std::string_view extension) noexcept
{
    const bool is_ext = (extension == ".glsl" || extension == ".frag" || extension == ".vert" ||
                         extension == ".comp" || extension == ".shader" || extension == ".hlsl" ||
                         extension == ".geom" || extension == ".tesc" || extension == ".tese" ||
                         extension == ".fs" || extension == ".vs");
    if (is_ext)
    {
        return true;
    }

    const bool has_glsl_tokens = (
        source_code.find("mainImage") != std::string_view::npos ||
        source_code.find("gl_FragColor") != std::string_view::npos ||
        source_code.find("gl_FragCoord") != std::string_view::npos ||
        source_code.find("#version") != std::string_view::npos ||
        source_code.find("fragColor") != std::string_view::npos ||
        source_code.find("iResolution") != std::string_view::npos ||
        source_code.find("iTime") != std::string_view::npos
    );

    return has_glsl_tokens;
}

void ShaderService::play() noexcept
{
    std::lock_guard<std::mutex> lock(m_service_mutex);
    m_engine.play();
}

void ShaderService::pause() noexcept
{
    std::lock_guard<std::mutex> lock(m_service_mutex);
    m_engine.pause();
}

void ShaderService::toggle_playback() noexcept
{
    std::lock_guard<std::mutex> lock(m_service_mutex);
    m_engine.toggle_playback();
}

void ShaderService::reset_time() noexcept
{
    std::lock_guard<std::mutex> lock(m_service_mutex);
    m_engine.reset_time();
}

void ShaderService::set_resolution_scale(ResolutionScale scale) noexcept
{
    std::lock_guard<std::mutex> lock(m_service_mutex);
    m_engine.set_resolution_scale(scale);
}

ResolutionScale ShaderService::get_resolution_scale() const noexcept
{
    return m_engine.get_resolution_scale();
}

void ShaderService::cycle_resolution_scale() noexcept
{
    std::lock_guard<std::mutex> lock(m_service_mutex);
    m_engine.cycle_resolution_scale();
}

void ShaderService::set_render_backend(RenderBackend backend) noexcept
{
    std::lock_guard<std::mutex> lock(m_service_mutex);
    m_engine.set_render_backend(backend);
}

RenderBackend ShaderService::get_render_backend() const noexcept
{
    return m_engine.get_render_backend();
}

void ShaderService::toggle_render_backend() noexcept
{
    std::lock_guard<std::mutex> lock(m_service_mutex);
    m_engine.toggle_render_backend();
}

void ShaderService::set_mouse(float x, float y, bool is_down)
{
    std::lock_guard<std::mutex> lock(m_service_mutex);
    m_engine.set_mouse(x, y, is_down);
}

void ShaderService::load_preset(std::size_t index)
{
    std::lock_guard<std::mutex> lock(m_service_mutex);
    m_engine.load_preset(index);
}

std::size_t ShaderService::get_active_preset_index() const noexcept
{
    return m_engine.get_active_preset_index();
}

ShaderStatus ShaderService::get_status() const noexcept
{
    return m_engine.get_status();
}

float ShaderService::get_time() const noexcept
{
    return m_engine.get_time();
}

float ShaderService::get_fps() const noexcept
{
    return m_engine.get_fps();
}

float ShaderService::get_frame_time_ms() const noexcept
{
    return m_engine.get_frame_time_ms();
}

const std::vector<ShaderDiagnostic>& ShaderService::get_diagnostics() const noexcept
{
    return m_engine.get_diagnostics();
}

bool ShaderService::export_snapshot_bmp(const std::string& filepath) const
{
    std::lock_guard<std::mutex> lock(m_service_mutex);
    return m_engine.export_snapshot_bmp(filepath);
}

void ShaderService::register_frame_listener(std::function<void(const SurfaceDescriptor&)> callback)
{
    std::lock_guard<std::mutex> lock(m_listener_mutex);
    m_frame_listeners.push_back(std::move(callback));
}

} // namespace Zenvra::Services::Shader
