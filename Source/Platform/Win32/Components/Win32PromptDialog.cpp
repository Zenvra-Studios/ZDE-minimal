#include "Platform/Win32/Components/Win32PromptDialog.h"
#include "Utility/Shadows.h"

#include <dwmapi.h>
#include <windowsx.h>
#include <algorithm>
#include <string>
#include <vector>

namespace Zenvra::Platform::Win32::Components
{

namespace
{
constexpr const wchar_t* prompt_dialog_class_name = L"ZDE_PromptDialogWindow";

void fill_rounded_rect(HDC dc, const RECT& rc, COLORREF col, int radius)
{
    HBRUSH brush = CreateSolidBrush(col);
    HPEN pen = CreatePen(PS_NULL, 0, 0);
    HGDIOBJ old_brush = SelectObject(dc, brush);
    HGDIOBJ old_pen = SelectObject(dc, pen);

    RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, radius * 2, radius * 2);

    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(pen);
    DeleteObject(brush);
}

std::wstring utf8_to_wstring(std::string_view str)
{
    if (str.empty()) return {};
    const int len = MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), nullptr, 0);
    if (len <= 0) return {};
    std::wstring result(static_cast<std::size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), result.data(), len);
    return result;
}
} // namespace

Win32PromptDialog::Win32PromptDialog() = default;

Win32PromptDialog::~Win32PromptDialog()
{
    close();
    if (m_title_font) { DeleteObject(m_title_font); m_title_font = nullptr; }
    if (m_ui_font) { DeleteObject(m_ui_font); m_ui_font = nullptr; }
    if (m_small_font) { DeleteObject(m_small_font); m_small_font = nullptr; }
}

