#pragma once

#include <string>
#include <X11/Xlib.h>

namespace Zenvra {
namespace Platform {
namespace X11 {

class X11Window {
public:
    X11Window(const std::string& title, int width, int height);
    virtual ~X11Window();

    bool initialize();
    void show();
    void update();
    
    bool should_close() const { return m_should_close; }
    Window get_handle() const { return m_window; }
    Display* get_display() const { return m_display; }

protected:
    // Callback methods for layouting and UI
    virtual void on_resize(int width, int height);

private:
    Display* m_display;
    Window m_window;
    std::string m_title;
    int m_width;
    int m_height;
    bool m_should_close;
};

} // namespace X11
} // namespace Platform
} // namespace Zenvra
