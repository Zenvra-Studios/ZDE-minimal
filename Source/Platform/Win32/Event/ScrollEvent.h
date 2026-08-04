#pragma once

#include <cstddef>

namespace Zenvra::Platform::Win32::Event
{

struct ScrollEvent
{
    std::ptrdiff_t delta_x = 0;
    std::ptrdiff_t delta_y = 0;
    float point_x = 0.0f;
    float point_y = 0.0f;
    bool is_mouse_wheel = false;
    bool is_keyboard = false;
};

} // namespace Zenvra::Platform::Win32::Event
