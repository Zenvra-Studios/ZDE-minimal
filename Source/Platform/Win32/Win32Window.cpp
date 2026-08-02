#include "Win32Window.h"
#include "Config/resource.h"

#include <iostream>

namespace Zenvra {
namespace Platform {
namespace Win32 {

const wchar_t* Win32Window::s_class_name = L"ZenvraWin32WindowClass";

Win32Window::Win32Window(const std::wstring& title, int width, int height)
    : m_hwnd(nullptr), m_hinstance(GetModuleHandle(nullptr)), 
      m_title(title), m_width(width), m_height(height), m_should_close(false)
{
}

Win32Window::~Win32Window()
{
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
    }
}

bool Win32Window::initialize()
{
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = window_proc;
    wc.hInstance = m_hinstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = s_class_name;

    RegisterClassExW(&wc);

    m_hwnd = CreateWindowExW(
        0,
        s_class_name,
        m_title.c_str(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, m_width, m_height,
        nullptr, nullptr, m_hinstance, this
    );

    if (!m_hwnd) {
        return false;
    }

    // Load and attach the menu bar from Win32Resources.rc (IDR_MAINMENU)
    if (!m_menubar.load(m_hinstance)) {
        std::wcerr << L"Warning: Failed to load main menu resource." << std::endl;
    } else if (!m_menubar.attach(m_hwnd)) {
        std::wcerr << L"Warning: Failed to attach main menu to window." << std::endl;
    }

    return true;
}

void Win32Window::show()
{
    if (m_hwnd) {
        ShowWindow(m_hwnd, SW_SHOW);
        UpdateWindow(m_hwnd);
    }
}

void Win32Window::update()
{
    MSG msg = {0};
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            m_should_close = true;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

LRESULT CALLBACK Win32Window::window_proc(HWND hwnd, UINT msg, WPARAM w_param, LPARAM l_param)
{
    Win32Window* p_this = nullptr;

    if (msg == WM_NCCREATE) {
        CREATESTRUCT* p_create = reinterpret_cast<CREATESTRUCT*>(l_param);
        p_this = reinterpret_cast<Win32Window*>(p_create->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(p_this));
        p_this->m_hwnd = hwnd;
    } else {
        p_this = reinterpret_cast<Win32Window*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }

    if (p_this) {
        return p_this->handle_message(hwnd, msg, w_param, l_param);
    }

    return DefWindowProc(hwnd, msg, w_param, l_param);
}

LRESULT Win32Window::handle_message(HWND hwnd, UINT msg, WPARAM w_param, LPARAM l_param)
{
    switch (msg) {
        case WM_SIZE:
        {
            // Update dimensions and trigger layouting logic for UI
            m_width = LOWORD(l_param);
            m_height = HIWORD(l_param);
            on_resize(m_width, m_height);
            return 0;
        }
        case WM_COMMAND:
        {
            // Handle Menu Bar item clicks
            on_command(LOWORD(w_param));
            return 0;
        }
        case WM_CLOSE:
        {
            PostQuitMessage(0);
            return 0;
        }
        case WM_DESTROY:
        {
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProc(hwnd, msg, w_param, l_param);
}

void Win32Window::on_resize(int width, int height)
{
    // Override this in a derived class or dispatch an event to handle UI layouting 
    // when the window is resized.
}

void Win32Window::on_command(int command_id)
{
    // Menu commands are handled by the Menubar component (IDR_MAINMENU).
    if (m_menubar.handle_command(command_id)) {
        return;
    }

    // Handle non-menu commands here later.
}

} // namespace Win32
} // namespace Platform
} // namespace Zenvra
