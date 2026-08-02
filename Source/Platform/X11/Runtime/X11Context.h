#pragma once

#include <X11/Xlib.h>

namespace Zenvra {
namespace Platform {
namespace X11 {
namespace Runtime {

class X11Context {
public:
    static bool initialize();
    static void shutdown();
    
    // Global access to the X11 Display
    static Display* get_display();

private:
    static int x11_error_handler(Display* display, XErrorEvent* event);
    static int x11_io_error_handler(Display* display);

    static bool s_is_initialized;
    static Display* s_display;
};

} // namespace Runtime
} // namespace X11
} // namespace Platform
} // namespace Zenvra