void Win32PromptDialog::refresh_fonts()
{
    if (m_title_font) { DeleteObject(m_title_font); m_title_font = nullptr; }
    if (m_ui_font) { DeleteObject(m_ui_font); m_ui_font = nullptr; }
    if (m_small_font) { DeleteObject(m_small_font); m_small_font = nullptr; }

    const float scale = static_cast<float>(m_dpi) / 96.0F;

    m_title_font = CreateFontW(
        -static_cast<int>(13.5F * scale), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    m_ui_font = CreateFontW(
        -static_cast<int>(12.5F * scale), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    m_small_font = CreateFontW(
        -static_cast<int>(11.0F * scale), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

bool Win32PromptDialog::open_new_folder(
    HWND parent,
    const std::filesystem::path& target_dir,
    std::function<void(const std::string&)> on_confirm)
{
    close();

    m_mode = PromptDialogMode::NewFolder;
    m_title = "New Folder";
    m_target_path = target_dir;
    m_subtitle = "Target: " + target_dir.filename().string() + "/";
    m_placeholder = "Folder name";
    m_confirm_label = "Create";
    m_text_value.clear();
    m_caret_pos = 0;
    m_ready_to_close = false;
    m_on_confirm_string = std::move(on_confirm);
    m_on_confirm_void = nullptr;

    m_parent_hwnd = parent;
    m_dpi = parent ? GetDpiForWindow(parent) : 96;
    if (m_dpi == 0) m_dpi = 96;
    refresh_fonts();

    const float scale = static_cast<float>(m_dpi) / 96.0F;
    const int w = static_cast<int>(360.0F * scale);
    const int h = static_cast<int>(92.0F * scale);

    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = dialog_proc;
    wc.cbWndExtra = sizeof(Win32PromptDialog*);
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hbrBackground = nullptr;
    wc.lpszClassName = prompt_dialog_class_name;
    RegisterClassExW(&wc);

    int pos_x = CW_USEDEFAULT;
    int pos_y = CW_USEDEFAULT;
    if (parent && IsWindow(parent))
    {
        RECT prc{};
        GetWindowRect(parent, &prc);
        pos_x = prc.left + (prc.right - prc.left - w) / 2;
        pos_y = prc.top + (prc.bottom - prc.top - h) / 2;
    }

    m_hwnd = CreateWindowExW(
        WS_EX_APPWINDOW, prompt_dialog_class_name, L"New Folder",
        WS_POPUP | WS_CLIPCHILDREN | WS_THICKFRAME, pos_x, pos_y,
        w, h, parent, nullptr, instance, this);

    if (m_hwnd != nullptr)
    {
        BOOL dark = TRUE;
        DwmSetWindowAttribute(m_hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
        constexpr DWORD dwm_corner_pref_attr = 33;
        const DWORD corner_pref = 2; // DWMWCP_ROUND
        DwmSetWindowAttribute(m_hwnd, dwm_corner_pref_attr, &corner_pref, sizeof(corner_pref));
        constexpr DWORD dwm_border_color_attr = 34;
        const COLORREF border_col = RGB(58, 60, 68);
        DwmSetWindowAttribute(m_hwnd, dwm_border_color_attr, &border_col, sizeof(border_col));
        const MARGINS frame_margins{0, 0, 0, 0};
        DwmExtendFrameIntoClientArea(m_hwnd, &frame_margins);

        SetTimer(m_hwnd, 1, 500, nullptr);
        ShowWindow(m_hwnd, SW_SHOW);
        UpdateWindow(m_hwnd);
        SetForegroundWindow(m_hwnd);
        SetFocus(m_hwnd);
        m_ready_to_close = true;
        return true;
    }
    return false;
}

bool Win32PromptDialog::open_new_file(
    HWND parent,
    const std::filesystem::path& target_dir,
    std::function<void(const std::string&)> on_confirm)
{
    close();

    m_mode = PromptDialogMode::NewFile;
    m_title = "New File";
    m_target_path = target_dir;
    m_subtitle = "Target: " + target_dir.filename().string() + "/";
    m_placeholder = "File name (e.g. main.cpp)";
    m_confirm_label = "Create";
    m_text_value.clear();
    m_caret_pos = 0;
    m_ready_to_close = false;
    m_on_confirm_string = std::move(on_confirm);
    m_on_confirm_void = nullptr;

    m_parent_hwnd = parent;
    m_dpi = parent ? GetDpiForWindow(parent) : 96;
    if (m_dpi == 0) m_dpi = 96;
    refresh_fonts();

    const float scale = static_cast<float>(m_dpi) / 96.0F;
    const int w = static_cast<int>(360.0F * scale);
    const int h = static_cast<int>(92.0F * scale);

    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = dialog_proc;
    wc.cbWndExtra = sizeof(Win32PromptDialog*);
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hbrBackground = nullptr;
    wc.lpszClassName = prompt_dialog_class_name;
    RegisterClassExW(&wc);

    int pos_x = CW_USEDEFAULT;
    int pos_y = CW_USEDEFAULT;
    if (parent && IsWindow(parent))
    {
        RECT prc{};
        GetWindowRect(parent, &prc);
        pos_x = prc.left + (prc.right - prc.left - w) / 2;
        pos_y = prc.top + (prc.bottom - prc.top - h) / 2;
    }

    m_hwnd = CreateWindowExW(
        WS_EX_APPWINDOW, prompt_dialog_class_name, L"New File",
        WS_POPUP | WS_CLIPCHILDREN | WS_THICKFRAME, pos_x, pos_y,
        w, h, parent, nullptr, instance, this);

    if (m_hwnd != nullptr)
    {
        BOOL dark = TRUE;
        DwmSetWindowAttribute(m_hwnd, 20, &dark, sizeof(dark));
        constexpr DWORD dwm_corner_pref_attr = 33;
        const DWORD corner_pref = 2;
        DwmSetWindowAttribute(m_hwnd, dwm_corner_pref_attr, &corner_pref, sizeof(corner_pref));
        constexpr DWORD dwm_border_color_attr = 34;
        const COLORREF border_col = RGB(58, 60, 68);
        DwmSetWindowAttribute(m_hwnd, dwm_border_color_attr, &border_col, sizeof(border_col));
        const MARGINS frame_margins{0, 0, 0, 0};
        DwmExtendFrameIntoClientArea(m_hwnd, &frame_margins);

        SetTimer(m_hwnd, 1, 500, nullptr);
        ShowWindow(m_hwnd, SW_SHOW);
        UpdateWindow(m_hwnd);
        SetForegroundWindow(m_hwnd);
        SetFocus(m_hwnd);
        m_ready_to_close = true;
        return true;
    }
    return false;
}

bool Win32PromptDialog::open_rename(
    HWND parent,
    const std::filesystem::path& item_path,
    std::function<void(const std::string&)> on_confirm)
{
    close();

    m_mode = PromptDialogMode::Rename;
    m_title = "Rename Item";
    m_target_path = item_path;
    m_subtitle = "Rename: " + item_path.filename().string();
    m_placeholder = "New name";
    m_confirm_label = "Rename";
    m_text_value = item_path.filename().string();
    m_caret_pos = m_text_value.size();
    m_ready_to_close = false;
    m_on_confirm_string = std::move(on_confirm);
    m_on_confirm_void = nullptr;

    m_parent_hwnd = parent;
    m_dpi = parent ? GetDpiForWindow(parent) : 96;
    if (m_dpi == 0) m_dpi = 96;
    refresh_fonts();

    const float scale = static_cast<float>(m_dpi) / 96.0F;
    const int w = static_cast<int>(360.0F * scale);
    const int h = static_cast<int>(92.0F * scale);

    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = dialog_proc;
    wc.cbWndExtra = sizeof(Win32PromptDialog*);
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hbrBackground = nullptr;
    wc.lpszClassName = prompt_dialog_class_name;
    RegisterClassExW(&wc);

    int pos_x = CW_USEDEFAULT;
    int pos_y = CW_USEDEFAULT;
    if (parent && IsWindow(parent))
    {
        RECT prc{};
        GetWindowRect(parent, &prc);
        pos_x = prc.left + (prc.right - prc.left - w) / 2;
        pos_y = prc.top + (prc.bottom - prc.top - h) / 2;
    }

    m_hwnd = CreateWindowExW(
        WS_EX_APPWINDOW, prompt_dialog_class_name, L"Rename",
        WS_POPUP | WS_CLIPCHILDREN | WS_THICKFRAME, pos_x, pos_y,
        w, h, parent, nullptr, instance, this);

    if (m_hwnd != nullptr)
    {
        BOOL dark = TRUE;
        DwmSetWindowAttribute(m_hwnd, 20, &dark, sizeof(dark));
        constexpr DWORD dwm_corner_pref_attr = 33;
        const DWORD corner_pref = 2;
        DwmSetWindowAttribute(m_hwnd, dwm_corner_pref_attr, &corner_pref, sizeof(corner_pref));
        constexpr DWORD dwm_border_color_attr = 34;
        const COLORREF border_col = RGB(58, 60, 68);
        DwmSetWindowAttribute(m_hwnd, dwm_border_color_attr, &border_col, sizeof(border_col));
        const MARGINS frame_margins{0, 0, 0, 0};
        DwmExtendFrameIntoClientArea(m_hwnd, &frame_margins);

        SetTimer(m_hwnd, 1, 500, nullptr);
        ShowWindow(m_hwnd, SW_SHOW);
        UpdateWindow(m_hwnd);
        SetForegroundWindow(m_hwnd);
        SetFocus(m_hwnd);
        m_ready_to_close = true;
        return true;
    }
    return false;
}

bool Win32PromptDialog::open_delete(
    HWND parent,
    const std::filesystem::path& item_path,
    std::function<void()> on_confirm)
{
    close();

    m_mode = PromptDialogMode::ConfirmDelete;
    m_title = "Delete Item";
    m_target_path = item_path;
    m_subtitle = "Are you sure you want to permanently delete '" + item_path.filename().string() + "'?";
    m_placeholder.clear();
    m_confirm_label = "Delete";
    m_text_value.clear();
    m_caret_pos = 0;
    m_ready_to_close = false;
    m_on_confirm_string = nullptr;
    m_on_confirm_void = std::move(on_confirm);

    m_parent_hwnd = parent;
    m_dpi = parent ? GetDpiForWindow(parent) : 96;
    if (m_dpi == 0) m_dpi = 96;
    refresh_fonts();

    const float scale = static_cast<float>(m_dpi) / 96.0F;
    const int w = static_cast<int>(360.0F * scale);
    const int h = static_cast<int>(130.0F * scale);

    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = dialog_proc;
    wc.cbWndExtra = sizeof(Win32PromptDialog*);
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hbrBackground = nullptr;
    wc.lpszClassName = prompt_dialog_class_name;
    RegisterClassExW(&wc);

    int pos_x = CW_USEDEFAULT;
    int pos_y = CW_USEDEFAULT;
    if (parent && IsWindow(parent))
    {
        RECT prc{};
        GetWindowRect(parent, &prc);
        pos_x = prc.left + (prc.right - prc.left - w) / 2;
        pos_y = prc.top + (prc.bottom - prc.top - h) / 2;
    }

    m_hwnd = CreateWindowExW(
        WS_EX_APPWINDOW, prompt_dialog_class_name, L"Delete Item",
        WS_POPUP | WS_CLIPCHILDREN | WS_THICKFRAME, pos_x, pos_y,
        w, h, parent, nullptr, instance, this);

    if (m_hwnd != nullptr)
    {
        BOOL dark = TRUE;
        DwmSetWindowAttribute(m_hwnd, 20, &dark, sizeof(dark));
        constexpr DWORD dwm_corner_pref_attr = 33;
        const DWORD corner_pref = 2;
        DwmSetWindowAttribute(m_hwnd, dwm_corner_pref_attr, &corner_pref, sizeof(corner_pref));
        constexpr DWORD dwm_border_color_attr = 34;
        const COLORREF border_col = RGB(58, 60, 68);
        DwmSetWindowAttribute(m_hwnd, dwm_border_color_attr, &border_col, sizeof(border_col));
        const MARGINS frame_margins{0, 0, 0, 0};
        DwmExtendFrameIntoClientArea(m_hwnd, &frame_margins);

        SetTimer(m_hwnd, 1, 500, nullptr);
        ShowWindow(m_hwnd, SW_SHOW);
        UpdateWindow(m_hwnd);
        SetForegroundWindow(m_hwnd);
        SetFocus(m_hwnd);
        m_ready_to_close = true;
        return true;
    }
    return false;
}

void Win32PromptDialog::close() noexcept
{
    m_ready_to_close = false;
    if (m_hwnd != nullptr && IsWindow(m_hwnd))
    {
        KillTimer(m_hwnd, 1);
        HWND hwnd = m_hwnd;
        m_hwnd = nullptr;
        DestroyWindow(hwnd);
    }
    if (m_parent_hwnd != nullptr && IsWindow(m_parent_hwnd))
    {
        SetForegroundWindow(m_parent_hwnd);
        SetFocus(m_parent_hwnd);
        InvalidateRect(m_parent_hwnd, nullptr, TRUE);
        m_parent_hwnd = nullptr;
    }
}

bool Win32PromptDialog::is_open() const noexcept
{
    return m_hwnd != nullptr && IsWindow(m_hwnd);
}

void Win32PromptDialog::submit()
{
    if (m_mode == PromptDialogMode::ConfirmDelete)
    {
        if (m_on_confirm_void)
        {
            m_on_confirm_void();
        }
        close();
    }
    else
    {
        if (!m_text_value.empty())
        {
            if (m_on_confirm_string)
            {
                m_on_confirm_string(m_text_value);
            }
            close();
        }
    }
}

LRESULT CALLBACK Win32PromptDialog::dialog_proc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param)
{
    Win32PromptDialog* self = nullptr;
    if (message == WM_NCCREATE)
    {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(l_param);
        self = reinterpret_cast<Win32PromptDialog*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    else
    {
        self = reinterpret_cast<Win32PromptDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self)
    {
        return self->handle_message(hwnd, message, w_param, l_param);
    }
    return DefWindowProcW(hwnd, message, w_param, l_param);
}

LRESULT Win32PromptDialog::handle_message(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param)
{
    const float scale = static_cast<float>(m_dpi) / 96.0F;

    switch (message)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        const int w = rc.right - rc.left;
        const int h = rc.bottom - rc.top;

        HDC mem_dc = CreateCompatibleDC(hdc);
        HBITMAP mem_bm = CreateCompatibleBitmap(hdc, w, h);
        HGDIOBJ old_bm = SelectObject(mem_dc, mem_bm);

        // 1. Background Fill (macOS deep dark)
        const COLORREF bg_col = RGB(30, 31, 36);
        HBRUSH bg_brush = CreateSolidBrush(bg_col);
        FillRect(mem_dc, &rc, bg_brush);
        DeleteObject(bg_brush);

        // 2. Centered Bold Title
        SelectObject(mem_dc, m_title_font);
        SetBkMode(mem_dc, TRANSPARENT);
        SetTextColor(mem_dc, RGB(235, 238, 242));

        const std::wstring title_w = utf8_to_wstring(m_title);
        SIZE title_sz{};
        GetTextExtentPoint32W(mem_dc, title_w.c_str(), static_cast<int>(title_w.size()), &title_sz);
        const int title_x = (w - title_sz.cx) / 2;
        const int title_y = static_cast<int>(14.0F * scale);
        TextOutW(mem_dc, title_x, title_y, title_w.c_str(), static_cast<int>(title_w.size()));

        if (m_mode == PromptDialogMode::ConfirmDelete)
        {
            // Subtitle
            SelectObject(mem_dc, m_small_font);
            SetTextColor(mem_dc, RGB(145, 150, 160));
            const std::wstring sub_w = utf8_to_wstring(m_subtitle);
            SIZE sub_sz{};
            GetTextExtentPoint32W(mem_dc, sub_w.c_str(), static_cast<int>(sub_w.size()), &sub_sz);
            const int sub_x = (w - sub_sz.cx) / 2;
            const int sub_y = title_y + title_sz.cy + static_cast<int>(4.0F * scale);
            TextOutW(mem_dc, sub_x, sub_y, sub_w.c_str(), static_cast<int>(sub_w.size()));

            // Cancel and Delete Buttons
            const int btn_w = static_cast<int>(78.0F * scale);
            const int btn_h = static_cast<int>(26.0F * scale);
            const int btn_y = h - static_cast<int>(14.0F * scale) - btn_h;

            const int cancel_x = w / 2 - btn_w - static_cast<int>(6.0F * scale);
            const int ok_x = w / 2 + static_cast<int>(6.0F * scale);

            RECT cancel_rc{cancel_x, btn_y, cancel_x + btn_w, btn_y + btn_h};
            fill_rounded_rect(mem_dc, cancel_rc, m_cancel_hovered ? RGB(58, 61, 68) : RGB(45, 47, 52), static_cast<int>(6.0F * scale));
            SelectObject(mem_dc, m_ui_font);
            SetTextColor(mem_dc, RGB(204, 204, 204));
            DrawTextW(mem_dc, L"Cancel", -1, &cancel_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            RECT ok_rc{ok_x, btn_y, ok_x + btn_w, btn_y + btn_h};
            fill_rounded_rect(mem_dc, ok_rc, m_ok_hovered ? RGB(235, 65, 70) : RGB(218, 45, 50), static_cast<int>(6.0F * scale));
            SetTextColor(mem_dc, RGB(255, 255, 255));
            DrawTextW(mem_dc, L"Delete", -1, &ok_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        else
        {
            // Pure Transparent Borderless Input Field (No background box fill)
            const int input_x = static_cast<int>(22.0F * scale);
            const int input_y = static_cast<int>(46.0F * scale);

            SelectObject(mem_dc, m_ui_font);
            if (m_text_value.empty())
            {
                SetTextColor(mem_dc, RGB(110, 115, 130));
                const std::wstring ph_w = utf8_to_wstring(m_placeholder);
                TextOutW(mem_dc, input_x + 4, input_y + 6, ph_w.c_str(), static_cast<int>(ph_w.size()));
            }
            else
            {
                SetTextColor(mem_dc, RGB(240, 242, 245));
                const std::wstring val_w = utf8_to_wstring(m_text_value);
                TextOutW(mem_dc, input_x + 4, input_y + 6, val_w.c_str(), static_cast<int>(val_w.size()));
            }

            if (m_caret_visible)
            {
                std::size_t cp = std::min(m_caret_pos, m_text_value.size());
                std::string pref = m_text_value.substr(0, cp);
                std::wstring pref_w = utf8_to_wstring(pref);
                SIZE pref_sz{};
                GetTextExtentPoint32W(mem_dc, pref_w.c_str(), static_cast<int>(pref_w.size()), &pref_sz);
                const int caret_x = input_x + 4 + pref_sz.cx;

                HPEN caret_pen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
                HGDIOBJ old_p = SelectObject(mem_dc, caret_pen);
                MoveToEx(mem_dc, caret_x, input_y + 5, nullptr);
                LineTo(mem_dc, caret_x, input_y + 25);
                SelectObject(mem_dc, old_p);
                DeleteObject(caret_pen);
            }
        }

        // Draw thin subtle 1px border matching IDE / AddNewItemDialog style
        HPEN border_pen = CreatePen(PS_SOLID, 1, RGB(48, 50, 55));
        HGDIOBJ old_pen = SelectObject(mem_dc, border_pen);
        HBRUSH null_brush = static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
        HGDIOBJ old_br = SelectObject(mem_dc, null_brush);
        Rectangle(mem_dc, 0, 0, w, h);
        SelectObject(mem_dc, old_br);
        SelectObject(mem_dc, old_pen);
        DeleteObject(border_pen);

        BitBlt(hdc, 0, 0, w, h, mem_dc, 0, 0, SRCCOPY);
        SelectObject(mem_dc, old_bm);
        DeleteObject(mem_bm);
        DeleteDC(mem_dc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_NCCALCSIZE:
        return 0;

    case WM_NCPAINT:
        return 0;

    case WM_NCACTIVATE:
        InvalidateRect(hwnd, nullptr, FALSE);
        return TRUE;

    case WM_CLOSE:
        close();
        return 0;

    case WM_TIMER:
        if (w_param == 1)
        {
            m_caret_visible = !m_caret_visible;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_NCHITTEST:
    {
        POINT pt{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
        ScreenToClient(hwnd, &pt);
        if (pt.y < static_cast<int>(38.0F * scale))
        {
            return HTCAPTION;
        }
        return HTCLIENT;
    }

    case WM_MOUSEMOVE:
    {
        const int x = GET_X_LPARAM(l_param);
        const int y = GET_Y_LPARAM(l_param);
        RECT rc;
        GetClientRect(hwnd, &rc);
        const int w = rc.right - rc.left;
        const int h = rc.bottom - rc.top;
        POINT pt{x, y};

        bool new_ok_h = false;
        bool new_cancel_h = false;

        if (m_mode == PromptDialogMode::ConfirmDelete)
        {
            const int btn_w = static_cast<int>(78.0F * scale);
            const int btn_h = static_cast<int>(26.0F * scale);
            const int btn_y = h - static_cast<int>(14.0F * scale) - btn_h;
            RECT cancel_rc{w / 2 - btn_w - static_cast<int>(6.0F * scale), btn_y, w / 2 - static_cast<int>(6.0F * scale), btn_y + btn_h};
            RECT ok_rc{w / 2 + static_cast<int>(6.0F * scale), btn_y, w / 2 + static_cast<int>(6.0F * scale) + btn_w, btn_y + btn_h};
            new_cancel_h = PtInRect(&cancel_rc, pt) != FALSE;
            new_ok_h = PtInRect(&ok_rc, pt) != FALSE;
        }

        if (new_ok_h != m_ok_hovered || new_cancel_h != m_cancel_hovered)
        {
            m_ok_hovered = new_ok_h;
            m_cancel_hovered = new_cancel_h;
            InvalidateRect(hwnd, nullptr, FALSE);
        }

        TRACKMOUSEEVENT tme{sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_MOUSELEAVE:
        m_ok_hovered = false;
        m_cancel_hovered = false;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_LBUTTONDOWN:
    {
        SetFocus(hwnd);
        const int x = GET_X_LPARAM(l_param);
        const int y = GET_Y_LPARAM(l_param);
        RECT rc;
        GetClientRect(hwnd, &rc);
        const int w = rc.right - rc.left;
        const int h = rc.bottom - rc.top;
        POINT pt{x, y};

        if (m_mode == PromptDialogMode::ConfirmDelete)
        {
            const int btn_w = static_cast<int>(78.0F * scale);
            const int btn_h = static_cast<int>(26.0F * scale);
            const int btn_y = h - static_cast<int>(14.0F * scale) - btn_h;
            RECT cancel_rc{w / 2 - btn_w - static_cast<int>(6.0F * scale), btn_y, w / 2 - static_cast<int>(6.0F * scale), btn_y + btn_h};
            RECT ok_rc{w / 2 + static_cast<int>(6.0F * scale), btn_y, w / 2 + static_cast<int>(6.0F * scale) + btn_w, btn_y + btn_h};

            if (PtInRect(&cancel_rc, pt))
            {
                close();
                return 0;
            }
            if (PtInRect(&ok_rc, pt))
            {
                submit();
                return 0;
            }
        }
        return 0;
    }

    case WM_CHAR:
    {
        if (m_mode == PromptDialogMode::ConfirmDelete)
        {
            return 0;
        }
        const wchar_t ch = static_cast<wchar_t>(w_param);
        if (ch == VK_BACK || ch == VK_RETURN || ch == VK_ESCAPE || ch == 0x0A)
        {
            return 0;
        }
        if (ch >= 32)
        {
            char utf8_buf[8] = {};
            WideCharToMultiByte(CP_UTF8, 0, &ch, 1, utf8_buf, sizeof(utf8_buf), nullptr, nullptr);
            m_caret_pos = std::min(m_caret_pos, m_text_value.size());
            m_text_value.insert(m_caret_pos, utf8_buf);
            m_caret_pos += strlen(utf8_buf);
            m_caret_visible = true;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_KEYDOWN:
    {
        if (w_param == VK_ESCAPE)
        {
            close();
            return 0;
        }
        if (w_param == VK_RETURN)
        {
            submit();
            return 0;
        }

        if (m_mode != PromptDialogMode::ConfirmDelete)
        {
            if (w_param == VK_BACK)
            {
                if (m_caret_pos > 0 && !m_text_value.empty())
                {
                    m_caret_pos = std::min(m_caret_pos, m_text_value.size());
                    m_text_value.erase(m_caret_pos - 1, 1);
                    --m_caret_pos;
                    m_caret_visible = true;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            }
            if (w_param == VK_DELETE)
            {
                if (m_caret_pos < m_text_value.size())
                {
                    m_text_value.erase(m_caret_pos, 1);
                    m_caret_visible = true;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            }
            if (w_param == VK_LEFT)
            {
                if (m_caret_pos > 0)
                {
                    --m_caret_pos;
                    m_caret_visible = true;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            }
            if (w_param == VK_RIGHT)
            {
                if (m_caret_pos < m_text_value.size())
                {
                    ++m_caret_pos;
                    m_caret_visible = true;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            }
            if (w_param == VK_HOME)
            {
                m_caret_pos = 0;
                m_caret_visible = true;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (w_param == VK_END)
            {
                m_caret_pos = m_text_value.size();
                m_caret_visible = true;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (w_param == 'A' && (GetKeyState(VK_CONTROL) & 0x8000))
            {
                m_caret_pos = m_text_value.size();
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (w_param == 'V' && (GetKeyState(VK_CONTROL) & 0x8000))
            {
                if (OpenClipboard(hwnd))
                {
                    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
                    if (hData)
                    {
                        wchar_t* pszText = static_cast<wchar_t*>(GlobalLock(hData));
                        if (pszText)
                        {
                            char utf8_buf[1024] = {};
                            WideCharToMultiByte(CP_UTF8, 0, pszText, -1, utf8_buf, sizeof(utf8_buf), nullptr, nullptr);
                            m_caret_pos = std::min(m_caret_pos, m_text_value.size());
                            m_text_value.insert(m_caret_pos, utf8_buf);
                            m_caret_pos += strlen(utf8_buf);
                            GlobalUnlock(hData);
                        }
                    }
                    CloseClipboard();
                    m_caret_visible = true;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            }
        }
        return 0;
    }

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        m_hwnd = nullptr;
        return 0;
    }

    return DefWindowProcW(hwnd, message, w_param, l_param);
}

} // namespace Zenvra::Platform::Win32::Components
