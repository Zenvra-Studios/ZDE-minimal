#include "Platform/X11/Runtime/X11Context.h"

#include <cstdlib>
#include <iostream>
#include <mutex>

namespace
{

std::mutex context_mutex;

} // namespace

namespace Zenvra::Platform::X11::Runtime
{

bool X11Context::s_threads_initialized = false;
Display* X11Context::s_display = nullptr;
std::size_t X11Context::s_reference_count = 0;

bool X11Context::initialize()
{
    const std::scoped_lock lock(context_mutex);
    if (s_display != nullptr)
    {
        ++s_reference_count;
        return true;
    }

    if (!s_threads_initialized)
    {
        if (XInitThreads() == 0)
        {
            std::cerr << "Fatal error: XInitThreads failed.\n";
            return false;
        }
        s_threads_initialized = true;
    }

    XSetErrorHandler(x11_error_handler);
    XSetIOErrorHandler(x11_io_error_handler);
    s_display = XOpenDisplay(nullptr);
    if (s_display == nullptr)
    {
        std::cerr << "Fatal error: the X11 display could not be opened.\n";
        return false;
    }

    s_reference_count = 1;
    return true;
}

void X11Context::shutdown()
{
    const std::scoped_lock lock(context_mutex);
    if (s_reference_count == 0)
    {
        return;
    }

    --s_reference_count;
    if (s_reference_count == 0 && s_display != nullptr)
    {
        XCloseDisplay(s_display);
        s_display = nullptr;
    }
}

Display* X11Context::get_display() noexcept
{
    const std::scoped_lock lock(context_mutex);
    return s_display;
}

int X11Context::x11_error_handler(Display* display, XErrorEvent* event)
{
    char error_text[1024]{};
    XGetErrorText(display, event->error_code, error_text, sizeof(error_text));
    std::cerr << "X11 error: " << error_text
              << " (request: " << static_cast<int>(event->request_code)
              << ", minor: " << static_cast<int>(event->minor_code) << ")\n";
    return 0;
}

int X11Context::x11_io_error_handler(Display* display)
{
    static_cast<void>(display);
    std::cerr << "Fatal X11 I/O error: the display connection was lost.\n";
    std::_Exit(EXIT_FAILURE);
}

} // namespace Zenvra::Platform::X11::Runtime
