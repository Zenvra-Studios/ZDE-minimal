#pragma once

#include <windows.h>
#include <shellapi.h>
#include <string>
#include <string_view>

namespace Zenvra::Platform::Win32 {

enum TrayCommandId : UINT {
    TrayCmdShow = 60001,
    TrayCmdHide = 60002,
    TrayCmdNewFile = 60003,
    TrayCmdOpenFolder = 60004,
    TrayCmdExit = 60005
};

class SystemTray {
public:
    SystemTray();
    ~SystemTray();

    SystemTray(const SystemTray&) = delete;
    SystemTray& operator=(const SystemTray&) = delete;

    bool create(HWND parent_hwnd, UINT callback_msg, std::wstring_view tooltip = L"ZDE - Zenvra Development Environment");
    void destroy();

    void set_tooltip(std::wstring_view tooltip);
    void set_visible(bool visible);
    [[nodiscard]] bool is_visible() const noexcept { return m_visible; }

    void show_notification(std::wstring_view title, std::wstring_view message);
    void show_context_menu(HWND parent_hwnd, POINT screen_pt);

    [[nodiscard]] HWND get_parent() const noexcept { return m_parent_hwnd; }
    [[nodiscard]] UINT get_callback_message() const noexcept { return m_callback_msg; }

private:
    void init_icon();

    HWND m_parent_hwnd = nullptr;
    UINT m_callback_msg = 0;
    HICON m_icon = nullptr;
    bool m_visible = false;
    bool m_added = false;
    NOTIFYICONDATAW m_nid{};
};

} // namespace Zenvra::Platform::Win32