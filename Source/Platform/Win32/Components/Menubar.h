#pragma once

#include <windows.h>

namespace Zenvra {
namespace Platform {
namespace Win32 {
namespace Components {

class Menubar {
public:
    Menubar();
    ~Menubar();

    // Loads the main menu resource (IDR_MAINMENU) defined in Win32Resources.rc
    bool load(HINSTANCE h_instance);

    // Attaches the loaded menu to the given window so it is displayed
    bool attach(HWND hwnd);

    // Routes menu command IDs to their handlers; returns true when handled
    bool handle_command(int command_id);

    HMENU get_handle() const { return m_menu; }
    HWND get_window() const { return m_hwnd; }

private:
    HMENU m_menu;
    HWND m_hwnd;
};

} // namespace Components
} // namespace Win32
} // namespace Platform
} // namespace Zenvra
