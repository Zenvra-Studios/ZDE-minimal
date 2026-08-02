#pragma once

#include <string>

namespace Zenvra {
namespace Platform {
namespace Cocoa {

class CocoaWindow {
public:
    CocoaWindow(const std::string& title, int width, int height);
    virtual ~CocoaWindow();

    bool initialize();
    void show();
    void update();
    
    bool should_close() const { return m_should_close; }
    void* get_handle() const { return m_window; }

protected:
    virtual void on_resize(int width, int height);

private:
    void* m_window;     // Points to NSWindow*
    void* m_delegate;   // Points to id<NSWindowDelegate>
    std::string m_title;
    int m_width;
    int m_height;
    bool m_should_close;
};

} // namespace Cocoa
} // namespace Platform
} // namespace Zenvra
