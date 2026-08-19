#include "Tray.h"
#include "Platform/Win32/WinRT/WinRTNotification.h"

#include <algorithm>

namespace Zenvra::Platform::Win32 {

SystemTray::SystemTray()
{
}

SystemTray::~SystemTray()
{
    destroy();
}

void SystemTray::init_icon()
{
    if (m_icon != nullptr) {
        return;
    }

    HINSTANCE hInst = GetModuleHandleW(nullptr);
    const int sm_cx = GetSystemMetrics(SM_CXSMICON);
    const int sm_cy = GetSystemMetrics(SM_CYSMICON);

    // 1. Try LoadImageW with IDI_APP_ICON (102) for exact tray icon size
    m_icon = static_cast<HICON>(LoadImageW(
        hInst, MAKEINTRESOURCEW(102), IMAGE_ICON,
        sm_cx > 0 ? sm_cx : 16, sm_cy > 0 ? sm_cy : 16,
        LR_DEFAULTCOLOR));

    // 2. Fallback to ExtractIconExW from running exe
    if (m_icon == nullptr) {
        WCHAR exe_path[MAX_PATH] = {0};
        DWORD len = GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
        if (len > 0) {
            ExtractIconExW(exe_path, 0, nullptr, &m_icon, 1);
        }
    }

    // 3. Fallback to default application icons
    if (m_icon == nullptr) {
        m_icon = LoadIconW(hInst, MAKEINTRESOURCEW(102));
    }
    if (m_icon == nullptr) {
        m_icon = LoadIconW(nullptr, IDI_APPLICATION);
    }
}

bool SystemTray::create(HWND parent_hwnd, UINT callback_msg, std::wstring_view tooltip)
{
    if (m_added) {
        return true;
    }

    m_parent_hwnd = parent_hwnd;
    m_callback_msg = callback_msg;
    init_icon();

    ZeroMemory(&m_nid, sizeof(m_nid));
    m_nid.cbSize = sizeof(NOTIFYICONDATAW);
    m_nid.hWnd = parent_hwnd;
    m_nid.uID = 1;
    m_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    m_nid.uCallbackMessage = callback_msg;
    m_nid.hIcon = m_icon;

    if (!tooltip.empty()) {
        const size_t copy_len = std::min(tooltip.size(), static_cast<size_t>(127));
        wcsncpy_s(m_nid.szTip, tooltip.data(), copy_len);
        m_nid.szTip[copy_len] = L'\0';
    }

    m_nid.uVersion = NOTIFYICON_VERSION_4;

    if (Shell_NotifyIconW(NIM_ADD, &m_nid)) {
        Shell_NotifyIconW(NIM_SETVERSION, &m_nid);
        m_added = true;
        m_visible = true;
        return true;
    }

    return false;
}

void SystemTray::destroy()
{
    if (m_added) {
        Shell_NotifyIconW(NIM_DELETE, &m_nid);
        m_added = false;
        m_visible = false;
    }

    if (m_icon != nullptr) {
        DestroyIcon(m_icon);
        m_icon = nullptr;
    }
}

void SystemTray::set_tooltip(std::wstring_view tooltip)
{
    if (!m_added) {
        return;
    }

    m_nid.uFlags = NIF_TIP | NIF_SHOWTIP;
    const size_t copy_len = std::min(tooltip.size(), static_cast<size_t>(127));
    wcsncpy_s(m_nid.szTip, tooltip.data(), copy_len);
    m_nid.szTip[copy_len] = L'\0';

    Shell_NotifyIconW(NIM_MODIFY, &m_nid);
}

void SystemTray::set_visible(bool visible)
{
    if (m_visible == visible) {
        return;
    }

    if (visible) {
        if (!m_added && m_parent_hwnd != nullptr) {
            create(m_parent_hwnd, m_callback_msg, m_nid.szTip);
        }
    } else {
        if (m_added) {
            Shell_NotifyIconW(NIM_DELETE, &m_nid);
            m_added = false;
        }
    }
    m_visible = visible;
}

void SystemTray::show_notification(std::wstring_view title, std::wstring_view message)
{
    // Try WinRT modern toast notification first
    if (Runtime::WinRTNotification::show_toast(title, message, L"ZDE_TRAY")) {
        return;
    }

    // Fallback to Shell Balloon notification
    if (!m_added) {
        return;
    }

    m_nid.uFlags = NIF_INFO;
    const size_t title_len = std::min(title.size(), static_cast<size_t>(63));
    wcsncpy_s(m_nid.szInfoTitle, title.data(), title_len);
    m_nid.szInfoTitle[title_len] = L'\0';

    const size_t msg_len = std::min(message.size(), static_cast<size_t>(255));
    wcsncpy_s(m_nid.szInfo, message.data(), msg_len);
    m_nid.szInfo[msg_len] = L'\0';

    m_nid.dwInfoFlags = NIIF_INFO | NIIF_LARGE_ICON;
    Shell_NotifyIconW(NIM_MODIFY, &m_nid);

    // Clear info flags
    m_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    m_nid.szInfo[0] = L'\0';
    m_nid.szInfoTitle[0] = L'\0';
}

void SystemTray::show_context_menu(HWND parent_hwnd, POINT screen_pt)
{
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) {
        return;
    }

    AppendMenuW(hMenu, MF_STRING, TrayCmdShow, L"Open ZDE");
    SetMenuDefaultItem(hMenu, TrayCmdShow, FALSE);
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, TrayCmdNewFile, L"New File");
    AppendMenuW(hMenu, MF_STRING, TrayCmdOpenFolder, L"Open Folder...");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, TrayCmdHide, L"Minimize to Tray");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, TrayCmdExit, L"Exit ZDE");

    SetForegroundWindow(parent_hwnd);
    TrackPopupMenuEx(hMenu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, screen_pt.x, screen_pt.y, parent_hwnd, nullptr);
    PostMessageW(parent_hwnd, WM_NULL, 0, 0);
    DestroyMenu(hMenu);
}

} // namespace Zenvra::Platform::Win32