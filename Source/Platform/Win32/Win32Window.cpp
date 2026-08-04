#include "Platform/Win32/Win32Window.h"

#include "Platform/Win32/Components/FileDropTarget.h"
#include "UI/Chrome/WindowMenuModel.h"
#include "Utility/Math.h"
#include "Utility/TextEncoding.h"
#include <dwmapi.h>
#include <uxtheme.h>
#include <vssym32.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <utility>

namespace Zenvra::Platform::Win32
{

namespace
{

constexpr DWORD dwm_immersive_dark_mode_attribute = 20;
constexpr UINT_PTR editor_caret_timer_id = 1;

COLORREF to_color_ref(const UI::Theme::Color& color)
{
    return RGB(color.red, color.green, color.blue);
}

using Zenvra::Utility::round_to_int;

RECT to_native_rect(const UI::Rect& rectangle)
{
    return RECT{
        round_to_int(rectangle.x),
        round_to_int(rectangle.y),
        round_to_int(rectangle.right()),
        round_to_int(rectangle.bottom()),
    };
}

void fill_rectangle(HDC device_context, const UI::Rect& rectangle, const UI::Theme::Color& color)
{
    RECT native_rectangle = to_native_rect(rectangle);
    HBRUSH brush = CreateSolidBrush(to_color_ref(color));
    FillRect(device_context, &native_rectangle, brush);
    DeleteObject(brush);
}

void draw_centered_text(
    HDC device_context,
    const wchar_t* text,
    RECT rectangle,
    const UI::Theme::Color& color)
{
    SetTextColor(device_context, to_color_ref(color));
    DrawTextW(
        device_context,
        text,
        -1,
        &rectangle,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
}

int caption_button_state(
    UI::Chrome::WindowControl control,
    UI::Chrome::WindowControl hovered_control,
    UI::Chrome::WindowControl pressed_control)
{
    if (pressed_control == control)
    {
        return MINBS_PUSHED;
    }
    if (hovered_control == control)
    {
        return MINBS_HOT;
    }
    return MINBS_NORMAL;
}

void draw_custom_caption_button(
    HDC device_context,
    const UI::Rect& bounds,
    UI::Chrome::WindowControl control,
    int theme_state,
    bool is_maximized,
    const UI::Theme::StudioTheme& theme,
    float scale)
{
    if (theme_state == MINBS_HOT || theme_state == MINBS_PUSHED)
    {
        UI::Theme::Color bg_color = theme_state == MINBS_PUSHED ? theme.pressed : theme.hover;
        if (control == UI::Chrome::WindowControl::Close)
        {
            bg_color = theme_state == MINBS_PUSHED ? theme.pressed : theme.close_hover;
        }
        fill_rectangle(device_context, bounds, bg_color);
    }

    UI::Theme::Color icon_color = theme.text_primary;
    if (control == UI::Chrome::WindowControl::Close && (theme_state == MINBS_HOT || theme_state == MINBS_PUSHED))
    {
        icon_color = UI::Theme::Color{255, 255, 255, 255};
    }

    HPEN icon_pen = CreatePen(PS_SOLID, std::max(1, round_to_int(scale)), to_color_ref(icon_color));
    HGDIOBJ previous_pen = SelectObject(device_context, icon_pen);
    HGDIOBJ previous_brush = SelectObject(device_context, GetStockObject(HOLLOW_BRUSH));

    const int center_x = round_to_int(bounds.x + bounds.width * 0.5F);
    const int center_y = round_to_int(bounds.y + bounds.height * 0.5F);
    const int icon_size = round_to_int(10.0F * scale);
    const int half_size = icon_size / 2;

    if (control == UI::Chrome::WindowControl::Minimize)
    {
        MoveToEx(device_context, center_x - half_size, center_y, nullptr);
        LineTo(device_context, center_x + half_size + 1, center_y);
    }
    else if (control == UI::Chrome::WindowControl::MaximizeRestore)
    {
        if (is_maximized)
        {
            const int offset = round_to_int(2.0F * scale);
            
            MoveToEx(device_context, center_x - half_size + offset, center_y - half_size, nullptr);
            LineTo(device_context, center_x + half_size + 1, center_y - half_size);
            LineTo(device_context, center_x + half_size + 1, center_y + half_size - offset + 1);
            
            MoveToEx(device_context, center_x - half_size + offset, center_y - half_size, nullptr);
            LineTo(device_context, center_x - half_size + offset, center_y - half_size + offset);

            Rectangle(
                device_context, 
                center_x - half_size, 
                center_y - half_size + offset, 
                center_x + half_size - offset + 1, 
                center_y + half_size + 1);
        }
        else
        {
            Rectangle(
                device_context, 
                center_x - half_size, 
                center_y - half_size, 
                center_x + half_size + 1, 
                center_y + half_size + 1);
        }
    }
    else if (control == UI::Chrome::WindowControl::Close)
    {
        MoveToEx(device_context, center_x - half_size, center_y - half_size, nullptr);
        LineTo(device_context, center_x + half_size + 1, center_y + half_size + 1);
        
        MoveToEx(device_context, center_x - half_size, center_y + half_size, nullptr);
        LineTo(device_context, center_x + half_size + 1, center_y - half_size - 1);
    }

    SelectObject(device_context, previous_brush);
    SelectObject(device_context, previous_pen);
    DeleteObject(icon_pen);
}

} // namespace

Win32Window::Win32Window(const WindowSpecification& specification)
    : m_instance_handle(GetModuleHandleW(nullptr)),
      m_specification(specification),
      m_window_title(utf8_to_wide(specification.title))
{
    m_capabilities.custom_chrome = true;
    m_capabilities.native_titlebar_hit_test = true;
    m_capabilities.native_resize = true;
    m_capabilities.native_snap = true;
    m_capabilities.per_monitor_dpi = true;
}

Win32Window::~Win32Window()
{
    if (m_window_handle != nullptr && IsWindow(m_window_handle) != FALSE)
    {
        DestroyWindow(m_window_handle);
    }
    if (m_ui_font != nullptr)
    {
        DeleteObject(m_ui_font);
    }
}

bool Win32Window::initialize()
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = m_instance_handle;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    window_class.lpszClassName = window_class_name;

    if (RegisterClassExW(&window_class) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        return false;
    }

