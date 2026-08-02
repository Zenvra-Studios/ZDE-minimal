#pragma once

#include <windows.h>
#include <string>

#include "Components/Menubar.h"

namespace Zenvra {
namespace Platform {
namespace Win32 {

class Win32Window {
public:
    Win32Window(const std::wstring& title, int width, int height);
    virtual ~Win32Window();

    bool initialize();
    void show();
    void update();
    
    bool should_close() const { return m_should_close; }
    HWND get_handle() const { return m_hwnd; }

protected:
    virtual LRESULT handle_message(HWND hwnd, UINT msg, WPARAM w_param, LPARAM l_param);
    
    // Callback methods for layouting and UI
    virtual void on_resize(int width, int height);
    virtual void on_command(int command_id);

private:
    static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM w_param, LPARAM l_param);

    HWND m_hwnd;
    HINSTANCE m_hinstance;
    std::wstring m_title;
    int m_width;
    int m_height;
    bool m_should_close;

    Components::Menubar m_menubar;

    static const wchar_t* s_class_name;
};

} // namespace Win32
} // namespace Platform
} // namespace Zenvra
