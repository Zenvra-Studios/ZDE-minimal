#pragma once

#include <X11/Xlib.h>

#include <cstddef>

namespace Zenvra::Platform::X11::Runtime
{

class X11Context
{
public:
    [[nodiscard]] static bool initialize();
    static void shutdown();

    [[nodiscard]] static Display* get_display() noexcept;

private:
    static int x11_error_handler(Display* display, XErrorEvent* event);
    static int x11_io_error_handler(Display* display);

    static bool s_threads_initialized;
    static Display* s_display;
    static std::size_t s_reference_count;
};

} // namespace Zenvra::Platform::X11::Runtime