    m_window_handle = CreateWindowExW(
        0,
        window_class_name,
        m_window_title.c_str(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        static_cast<int>(m_specification.width),
        static_cast<int>(m_specification.height),
        nullptr,
        nullptr,
        m_instance_handle,
        this);

    if (m_window_handle == nullptr)
    {
        return false;
    }

    const BOOL dark_mode_enabled = TRUE;
    DwmSetWindowAttribute(
        m_window_handle,
        dwm_immersive_dark_mode_attribute,
        &dark_mode_enabled,
        sizeof(dark_mode_enabled));

    if (!m_menubar.load(m_instance_handle) || !m_menubar.attach(m_window_handle))
    {
        std::clog << "Warning: the window menu resource could not be loaded.\n";
    }

    m_dpi = GetDpiForWindow(m_window_handle);
    refresh_ui_font();
    if (!m_workspace_renderer.initialize(m_dpi))
    {
        std::cerr << "Fatal error: the Win32 workspace renderer could not be initialized.\n";
        return false;
    }
    static_cast<void>(m_workspace_renderer.create_buffer());
    Components::FileDropTarget::set_enabled(m_window_handle, true);
    static_cast<void>(SetTimer(m_window_handle, editor_caret_timer_id, 100, nullptr));
    refresh_chrome_layout();
    set_custom_chrome_enabled(m_specification.custom_chrome_enabled);
    return true;
}

void Win32Window::show()
{
    if (m_window_handle == nullptr)
    {
        return;
    }

    ShowWindow(m_window_handle, SW_SHOWDEFAULT);
    UpdateWindow(m_window_handle);
}

void Win32Window::poll_events()
{
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != FALSE)
    {
        if (message.message == WM_QUIT)
        {
            m_should_close = true;
            continue;
        }

        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

bool Win32Window::should_close() const
{
    return m_should_close;
}

void Win32Window::minimize()
{
    ShowWindow(m_window_handle, SW_MINIMIZE);
}

void Win32Window::maximize()
{
    ShowWindow(m_window_handle, SW_MAXIMIZE);
}

void Win32Window::restore()
{
    ShowWindow(m_window_handle, SW_RESTORE);
}

void Win32Window::request_close()
{
    if (m_window_handle != nullptr)
    {
        PostMessageW(m_window_handle, WM_CLOSE, 0, 0);
    }
}

bool Win32Window::is_maximized() const
{
    return m_window_handle != nullptr && IsZoomed(m_window_handle) != FALSE;
}

bool Win32Window::is_minimized() const
{
    return m_window_handle != nullptr && IsIconic(m_window_handle) != FALSE;
}

bool Win32Window::is_focused() const
{
    return m_window_handle != nullptr && GetForegroundWindow() == m_window_handle;
}

const WindowCapabilities& Win32Window::get_capabilities() const noexcept
{
    return m_capabilities;
}

void* Win32Window::get_native_handle() const noexcept
{
    return m_window_handle;
}

void Win32Window::set_custom_chrome_enabled(bool enabled)
{
    m_custom_chrome_enabled = enabled && m_capabilities.custom_chrome;
    if (m_window_handle == nullptr)
    {
        return;
    }

    if (m_custom_chrome_enabled)
    {
        static_cast<void>(m_menubar.detach());
    }
    else
    {
        close_menu_overlay();
        static_cast<void>(m_menubar.attach(m_window_handle));
    }

    SetWindowPos(
        m_window_handle,
        nullptr,
        0,
        0,
        0,
        0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    refresh_chrome_layout();
    InvalidateRect(m_window_handle, nullptr, FALSE);
}

void Win32Window::set_titlebar_hit_test_callback(TitlebarHitTestCallback callback)
{
    m_titlebar_hit_test_callback = std::move(callback);
}

void Win32Window::set_command_invoked_callback(CommandInvokedCallback callback)
{
    m_command_invoked_callback = std::move(callback);
    m_menubar.set_command_invoked_callback([this](std::string_view command_id) {
        const std::optional<bool> editor_result =
            m_workspace_renderer.handle_editor_command(command_id);
        if (editor_result)
        {
            if (*editor_result && m_window_handle != nullptr)
            {
                InvalidateRect(m_window_handle, nullptr, FALSE);
            }
            return;
        }
        if (m_command_invoked_callback)
        {
            m_command_invoked_callback(command_id);
        }
    });
}

void Win32Window::set_command_state_query_callback(CommandStateQueryCallback callback)
{
    m_command_state_query_callback = std::move(callback);
    m_menubar.set_command_state_query_callback([this](std::string_view command_id) {
        const std::optional<bool> editor_enabled =
            m_workspace_renderer.is_editor_command_enabled(command_id);
        if (editor_enabled)
        {
            return CommandPresentationState{*editor_enabled, false};
        }
        return m_command_state_query_callback
            ? m_command_state_query_callback(command_id)
            : CommandPresentationState{true, false};
    });
}

LRESULT CALLBACK Win32Window::window_proc(
    HWND window_handle,
    UINT message,
    WPARAM w_param,
    LPARAM l_param)
{
    Win32Window* window = nullptr;

    if (message == WM_NCCREATE)
    {
        const auto* create_data = reinterpret_cast<const CREATESTRUCTW*>(l_param);
        window = static_cast<Win32Window*>(create_data->lpCreateParams);
        window->m_window_handle = window_handle;
        SetWindowLongPtrW(window_handle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
    }
    else
    {
        window = reinterpret_cast<Win32Window*>(GetWindowLongPtrW(window_handle, GWLP_USERDATA));
    }

    if (window != nullptr)
    {
        return window->handle_message(window_handle, message, w_param, l_param);
    }

    return DefWindowProcW(window_handle, message, w_param, l_param);
}

LRESULT Win32Window::handle_message(
    HWND window_handle,
    UINT message,
    WPARAM w_param,
    LPARAM l_param)
{
    switch (message)
    {
    case WM_DROPFILES:
    {
        const HDROP drop = reinterpret_cast<HDROP>(w_param);
        const std::vector<std::filesystem::path> dropped_paths =
            Components::FileDropTarget::collect_paths(drop);
        if (m_workspace_renderer.open_dropped_paths(dropped_paths) > 0)
        {
            SetFocus(window_handle);
            InvalidateRect(window_handle, nullptr, FALSE);
        }
        return 0;
    }

    case WM_NCCALCSIZE:
        if (m_custom_chrome_enabled && w_param != FALSE)
        {
            if (is_maximized())
            {
                auto* parameters = reinterpret_cast<NCCALCSIZE_PARAMS*>(l_param);
                MONITORINFO monitor_info{};
                monitor_info.cbSize = sizeof(monitor_info);
                const HMONITOR monitor = MonitorFromWindow(window_handle, MONITOR_DEFAULTTONEAREST);
                if (GetMonitorInfoW(monitor, &monitor_info) != FALSE)
                {
                    parameters->rgrc[0] = monitor_info.rcWork;
                }
            }
            return 0;
        }
        break;

    case WM_NCHITTEST:
        if (m_custom_chrome_enabled)
        {
            return hit_test_non_client(l_param);
        }
        break;

    case WM_NCLBUTTONDOWN:
        if (m_custom_chrome_enabled)
        {
            if (w_param == HTMINBUTTON)
            {
                m_pressed_control = UI::Chrome::WindowControl::Minimize;
                InvalidateRect(window_handle, nullptr, FALSE);
                return 0;
            }
            if (w_param == HTMAXBUTTON)
            {
                m_pressed_control = UI::Chrome::WindowControl::MaximizeRestore;
                InvalidateRect(window_handle, nullptr, FALSE);
                return 0;
            }
            if (w_param == HTCLOSE)
            {
                m_pressed_control = UI::Chrome::WindowControl::Close;
                InvalidateRect(window_handle, nullptr, FALSE);
                return 0;
            }
        }
        break;

    case WM_NCLBUTTONUP:
        if (m_custom_chrome_enabled && m_pressed_control != UI::Chrome::WindowControl::NoControl)
        {
            const UI::Chrome::WindowControl pressed_control = m_pressed_control;
            m_pressed_control = UI::Chrome::WindowControl::NoControl;

            if (pressed_control == UI::Chrome::WindowControl::Minimize && w_param == HTMINBUTTON)
            {
                minimize();
            }
            else if (pressed_control == UI::Chrome::WindowControl::MaximizeRestore && w_param == HTMAXBUTTON)
            {
                is_maximized() ? restore() : maximize();
            }
            else if (pressed_control == UI::Chrome::WindowControl::Close && w_param == HTCLOSE)
            {
                request_close();
            }

            InvalidateRect(window_handle, nullptr, FALSE);
            return 0;
        }
        break;

    case WM_NCMOUSEMOVE:
        if (m_custom_chrome_enabled)
        {
        UI::Chrome::WindowControl control = UI::Chrome::WindowControl::NoControl;
            if (w_param == HTMINBUTTON)
            {
                control = UI::Chrome::WindowControl::Minimize;
            }
            else if (w_param == HTMAXBUTTON)
            {
                control = UI::Chrome::WindowControl::MaximizeRestore;
            }
            else if (w_param == HTCLOSE)
            {
                control = UI::Chrome::WindowControl::Close;
            }
            update_hovered_control(control);

            TRACKMOUSEEVENT tracking_data{
                .cbSize = sizeof(TRACKMOUSEEVENT),
                .dwFlags = TME_LEAVE | TME_NONCLIENT,
                .hwndTrack = window_handle,
                .dwHoverTime = HOVER_DEFAULT,
            };
            TrackMouseEvent(&tracking_data);
        }
        break;

    case WM_NCMOUSELEAVE:
        update_hovered_control(UI::Chrome::WindowControl::NoControl);
        m_pressed_control = UI::Chrome::WindowControl::NoControl;
        return 0;

    case WM_MOUSEMOVE:
        if (m_custom_chrome_enabled)
        {
            const float point_x = static_cast<float>(GET_X_LPARAM(l_param));
            const float point_y = static_cast<float>(GET_Y_LPARAM(l_param));
            RECT client_bounds{};
            GetClientRect(window_handle, &client_bounds);
            if (m_workspace_pointer_captured)
            {
                HDC device_context = GetDC(window_handle);
                const bool changed = device_context != nullptr &&
                    m_workspace_renderer.handle_pointer_drag(
                        device_context,
                        point_x,
                        point_y,
                        client_bounds.right - client_bounds.left,
                        client_bounds.bottom - client_bounds.top,
                        m_chrome_layout.titlebar_bounds.bottom());
                if (device_context != nullptr)
                {
                    ReleaseDC(window_handle, device_context);
                }
                if (changed)
                {
                    InvalidateRect(window_handle, nullptr, FALSE);
                }
                return 0;
            }
            const std::optional<std::size_t> menu_index = m_menu_overlay_open
                ? get_menu_overlay_index(point_x, point_y)
                : std::nullopt;
            const std::optional<std::size_t> popup_item_index = m_open_menu_index
                ? get_popup_menu_item_index(point_x, point_y)
                : std::nullopt;
            const bool overflow_menu_hovered = m_chrome_layout.is_overflow_menu(point_x, point_y);
            const bool terminal_hover_changed = m_workspace_renderer.handle_pointer_move(
                point_x,
                point_y,
                client_bounds.right - client_bounds.left,
                client_bounds.bottom - client_bounds.top,
                m_chrome_layout.titlebar_bounds.bottom());
            if (menu_index != m_hovered_menu_index ||
                popup_item_index != m_hovered_popup_item_index ||
                overflow_menu_hovered != m_overflow_menu_hovered ||
                terminal_hover_changed)
            {
                m_hovered_menu_index = menu_index;
                m_hovered_popup_item_index = popup_item_index;
                m_overflow_menu_hovered = overflow_menu_hovered;
                InvalidateRect(window_handle, nullptr, FALSE);
            }

            TRACKMOUSEEVENT tracking_data{
                .cbSize = sizeof(TRACKMOUSEEVENT),
                .dwFlags = TME_LEAVE,
                .hwndTrack = window_handle,
                .dwHoverTime = HOVER_DEFAULT,
            };
            TrackMouseEvent(&tracking_data);
        }
        return 0;

    case WM_MOUSEWHEEL:
        if (m_custom_chrome_enabled)
        {
            POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
            ScreenToClient(window_handle, &point);
            RECT client_bounds{};
            GetClientRect(window_handle, &client_bounds);
            const float point_x = static_cast<float>(point.x);
            const float point_y = static_cast<float>(point.y);
            const int client_width = client_bounds.right - client_bounds.left;
            const int client_height = client_bounds.bottom - client_bounds.top;
            const float content_top = m_chrome_layout.titlebar_bounds.bottom();
            const bool over_editor = m_workspace_renderer.is_editor_point(
                point_x, point_y, client_width, client_height, content_top) ||
                m_workspace_renderer.is_minimap_point(
                    point_x, point_y, client_width, client_height, content_top) ||
                m_workspace_renderer.is_scrollbar_point(
                    point_x, point_y, client_width, client_height, content_top);
            const bool over_terminal = m_workspace_renderer.is_terminal_point(
                point_x, point_y, client_width, client_height, content_top);
            const bool over_tool_sidebar = m_workspace_renderer.is_tool_sidebar_point(
                point_x, point_y, client_width, client_height, content_top);
            const short wheel_delta = GET_WHEEL_DELTA_WPARAM(w_param);
            const std::ptrdiff_t line_delta = wheel_delta == 0
                ? 0
                : (wheel_delta > 0 ? -3 : 3);
            if (over_tool_sidebar && line_delta != 0 &&
                m_workspace_renderer.handle_tool_sidebar_scroll(
                    line_delta, client_width, client_height, content_top))
            {
                InvalidateRect(window_handle, nullptr, FALSE);
                return 0;
            }
            if (over_terminal && line_delta != 0 &&
                m_workspace_renderer.handle_terminal_scroll(line_delta))
            {
                InvalidateRect(window_handle, nullptr, FALSE);
                return 0;
            }
            if (over_editor && line_delta != 0 && m_workspace_renderer.handle_scroll(
                    line_delta, client_width, client_height, content_top))
            {
                InvalidateRect(window_handle, nullptr, FALSE);
            }
            return 0;
        }
        break;

    case WM_MOUSELEAVE:
        m_hovered_menu_index.reset();
        m_hovered_popup_item_index.reset();
        m_overflow_menu_hovered = false;
        m_command_center_hovered = false;
        {
            RECT client_bounds{};
            GetClientRect(window_handle, &client_bounds);
            static_cast<void>(m_workspace_renderer.handle_pointer_move(
                -10000.0F,
                -10000.0F,
                client_bounds.right - client_bounds.left,
                client_bounds.bottom - client_bounds.top,
                m_chrome_layout.titlebar_bounds.bottom()));
        }
        InvalidateRect(window_handle, nullptr, FALSE);
        return 0;

    case WM_LBUTTONDOWN:
        if (m_custom_chrome_enabled)
        {
            const float point_x = static_cast<float>(GET_X_LPARAM(l_param));
            const float point_y = static_cast<float>(GET_Y_LPARAM(l_param));
            if (m_chrome_layout.is_overflow_menu(point_x, point_y))
            {
                show_overflow_menu();
                return 0;
            }
            if (m_menu_overlay_open)
            {
                const std::optional<std::size_t> menu_index =
                    get_menu_overlay_index(point_x, point_y);
                if (menu_index)
                {
                    show_menu(*menu_index);
                }
                else
                {
                    close_menu_overlay();
                    InvalidateRect(window_handle, nullptr, FALSE);
                }
                return 0;
            }
            if (m_open_menu_index)
            {
                const std::optional<std::size_t> item_index =
                    get_popup_menu_item_index(point_x, point_y);
                if (item_index &&
                    is_popup_menu_item_enabled(*m_open_menu_index, *item_index))
                {
                    execute_menu_item(*m_open_menu_index, *item_index);
                }
                else
                {
                    close_menu_overlay();
                    InvalidateRect(window_handle, nullptr, FALSE);
                }
                return 0;
            }
        }
        if (m_custom_chrome_enabled)
        {
            const float point_x = static_cast<float>(GET_X_LPARAM(l_param));
            const float point_y = static_cast<float>(GET_Y_LPARAM(l_param));
            RECT client_bounds{};
            GetClientRect(window_handle, &client_bounds);
            const bool editor_point = m_workspace_renderer.is_editor_point(
                point_x,
                point_y,
                client_bounds.right - client_bounds.left,
                client_bounds.bottom - client_bounds.top,
                m_chrome_layout.titlebar_bounds.bottom());
            const bool scrollbar_point = m_workspace_renderer.is_scrollbar_point(
                point_x,
                point_y,
                client_bounds.right - client_bounds.left,
                client_bounds.bottom - client_bounds.top,
                m_chrome_layout.titlebar_bounds.bottom());
            const bool minimap_point = m_workspace_renderer.is_minimap_point(
                point_x,
                point_y,
                client_bounds.right - client_bounds.left,
                client_bounds.bottom - client_bounds.top,
                m_chrome_layout.titlebar_bounds.bottom());
            HDC device_context = GetDC(window_handle);
            const bool handled = device_context != nullptr &&
                m_workspace_renderer.handle_pointer_press(
                    device_context,
                    point_x,
                    point_y,
                    client_bounds.right - client_bounds.left,
                    client_bounds.bottom - client_bounds.top,
                    m_chrome_layout.titlebar_bounds.bottom(),
                    (w_param & MK_SHIFT) != 0);
            if (device_context != nullptr)
            {
                ReleaseDC(window_handle, device_context);
            }
            if (handled)
            {
                if (editor_point || scrollbar_point || minimap_point ||
                    m_workspace_renderer.is_terminal_resizing())
                {
                    m_workspace_pointer_captured = true;
                    SetCapture(window_handle);
                }
                SetFocus(window_handle);
                InvalidateRect(window_handle, nullptr, FALSE);
                return 0;
            }
        }
        break;

    case WM_LBUTTONDBLCLK:
        if (m_custom_chrome_enabled)
        {
            const float point_x = static_cast<float>(GET_X_LPARAM(l_param));
            const float point_y = static_cast<float>(GET_Y_LPARAM(l_param));
            RECT client_bounds{};
            GetClientRect(window_handle, &client_bounds);
            if (m_workspace_renderer.handle_double_click(
                    point_x,
                    point_y,
                    client_bounds.right - client_bounds.left,
                    client_bounds.bottom - client_bounds.top,
                    m_chrome_layout.titlebar_bounds.bottom()))
            {
                m_workspace_pointer_captured = false;
                static_cast<void>(m_workspace_renderer.handle_pointer_release());
                if (GetCapture() == window_handle)
                {
                    ReleaseCapture();
                }
                InvalidateRect(window_handle, nullptr, FALSE);
                return 0;
            }
        }
        break;

    case WM_LBUTTONUP:
        if (m_custom_chrome_enabled && m_workspace_pointer_captured)
        {
            m_workspace_pointer_captured = false;
            static_cast<void>(m_workspace_renderer.handle_pointer_release());
            if (GetCapture() == window_handle)
            {
                ReleaseCapture();
            }
            InvalidateRect(window_handle, nullptr, FALSE);
            return 0;
        }
        if (m_custom_chrome_enabled && m_menu_pointer_tracking)
        {
            m_menu_pointer_tracking = false;
            if (GetCapture() == window_handle)
            {
                ReleaseCapture();
            }
            const float point_x = static_cast<float>(GET_X_LPARAM(l_param));
            const float point_y = static_cast<float>(GET_Y_LPARAM(l_param));
            const std::optional<std::size_t> menu_index = m_chrome_layout.get_menu_index(point_x, point_y);
            if (menu_index)
            {
                show_menu(*menu_index);
                return 0;
            }
            if (m_chrome_layout.is_overflow_menu(point_x, point_y))
            {
                show_overflow_menu();
                return 0;
            }

            m_hovered_menu_index.reset();
            m_overflow_menu_hovered = false;
            InvalidateRect(window_handle, nullptr, FALSE);
            return 0;
        }
        break;

    case WM_CANCELMODE:
    case WM_CAPTURECHANGED:
        if (m_workspace_pointer_captured)
        {
            m_workspace_pointer_captured = false;
            static_cast<void>(m_workspace_renderer.handle_pointer_release());
        }
        if (m_menu_pointer_tracking)
        {
            m_menu_pointer_tracking = false;
            m_hovered_menu_index.reset();
            m_overflow_menu_hovered = false;
            InvalidateRect(window_handle, nullptr, FALSE);
        }
        break;

    case WM_SETCURSOR:
        if (m_custom_chrome_enabled && LOWORD(l_param) == HTCLIENT)
        {
            POINT cursor_position{};
            GetCursorPos(&cursor_position);
            ScreenToClient(window_handle, &cursor_position);
            const bool interactive = m_chrome_layout.get_menu_index(
                                         static_cast<float>(cursor_position.x),
                                         static_cast<float>(cursor_position.y))
                                         .has_value() ||
                m_chrome_layout.is_overflow_menu(
                    static_cast<float>(cursor_position.x),
                    static_cast<float>(cursor_position.y)) ||
                m_chrome_layout.command_center_bounds.contains(
                    static_cast<float>(cursor_position.x),
                    static_cast<float>(cursor_position.y));
            if (interactive)
            {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            RECT client_bounds{};
            GetClientRect(window_handle, &client_bounds);
            if (m_workspace_renderer.is_terminal_resize_handle_point(
                    static_cast<float>(cursor_position.x),
                    static_cast<float>(cursor_position.y),
                    client_bounds.right - client_bounds.left,
                    client_bounds.bottom - client_bounds.top,
                    m_chrome_layout.titlebar_bounds.bottom()))
            {
                SetCursor(LoadCursorW(nullptr, IDC_SIZENS));
                return TRUE;
            }
            if (m_workspace_renderer.is_editor_point(
                    static_cast<float>(cursor_position.x),
                    static_cast<float>(cursor_position.y),
                    client_bounds.right - client_bounds.left,
                    client_bounds.bottom - client_bounds.top,
                    m_chrome_layout.titlebar_bounds.bottom()) ||
                m_workspace_renderer.is_terminal_point(
                    static_cast<float>(cursor_position.x),
                    static_cast<float>(cursor_position.y),
                    client_bounds.right - client_bounds.left,
                    client_bounds.bottom - client_bounds.top,
                    m_chrome_layout.titlebar_bounds.bottom()))
            {
                SetCursor(LoadCursorW(nullptr, IDC_IBEAM));
                return TRUE;
            }
        }
        break;

    case WM_KEYDOWN:
        if (m_custom_chrome_enabled &&
            (m_menu_overlay_open || m_open_menu_index) && w_param == VK_ESCAPE)
        {
            close_menu_overlay();
            InvalidateRect(window_handle, nullptr, FALSE);
            return 0;
        }
        if (m_custom_chrome_enabled && m_workspace_renderer.is_terminal_focused())
        {
            std::optional<Terminal::TerminalInputKey> terminal_key;
            switch (w_param)
            {
            case VK_ESCAPE: terminal_key = Terminal::TerminalInputKey::Escape; break;
            case VK_UP: terminal_key = Terminal::TerminalInputKey::ArrowUp; break;
            case VK_DOWN: terminal_key = Terminal::TerminalInputKey::ArrowDown; break;
            case VK_LEFT: terminal_key = Terminal::TerminalInputKey::ArrowLeft; break;
            case VK_RIGHT: terminal_key = Terminal::TerminalInputKey::ArrowRight; break;
            case VK_HOME: terminal_key = Terminal::TerminalInputKey::Home; break;
            case VK_END: terminal_key = Terminal::TerminalInputKey::End; break;
            case VK_DELETE: terminal_key = Terminal::TerminalInputKey::DeleteForward; break;
            default: break;
            }
            if (terminal_key)
            {
                if (m_workspace_renderer.handle_terminal_key(*terminal_key))
                {
                    InvalidateRect(window_handle, nullptr, FALSE);
                }
                return 0;
            }
        }
        if (m_custom_chrome_enabled && m_workspace_renderer.is_editor_focused())
        {
            const bool control_pressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool shift_pressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            std::optional<UI::Editor::EditorAction> action;
            if (control_pressed && shift_pressed && w_param == VK_DELETE)
            {
                action = UI::Editor::EditorAction::RemoveDocument;
            }
            else if (control_pressed)
            {
                switch (w_param)
                {
                case 'N':
                    action = UI::Editor::EditorAction::CreateDocument;
                    break;
                case 'S':
                    action = UI::Editor::EditorAction::SaveDocument;
                    break;
                case 'W':
                    action = UI::Editor::EditorAction::CloseDocument;
                    break;
                case 'A':
                    action = UI::Editor::EditorAction::SelectAll;
                    break;
                case 'C':
                    action = UI::Editor::EditorAction::Copy;
                    break;
                case 'X':
                    action = UI::Editor::EditorAction::Cut;
                    break;
                case 'V':
                    action = UI::Editor::EditorAction::Paste;
                    break;
                default:
                    break;
                }
            }
            if (action)
            {
                if (m_workspace_renderer.handle_editor_action(*action))
                {
                    InvalidateRect(window_handle, nullptr, FALSE);
                }
                return 0;
            }

            std::optional<UI::Editor::EditorInputCommand> editor_command;
            switch (w_param)
            {
            case VK_LEFT:
                editor_command = UI::Editor::EditorInputCommand::MoveLeft;
                break;
            case VK_RIGHT:
                editor_command = UI::Editor::EditorInputCommand::MoveRight;
                break;
            case VK_UP:
                editor_command = UI::Editor::EditorInputCommand::MoveUp;
                break;
            case VK_DOWN:
                editor_command = UI::Editor::EditorInputCommand::MoveDown;
                break;
            case VK_HOME:
                editor_command = UI::Editor::EditorInputCommand::MoveHome;
                break;
            case VK_END:
                editor_command = UI::Editor::EditorInputCommand::MoveEnd;
                break;
            case VK_DELETE:
                editor_command = UI::Editor::EditorInputCommand::DeleteForward;
                break;
            default:
                break;
            }
            if (editor_command)
            {
                if (m_workspace_renderer.handle_editor_input(
                        *editor_command, shift_pressed))
                {
                    InvalidateRect(window_handle, nullptr, FALSE);
                }
                return 0;
            }
        }
        break;

    case WM_CHAR:
        if (m_custom_chrome_enabled && m_workspace_renderer.is_terminal_focused())
        {
            bool changed = false;
            const wchar_t character = static_cast<wchar_t>(w_param);
            if (character == L'\r')
            {
                changed = m_workspace_renderer.handle_terminal_key(
                    Terminal::TerminalInputKey::Enter);
            }
            else if (character == L'\b')
            {
                changed = m_workspace_renderer.handle_terminal_key(
                    Terminal::TerminalInputKey::Backspace);
            }
            else if (character == L'\t')
            {
                changed = m_workspace_renderer.handle_terminal_key(
                    Terminal::TerminalInputKey::Tab);
            }
            else if (character >= 1 && character <= 26)
            {
                changed = m_workspace_renderer.handle_terminal_control(
                    static_cast<char>('A' + character - 1));
            }
            else if (character >= 0x20 && character != 0x7F)
            {
                std::wstring utf16;
                if (character >= 0xD800 && character <= 0xDBFF)
                {
                    m_pending_high_surrogate = character;
                    return 0;
                }
                if (character >= 0xDC00 && character <= 0xDFFF)
                {
                    if (m_pending_high_surrogate == 0)
                    {
                        return 0;
                    }
                    utf16.push_back(m_pending_high_surrogate);
                    m_pending_high_surrogate = 0;
                }
                else
                {
                    m_pending_high_surrogate = 0;
                }
                utf16.push_back(character);
                const auto utf8 = Utility::wide_to_utf8(utf16);
                changed = utf8 && !utf8->empty() &&
                    m_workspace_renderer.handle_text_input(*utf8);
            }
            if (changed)
            {
                InvalidateRect(window_handle, nullptr, FALSE);
            }
            return 0;
        }
        if (m_custom_chrome_enabled && m_workspace_renderer.is_editor_focused())
        {
            bool changed = false;
            const wchar_t character = static_cast<wchar_t>(w_param);
            if (character == L'\r')
            {
                changed = m_workspace_renderer.handle_editor_input(
                    UI::Editor::EditorInputCommand::InsertNewLine, false);
            }
            else if (character == L'\b')
            {
                changed = m_workspace_renderer.handle_editor_input(
                    UI::Editor::EditorInputCommand::DeleteBackward, false);
            }
            else if (character == L'\t')
            {
                changed = m_workspace_renderer.handle_editor_input(
                    UI::Editor::EditorInputCommand::InsertTab, false);
            }
            else if (character >= 0x20 && character != 0x7F)
            {
                std::wstring utf16;
                if (character >= 0xD800 && character <= 0xDBFF)
                {
                    m_pending_high_surrogate = character;
                    return 0;
                }
                if (character >= 0xDC00 && character <= 0xDFFF)
                {
                    if (m_pending_high_surrogate == 0)
                    {
                        return 0;
                    }
                    utf16.push_back(m_pending_high_surrogate);
                    m_pending_high_surrogate = 0;
                }
                else
                {
                    m_pending_high_surrogate = 0;
                }
                utf16.push_back(character);
                const auto utf8 = Utility::wide_to_utf8(utf16);
                changed = utf8 && !utf8->empty() &&
                    m_workspace_renderer.handle_text_input(*utf8);
            }
            if (changed)
            {
                InvalidateRect(window_handle, nullptr, FALSE);
            }
            return 0;
        }
        break;

    case WM_COMMAND:
        if (m_menubar.handle_command(LOWORD(w_param)))
        {
            return 0;
        }
        break;

    case WM_TIMER:
        if (w_param == editor_caret_timer_id &&
            m_workspace_renderer.tick_caret_blink())
        {
            InvalidateRect(window_handle, nullptr, FALSE);
            return 0;
        }
        break;

    case WM_GETMINMAXINFO:
        if (m_custom_chrome_enabled)
        {
            auto* min_max_info = reinterpret_cast<MINMAXINFO*>(l_param);
            const float dpi_scale = static_cast<float>(m_dpi) / 96.0F;
            min_max_info->ptMinTrackSize.x = round_to_int(720.0F * dpi_scale);
            min_max_info->ptMinTrackSize.y = round_to_int(480.0F * dpi_scale);
            return 0;
        }
        break;

    case WM_DPICHANGED:
    {
        m_dpi = HIWORD(w_param);
        const auto* suggested_bounds = reinterpret_cast<const RECT*>(l_param);
        SetWindowPos(
            window_handle,
            nullptr,
            suggested_bounds->left,
            suggested_bounds->top,
            suggested_bounds->right - suggested_bounds->left,
            suggested_bounds->bottom - suggested_bounds->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        refresh_ui_font();
        static_cast<void>(m_workspace_renderer.initialize(m_dpi));
        refresh_chrome_layout();
        return 0;
    }

    case WM_SIZE:
        refresh_chrome_layout();
        if (m_custom_chrome_enabled)
        {
            InvalidateRect(window_handle, nullptr, FALSE);
        }
        return 0;

    case WM_PAINT:
        if (m_custom_chrome_enabled)
        {
            paint_custom_chrome();
            return 0;
        }
        break;

    case WM_ERASEBKGND:
        if (m_custom_chrome_enabled)
        {
            return 1;
        }
        break;

    case WM_NCACTIVATE:
        if (m_custom_chrome_enabled)
        {
            InvalidateRect(window_handle, nullptr, FALSE);
            return TRUE;
        }
        break;

    case WM_CLOSE:
        DestroyWindow(window_handle);
        return 0;

    case WM_DESTROY:
        Components::FileDropTarget::set_enabled(window_handle, false);
        KillTimer(window_handle, editor_caret_timer_id);
        m_should_close = true;
        PostQuitMessage(0);
        return 0;

    case WM_NCDESTROY:
        SetWindowLongPtrW(window_handle, GWLP_USERDATA, 0);
        m_window_handle = nullptr;
        break;

    default:
        break;
    }

    return DefWindowProcW(window_handle, message, w_param, l_param);
}

LRESULT Win32Window::hit_test_non_client(LPARAM l_param)
{
    POINT cursor_position{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
    ScreenToClient(m_window_handle, &cursor_position);

    const LRESULT resize_result = hit_test_resize_border(cursor_position);
    if (resize_result != HTNOWHERE)
    {
        return resize_result;
    }

    const float point_x = static_cast<float>(cursor_position.x);
    const float point_y = static_cast<float>(cursor_position.y);
    switch (m_chrome_layout.get_window_control(point_x, point_y))
    {
    case UI::Chrome::WindowControl::Minimize:
        return HTMINBUTTON;
    case UI::Chrome::WindowControl::MaximizeRestore:
        return HTMAXBUTTON;
    case UI::Chrome::WindowControl::Close:
        return HTCLOSE;
    case UI::Chrome::WindowControl::NoControl:
        break;
    }

    RECT client_bounds{};
    GetClientRect(m_window_handle, &client_bounds);
    if (m_workspace_renderer.is_tab_bar_point(
            point_x,
            point_y,
            client_bounds.right - client_bounds.left,
            client_bounds.bottom - client_bounds.top,
            m_chrome_layout.titlebar_bounds.bottom()))
    {
        return HTCLIENT;
    }

    const bool is_drag_region = m_titlebar_hit_test_callback
        ? m_titlebar_hit_test_callback(point_x, point_y)
        : m_chrome_layout.is_drag_region(point_x, point_y);
    return is_drag_region ? HTCAPTION : HTCLIENT;
}

LRESULT Win32Window::hit_test_resize_border(POINT client_position) const
{
    if (is_maximized())
    {
        return HTNOWHERE;
    }

    RECT client_bounds{};
    GetClientRect(m_window_handle, &client_bounds);
    const int frame_x = GetSystemMetricsForDpi(SM_CXFRAME, m_dpi) +
        GetSystemMetricsForDpi(SM_CXPADDEDBORDER, m_dpi);
    const int frame_y = GetSystemMetricsForDpi(SM_CYFRAME, m_dpi) +
        GetSystemMetricsForDpi(SM_CXPADDEDBORDER, m_dpi);

    const bool on_left = client_position.x < frame_x;
    const bool on_right = client_position.x >= client_bounds.right - frame_x;
    const bool on_top = client_position.y < frame_y;
    const bool on_bottom = client_position.y >= client_bounds.bottom - frame_y;

    if (on_top && on_left)
    {
        return HTTOPLEFT;
    }
    if (on_top && on_right)
    {
        return HTTOPRIGHT;
    }
    if (on_bottom && on_left)
    {
        return HTBOTTOMLEFT;
    }
    if (on_bottom && on_right)
    {
        return HTBOTTOMRIGHT;
    }
    if (on_left)
    {
        return HTLEFT;
    }
    if (on_right)
    {
        return HTRIGHT;
    }
    if (on_top)
    {
        return HTTOP;
    }
    if (on_bottom)
    {
        return HTBOTTOM;
    }
    return HTNOWHERE;
}

void Win32Window::paint_custom_chrome()
{
    PAINTSTRUCT paint_data{};
    HDC window_context = BeginPaint(m_window_handle, &paint_data);

    RECT client_bounds{};
    GetClientRect(m_window_handle, &client_bounds);
    const int client_width = client_bounds.right - client_bounds.left;
    const int client_height = client_bounds.bottom - client_bounds.top;
    if (client_width <= 0 || client_height <= 0)
    {
        EndPaint(m_window_handle, &paint_data);
        return;
    }

    HDC buffer_context = CreateCompatibleDC(window_context);
    HBITMAP buffer_bitmap = CreateCompatibleBitmap(window_context, client_width, client_height);
    HGDIOBJ previous_bitmap = SelectObject(buffer_context, buffer_bitmap);

    fill_rectangle(
        buffer_context,
        UI::Rect{0.0F, 0.0F, static_cast<float>(client_width), static_cast<float>(client_height)},
        m_theme.window_background);
    fill_rectangle(buffer_context, m_chrome_layout.titlebar_bounds, m_theme.titlebar_background);
    fill_rectangle(
        buffer_context,
        UI::Rect{
            0.0F,
            m_chrome_layout.titlebar_bounds.bottom() - 1.0F,
            static_cast<float>(client_width),
            1.0F,
        },
        m_theme.titlebar_border);

    SetBkMode(buffer_context, TRANSPARENT);
    HGDIOBJ previous_font = SelectObject(buffer_context, m_ui_font);

    if (m_chrome_layout.has_overflow_menu())
    {
        if (m_overflow_menu_hovered || m_menu_overlay_open || m_open_menu_index)
        {
            fill_rectangle(buffer_context, m_chrome_layout.overflow_menu_bounds, m_theme.hover);
        }
        HPEN menu_pen = CreatePen(
            PS_SOLID,
            std::max(1, round_to_int(m_chrome_layout.dpi_scale)),
            to_color_ref(m_theme.text_primary));
        HGDIOBJ previous_pen = SelectObject(buffer_context, menu_pen);
        const int center_x = round_to_int(
            m_chrome_layout.overflow_menu_bounds.x +
            m_chrome_layout.overflow_menu_bounds.width * 0.5F);
        const int center_y = round_to_int(
            m_chrome_layout.overflow_menu_bounds.y +
            m_chrome_layout.overflow_menu_bounds.height * 0.5F);
        const int half_width = std::max(round_to_int(6.0F * m_chrome_layout.dpi_scale), 4);
        const int gap = std::max(round_to_int(4.0F * m_chrome_layout.dpi_scale), 3);
        for (int row = -1; row <= 1; ++row)
        {
            MoveToEx(buffer_context, center_x - half_width, center_y + row * gap, nullptr);
            LineTo(buffer_context, center_x + half_width + 1, center_y + row * gap);
        }
        SelectObject(buffer_context, previous_pen);
        DeleteObject(menu_pen);
    }

    const float scale = m_chrome_layout.dpi_scale;
    const float logo_size = 22.0F * scale;
    const UI::Rect logo_mark{
        m_chrome_layout.logo_bounds.x + (m_chrome_layout.logo_bounds.width - logo_size) * 0.5F,
        m_chrome_layout.logo_bounds.y + (m_chrome_layout.logo_bounds.height - logo_size) * 0.5F,
        logo_size,
        logo_size,
    };
    fill_rectangle(buffer_context, logo_mark, m_theme.accent);
    RECT logo_text_bounds = to_native_rect(logo_mark);
    draw_centered_text(buffer_context, L"Z", logo_text_bounds, UI::Theme::Color{255, 255, 255, 255});

    m_workspace_renderer.render(
        buffer_context,
        client_width,
        client_height,
        m_chrome_layout.titlebar_bounds.bottom());

    const int minimize_state = caption_button_state(
        UI::Chrome::WindowControl::Minimize,
        m_hovered_control,
        m_pressed_control);
    const int maximize_state = caption_button_state(
        UI::Chrome::WindowControl::MaximizeRestore,
        m_hovered_control,
        m_pressed_control);
    const int close_state = caption_button_state(
        UI::Chrome::WindowControl::Close,
        m_hovered_control,
        m_pressed_control);

    draw_custom_caption_button(
        buffer_context,
        m_chrome_layout.minimize_bounds,
        UI::Chrome::WindowControl::Minimize,
        minimize_state,
        is_maximized(),
        m_theme,
        scale);
    draw_custom_caption_button(
        buffer_context,
        m_chrome_layout.maximize_bounds,
        UI::Chrome::WindowControl::MaximizeRestore,
        maximize_state,
        is_maximized(),
        m_theme,
        scale);
    draw_custom_caption_button(
        buffer_context,
        m_chrome_layout.close_bounds,
        UI::Chrome::WindowControl::Close,
        close_state,
        is_maximized(),
        m_theme,
        scale);

    draw_menu_overlay(buffer_context);
    
    SelectObject(buffer_context, previous_font);

    BitBlt(window_context, 0, 0, client_width, client_height, buffer_context, 0, 0, SRCCOPY);
    SelectObject(buffer_context, previous_bitmap);
    DeleteObject(buffer_bitmap);
    DeleteDC(buffer_context);
    EndPaint(m_window_handle, &paint_data);
}

void Win32Window::refresh_chrome_layout()
{
    if (m_window_handle == nullptr)
    {
        return;
    }

    RECT client_bounds{};
    GetClientRect(m_window_handle, &client_bounds);
    m_chrome_layout = m_chrome_layout_engine.calculate(
        static_cast<float>(client_bounds.right - client_bounds.left),
        static_cast<float>(m_dpi) / 96.0F);
}

void Win32Window::refresh_ui_font()
{
    if (m_ui_font != nullptr)
    {
        DeleteObject(m_ui_font);
    }

    m_ui_font = CreateFontW(
        -MulDiv(9, static_cast<int>(m_dpi), 72),
        0,
        0,
        0,
        FW_NORMAL,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe UI");
}

void Win32Window::show_menu(std::size_t menu_index)
{
    const std::span<const UI::Chrome::WindowMenu> menus =
        UI::Chrome::get_window_menu_model();
    if (menu_index >= menus.size())
    {
        return;
    }

    m_menu_overlay_open = false;
    m_open_menu_index = menu_index;
    m_hovered_menu_index.reset();
    m_hovered_popup_item_index.reset();
    InvalidateRect(m_window_handle, nullptr, FALSE);
}

void Win32Window::show_overflow_menu()
{
    if (!m_chrome_layout.has_overflow_menu())
    {
        return;
    }

    const bool should_open = !m_menu_overlay_open;
    close_menu_overlay();
    m_menu_overlay_open = should_open;
    InvalidateRect(m_window_handle, nullptr, FALSE);
}

void Win32Window::close_menu_overlay()
{
    m_menu_overlay_open = false;
    m_open_menu_index.reset();
    m_hovered_menu_index.reset();
    m_hovered_popup_item_index.reset();
    m_menu_pointer_tracking = false;
}

void Win32Window::execute_menu_item(std::size_t menu_index, std::size_t item_index)
{
    const std::span<const UI::Chrome::WindowMenu> menus =
        UI::Chrome::get_window_menu_model();
    if (menu_index >= menus.size() || item_index >= menus[menu_index].items.size())
    {
        close_menu_overlay();
        return;
    }

    const std::string_view command_id = menus[menu_index].items[item_index].command_id;
    close_menu_overlay();
    if (!command_id.empty())
    {
        const std::optional<bool> editor_result =
            m_workspace_renderer.handle_editor_command(command_id);
        if (!editor_result && m_command_invoked_callback)
        {
            m_command_invoked_callback(command_id);
        }
    }
    InvalidateRect(m_window_handle, nullptr, FALSE);
}

Win32Window::MenuOverlayGeometry Win32Window::calculate_menu_overlay_geometry() const noexcept
{
    MenuOverlayGeometry geometry;
    if (!m_chrome_layout.has_overflow_menu())
    {
        return geometry;
    }

    const std::span<const UI::Chrome::WindowMenu> menus =
        UI::Chrome::get_window_menu_model();
    const float row_height = 28.0F * m_chrome_layout.dpi_scale;
    float popup_width = 168.0F * m_chrome_layout.dpi_scale;
    for (const UI::Chrome::WindowMenu& menu : menus)
    {
        popup_width = std::max(
            popup_width,
            static_cast<float>(menu.label.size()) * 7.0F * m_chrome_layout.dpi_scale +
                34.0F * m_chrome_layout.dpi_scale);
    }
    geometry.item_count = std::min(menus.size(), geometry.item_bounds.size());
    geometry.bounds = {
        m_chrome_layout.overflow_menu_bounds.x,
        m_chrome_layout.titlebar_bounds.bottom(),
        popup_width,
        row_height * static_cast<float>(geometry.item_count),
    };
    for (std::size_t index = 0; index < geometry.item_count; ++index)
    {
        geometry.item_bounds[index] = {
            geometry.bounds.x,
            geometry.bounds.y + row_height * static_cast<float>(index),
            geometry.bounds.width,
            row_height,
        };
    }
    return geometry;
}

Win32Window::PopupMenuGeometry Win32Window::calculate_popup_menu_geometry(
    std::size_t menu_index) const noexcept
{
    PopupMenuGeometry geometry;
    const std::span<const UI::Chrome::WindowMenu> menus =
        UI::Chrome::get_window_menu_model();
    if (!m_chrome_layout.has_overflow_menu() || menu_index >= menus.size())
    {
        return geometry;
    }

    const UI::Chrome::WindowMenu& menu = menus[menu_index];
    const float row_height = 28.0F * m_chrome_layout.dpi_scale;
    const float separator_height = 9.0F * m_chrome_layout.dpi_scale;
    float popup_width = 220.0F * m_chrome_layout.dpi_scale;
    for (const UI::Chrome::WindowMenuItem& item : menu.items)
    {
        popup_width = std::max(
            popup_width,
            static_cast<float>(item.label.size()) * 7.0F * m_chrome_layout.dpi_scale +
                48.0F * m_chrome_layout.dpi_scale);
    }
    popup_width = std::min(popup_width, 380.0F * m_chrome_layout.dpi_scale);

    geometry.bounds.x = m_chrome_layout.overflow_menu_bounds.x;
    geometry.bounds.y = m_chrome_layout.titlebar_bounds.bottom();
    geometry.bounds.width = popup_width;
    geometry.item_count = std::min(menu.items.size(), geometry.item_bounds.size());
    float current_y = geometry.bounds.y;
    for (std::size_t index = 0; index < geometry.item_count; ++index)
    {
        const float item_height = menu.items[index].separator
            ? separator_height
            : row_height;
        geometry.item_bounds[index] = {
            geometry.bounds.x,
            current_y,
            geometry.bounds.width,
            item_height,
        };
        current_y += item_height;
    }
    geometry.bounds.height = current_y - geometry.bounds.y;
    return geometry;
}

std::optional<std::size_t> Win32Window::get_menu_overlay_index(
    float point_x,
    float point_y) const noexcept
{
    if (!m_menu_overlay_open)
    {
        return std::nullopt;
    }
    const MenuOverlayGeometry geometry = calculate_menu_overlay_geometry();
    for (std::size_t index = 0; index < geometry.item_count; ++index)
    {
        if (geometry.item_bounds[index].contains(point_x, point_y))
        {
            return index;
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> Win32Window::get_popup_menu_item_index(
    float point_x,
    float point_y) const noexcept
{
    if (!m_open_menu_index)
    {
        return std::nullopt;
    }
    const PopupMenuGeometry geometry = calculate_popup_menu_geometry(*m_open_menu_index);
    const std::span<const UI::Chrome::WindowMenu> menus =
        UI::Chrome::get_window_menu_model();
    if (*m_open_menu_index >= menus.size())
    {
        return std::nullopt;
    }
    for (std::size_t index = 0; index < geometry.item_count; ++index)
    {
        if (!menus[*m_open_menu_index].items[index].separator &&
            geometry.item_bounds[index].contains(point_x, point_y))
        {
            return index;
        }
    }
    return std::nullopt;
}

bool Win32Window::is_popup_menu_item_enabled(
    std::size_t menu_index,
    std::size_t item_index) const
{
    const std::span<const UI::Chrome::WindowMenu> menus =
        UI::Chrome::get_window_menu_model();
    if (menu_index >= menus.size() || item_index >= menus[menu_index].items.size())
    {
        return false;
    }
    const UI::Chrome::WindowMenuItem& item = menus[menu_index].items[item_index];
    if (item.separator || item.command_id.empty())
    {
        return false;
    }
    if (const std::optional<bool> editor_enabled =
            m_workspace_renderer.is_editor_command_enabled(item.command_id))
    {
        return *editor_enabled;
    }
    return !m_command_state_query_callback ||
        m_command_state_query_callback(item.command_id).enabled;
}

void Win32Window::draw_menu_overlay(HDC device_context) const
{
    const bool drawing_root = m_menu_overlay_open;
    if (!drawing_root && !m_open_menu_index)
    {
        return;
    }

    const float scale = m_chrome_layout.dpi_scale;
    const UI::Rect overlay_bounds = drawing_root
        ? calculate_menu_overlay_geometry().bounds
        : calculate_popup_menu_geometry(*m_open_menu_index).bounds;
    if (overlay_bounds.is_empty())
    {
        return;
    }

    RECT native_bounds = to_native_rect(overlay_bounds);
    HBRUSH background_brush = CreateSolidBrush(to_color_ref(m_theme.panel_background));
    HPEN border_pen = CreatePen(PS_SOLID, 1, to_color_ref(m_theme.titlebar_border));
    HGDIOBJ previous_brush = SelectObject(device_context, background_brush);
    HGDIOBJ previous_pen = SelectObject(device_context, border_pen);
    const int radius = std::max(round_to_int(6.0F * scale), 4);
    RoundRect(
        device_context,
        native_bounds.left,
        native_bounds.top,
        native_bounds.right,
        native_bounds.bottom,
        radius,
        radius);
    SelectObject(device_context, previous_pen);
    SelectObject(device_context, previous_brush);
    DeleteObject(border_pen);
    DeleteObject(background_brush);

    const std::span<const UI::Chrome::WindowMenu> menus =
        UI::Chrome::get_window_menu_model();
    if (drawing_root)
    {
        const MenuOverlayGeometry geometry = calculate_menu_overlay_geometry();
        for (std::size_t index = 0; index < geometry.item_count; ++index)
        {
            const bool hovered = m_hovered_menu_index == index;
            UI::Rect item_bounds = geometry.item_bounds[index];
            if (hovered)
            {
                UI::Rect hover_bounds = item_bounds;
                hover_bounds.x += 4.0F * scale;
                hover_bounds.width -= 8.0F * scale;
                hover_bounds.y += 2.0F * scale;
                hover_bounds.height -= 4.0F * scale;
                fill_rectangle(device_context, hover_bounds, m_theme.accent);
            }
            RECT text_bounds = to_native_rect(item_bounds);
            text_bounds.left += round_to_int(12.0F * scale);
            SetTextColor(
                device_context,
                to_color_ref(hovered
                    ? UI::Theme::Color{255, 255, 255, 255}
                    : m_theme.text_primary));
            DrawTextW(
                device_context,
                utf8_to_wide(menus[index].label).c_str(),
                -1,
                &text_bounds,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        }
        return;
    }

    const std::size_t menu_index = *m_open_menu_index;
    if (menu_index >= menus.size())
    {
        return;
    }
    const PopupMenuGeometry geometry = calculate_popup_menu_geometry(menu_index);
    const UI::Chrome::WindowMenu& menu = menus[menu_index];
    for (std::size_t index = 0; index < geometry.item_count; ++index)
    {
        const UI::Chrome::WindowMenuItem& item = menu.items[index];
        const UI::Rect& item_bounds = geometry.item_bounds[index];
        if (item.separator)
        {
            fill_rectangle(
                device_context,
                UI::Rect{
                    item_bounds.x + 8.0F * scale,
                    item_bounds.y + item_bounds.height * 0.5F,
                    item_bounds.width - 16.0F * scale,
                    1.0F,
                },
                m_theme.titlebar_border);
            continue;
        }

        const bool enabled = is_popup_menu_item_enabled(menu_index, index);
        const bool hovered = enabled && m_hovered_popup_item_index == index;
        if (hovered)
        {
            UI::Rect hover_bounds = item_bounds;
            hover_bounds.x += 4.0F * scale;
            hover_bounds.width -= 8.0F * scale;
            hover_bounds.y += 2.0F * scale;
            hover_bounds.height -= 4.0F * scale;
            fill_rectangle(device_context, hover_bounds, m_theme.accent);
        }

        RECT text_bounds = to_native_rect(item_bounds);
        text_bounds.left += round_to_int(26.0F * scale);
        SetTextColor(
            device_context,
            to_color_ref(!enabled
                ? m_theme.text_secondary
                : hovered
                    ? UI::Theme::Color{255, 255, 255, 255}
                    : m_theme.text_primary));
        DrawTextW(
            device_context,
            utf8_to_wide(item.label).c_str(),
            -1,
            &text_bounds,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    }
}

void Win32Window::update_hovered_control(UI::Chrome::WindowControl control)
{
    if (m_hovered_control != control)
    {
        m_hovered_control = control;
        InvalidateRect(m_window_handle, nullptr, FALSE);
    }
}

std::wstring Win32Window::utf8_to_wide(std::string_view text)
{
    return Utility::utf8_to_wide(text).value_or(std::wstring{});
}

} // namespace Zenvra::Platform::Win32
