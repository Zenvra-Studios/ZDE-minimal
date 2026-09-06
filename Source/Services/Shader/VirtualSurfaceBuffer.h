#pragma once

#include "Services/Shader/ShaderTypes.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>
#include <vector>

namespace Zenvra::Services::Shader
{

struct SurfaceDescriptor
{
    int width = 0;
    int height = 0;
    int stride = 0;
    std::uint64_t frame_index = 0;
    float timestamp_sec = 0.0F;
    float frame_time_ms = 0.0F;
    float fps = 60.0F;
    RenderBackend backend = RenderBackend::Cpu;
    ShaderStatus status = ShaderStatus::Idle;
};

class MappedSurfaceLock
{
public:
    MappedSurfaceLock() = default;
    MappedSurfaceLock(
        std::span<const std::uint32_t> pixels,
        SurfaceDescriptor desc,
        std::unique_lock<std::mutex> lock) noexcept
        : m_pixels(pixels), m_descriptor(desc), m_lock(std::move(lock))
    {
    }

    MappedSurfaceLock(const MappedSurfaceLock&) = delete;
    MappedSurfaceLock& operator=(const MappedSurfaceLock&) = delete;
    MappedSurfaceLock(MappedSurfaceLock&&) noexcept = default;
    MappedSurfaceLock& operator=(MappedSurfaceLock&&) noexcept = default;

    [[nodiscard]] bool is_valid() const noexcept
    {
        return !m_pixels.empty() && m_descriptor.width > 0 && m_descriptor.height > 0;
    }

    [[nodiscard]] std::span<const std::uint32_t> get_pixels() const noexcept
    {
        return m_pixels;
    }

    [[nodiscard]] const SurfaceDescriptor& get_descriptor() const noexcept
    {
        return m_descriptor;
    }

    [[nodiscard]] int get_width() const noexcept { return m_descriptor.width; }
    [[nodiscard]] int get_height() const noexcept { return m_descriptor.height; }

    void unlock()
    {
        if (m_lock.owns_lock())
        {
            m_lock.unlock();
        }
        m_pixels = {};
    }

private:
    std::span<const std::uint32_t> m_pixels;
    SurfaceDescriptor m_descriptor;
    std::unique_lock<std::mutex> m_lock;
};

class VirtualSurfaceBuffer
{
public:
    VirtualSurfaceBuffer();
    ~VirtualSurfaceBuffer();

    VirtualSurfaceBuffer(const VirtualSurfaceBuffer&) = delete;
    VirtualSurfaceBuffer& operator=(const VirtualSurfaceBuffer&) = delete;

    void resize(int width, int height);

    void post_frame(
        std::span<const std::uint32_t> pixels,
        const SurfaceDescriptor& descriptor);

    [[nodiscard]] MappedSurfaceLock acquire_mapped_surface() const;

    [[nodiscard]] SurfaceDescriptor get_descriptor() const;

    [[nodiscard]] bool has_new_frame() const noexcept;
    void mark_frame_consumed() noexcept;

    void clear();

private:
    mutable std::mutex m_surface_mutex;
    std::vector<std::uint32_t> m_back_buffer;
    std::vector<std::uint32_t> m_front_buffer;
    SurfaceDescriptor m_descriptor;
    bool m_has_new_frame = false;
};

} // namespace Zenvra::Services::Shader
