#include "Menubar.h"
#include "../Config/resource.h"

namespace Zenvra {
namespace Platform {
namespace Win32 {
namespace Components {

Menubar::Menubar()
    : m_menu(nullptr), m_hwnd(nullptr)
{
}

Menubar::~Menubar()
{
    if (m_menu) {
        DestroyMenu(m_menu);
        m_menu = nullptr;
    }
}

bool Menubar::load(HINSTANCE h_instance)
{
    m_menu = LoadMenuW(h_instance, MAKEINTRESOURCEW(IDR_MAINMENU));
    return m_menu != nullptr;
}

bool Menubar::attach(HWND hwnd)
{
    m_hwnd = hwnd;
    return hwnd ? (SetMenu(hwnd, m_menu) != 0) : false;
}

bool Menubar::handle_command(int command_id)
{
    switch (command_id) {
        case ID_FILE_EXIT:
            PostMessage(m_hwnd, WM_CLOSE, 0, 0);
            return true;
        case ID_HELP_ABOUT:
            MessageBoxW(m_hwnd, L"Zenvra Development Studio v0.1.0", L"About", MB_OK | MB_ICONINFORMATION);
            return true;
    }
    return false;
}

} // namespace Components
} // namespace Win32
} // namespace Platform
} // namespace Zenvra
