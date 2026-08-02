#include "X11Context.h"
#include <iostream>
#include <cstdlib>

namespace Zenvra {
namespace Platform {
namespace X11 {
namespace Runtime {

bool X11Context::s_is_initialized = false;
Display* X11Context::s_display = nullptr;

int X11Context::x11_error_handler(Display* display, XErrorEvent* event)
{
    char error_text[1024];
    XGetErrorText(display, event->error_code, error_text, sizeof(error_text));
    
    std::cerr << "X11 Error: " << error_text 
              << " (Request: " << (int)event->request_code 
              << ", Minor: " << (int)event->minor_code << ")" << std::endl;
              
    return 0; // Returning 0 prevents the application from exiting abruptly
}

int X11Context::x11_io_error_handler(Display* display)
{
    std::cerr << "Fatal X11 I/O Error: Connection to X server lost!" << std::endl;
    exit(1); // I/O errors are typically fatal, Xlib requires exit
    return 0;
}

bool X11Context::initialize()
{
    if (s_is_initialized) {
        return true;
    }

    // Initialize multithreading support before any other X11 calls
    if (XInitThreads() == 0) {
        std::cerr << "Fatal Error: XInitThreads failed. Xlib does not support multithreading." << std::endl;
        return false;
    }

    // Set custom error handlers
    XSetErrorHandler(x11_error_handler);
    XSetIOErrorHandler(x11_io_error_handler);

    // Open a global display connection
    s_display = XOpenDisplay(nullptr);
    if (!s_display) {
        std::cerr << "Fatal Error: Failed to open X11 global display." << std::endl;
        return false;
    }

    s_is_initialized = true;
    return true;
}

void X11Context::shutdown()
{
    if (s_is_initialized && s_display) {
        XCloseDisplay(s_display);
        s_display = nullptr;
        s_is_initialized = false;
    }
}

Display* X11Context::get_display()
{
    return s_display;
}

} // namespace Runtime
} // namespace X11
} // namespace Platform
} // namespace Zenvra
