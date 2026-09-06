#include "Services/Shader/VirtualSurfaceBuffer.h"

#include <algorithm>

namespace Zenvra::Services::Shader
{

VirtualSurfaceBuffer::VirtualSurfaceBuffer() = default;

VirtualSurfaceBuffer::~VirtualSurfaceBuffer() = default;

void VirtualSurfaceBuffer::resize(int width, int height)
{
    std::lock_guard<std::mutex> lock(m_surface_mutex);
    if (width <= 0 || height <= 0)
    {
        m_front_buffer.clear();
        m_back_buffer.clear();
        m_descriptor = SurfaceDescriptor{};
        return;
    }

    const std::size_t pixel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    m_front_buffer.resize(pixel_count, 0xFF000000U);
    m_back_buffer.resize(pixel_count, 0xFF000000U);
    m_descriptor.width = width;
    m_descriptor.height = height;
    m_descriptor.stride = width * static_cast<int>(sizeof(std::uint32_t));
}

void VirtualSurfaceBuffer::post_frame(
    std::span<const std::uint32_t> pixels,
    const SurfaceDescriptor& descriptor)
{
    std::lock_guard<std::mutex> lock(m_surface_mutex);
    if (pixels.empty() || descriptor.width <= 0 || descriptor.height <= 0)
    {
        return;
    }

    const std::size_t count = std::min(
        pixels.size(),
        static_cast<std::size_t>(descriptor.width) * static_cast<std::size_t>(descriptor.height));

    if (m_front_buffer.size() != count)
    {
        m_front_buffer.resize(count);
    }

    std::copy_n(pixels.data(), count, m_front_buffer.data());
    m_descriptor = descriptor;
    m_descriptor.stride = descriptor.width * static_cast<int>(sizeof(std::uint32_t));
    m_has_new_frame = true;
}

MappedSurfaceLock VirtualSurfaceBuffer::acquire_mapped_surface() const
{
    std::unique_lock<std::mutex> lock(m_surface_mutex);
    if (m_front_buffer.empty())
    {
        return MappedSurfaceLock{};
    }

    return MappedSurfaceLock(
        std::span<const std::uint32_t>(m_front_buffer.data(), m_front_buffer.size()),
        m_descriptor,
        std::move(lock));
}

SurfaceDescriptor VirtualSurfaceBuffer::get_descriptor() const
{
    std::lock_guard<std::mutex> lock(m_surface_mutex);
    return m_descriptor;
}

bool VirtualSurfaceBuffer::has_new_frame() const noexcept
{
    return m_has_new_frame;
}

void VirtualSurfaceBuffer::mark_frame_consumed() noexcept
{
    m_has_new_frame = false;
}

void VirtualSurfaceBuffer::clear()
{
    std::lock_guard<std::mutex> lock(m_surface_mutex);
    m_front_buffer.clear();
    m_back_buffer.clear();
    m_descriptor = SurfaceDescriptor{};
    m_has_new_frame = false;
}

} // namespace Zenvra::Services::Shader
