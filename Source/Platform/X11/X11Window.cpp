#include "X11Window.h"
#include "Runtime/X11Context.h"
#include <X11/Xutil.h>
#include <iostream>

namespace Zenvra {
namespace Platform {
namespace X11 {

X11Window::X11Window(const std::string& title, int width, int height)
    : m_display(nullptr), m_window(0), m_title(title), m_width(width), m_height(height), m_should_close(false)
{
}

X11Window::~X11Window()
{
    if (m_display && m_window) {
        XDestroyWindow(m_display, m_window);
        m_window = 0;
    }
    // Global Display connection is managed and closed by Runtime::X11Context
}

bool X11Window::initialize()
{
    m_display = Runtime::X11Context::get_display();
    if (!m_display) {
        std::cerr << "Fatal Error: X11 global display is null. Did you initialize X11Context?" << std::endl;
        return false;
    }

    int screen = DefaultScreen(m_display);
    Window root = RootWindow(m_display, screen);

    m_window = XCreateSimpleWindow(
        m_display, root,
        0, 0, m_width, m_height, 1,
        BlackPixel(m_display, screen), WhitePixel(m_display, screen)
    );

    XStoreName(m_display, m_window, m_title.c_str());

    // Select input events to listen to
    XSelectInput(m_display, m_window, ExposureMask | KeyPressMask | StructureNotifyMask);

    // Register protocol to handle window close button correctly
    Atom wm_delete_window = XInternAtom(m_display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(m_display, m_window, &wm_delete_window, 1);

    return true;
}

void X11Window::show()
{
    if (m_display && m_window) {
        XMapWindow(m_display, m_window);
        XFlush(m_display);
    }
}

void X11Window::update()
{
    if (!m_display) return;

    XEvent event;
    // Process all pending events
    while (XPending(m_display) > 0) {
        XNextEvent(m_display, &event);
        
        if (event.type == ClientMessage) {
            Atom wm_delete_window = XInternAtom(m_display, "WM_DELETE_WINDOW", False);
            if (static_cast<Atom>(event.xclient.data.l[0]) == wm_delete_window) {
                m_should_close = true;
            }
        }
        else if (event.type == ConfigureNotify) {
            if (event.xconfigure.width != m_width || event.xconfigure.height != m_height) {
                m_width = event.xconfigure.width;
                m_height = event.xconfigure.height;
                on_resize(m_width, m_height);
            }
        }
    }
}

void X11Window::on_resize(int width, int height)
{
    // Override this in a derived class or dispatch an event to handle UI layouting 
    // when the window is resized.
}

} // namespace X11
} // namespace Platform
} // namespace Zenvra
