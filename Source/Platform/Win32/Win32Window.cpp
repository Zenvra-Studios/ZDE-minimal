#include "Platform/Win32/Win32Window.h"
#include "Platform/Win32/WinRT/WinRTContext.h"
#include "Commands/CommandIds.h"
#include "Config/resource.h"
#include "Language/LanguageServerManager.h"
#include "Platform/HostSystem.h"
#include "Platform/PlatformDialogs.h"
#include "Platform/Win32/Components/FileDropTarget.h"
#include "Platform/Win32/Event/ScrollEvent.h"
#include "UI/Components/MenuModel.h"
#include "Utility/Antialiasing.h"
#include "Utility/MathUtil.h"
#include "Utility/Shadows.h"
#include "Utility/TextEncoding.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <vssym32.h>
#include <windowsx.h>
#pragma comment(lib, "Msimg32.lib")
#pragma comment(lib, "comctl32.lib")

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <utility>
#include <vector>

namespace Zenvra::Platform::Win32 {

namespace {

constexpr DWORD dwm_immersive_dark_mode_attribute = 20;
constexpr UINT_PTR editor_caret_timer_id = 1;

enum class PreferredAppMode {
  Default = 0,
  AllowDark = 1,
  ForceDark = 2,
  ForceLight = 3,
  Max = 4
};

using fnSetPreferredAppMode = PreferredAppMode(WINAPI*)(PreferredAppMode mode);
using fnAllowDarkModeForWindow = bool(WINAPI*)(HWND hwnd, bool allow);
using fnFlushMenuThemes = void(WINAPI*)();
using fnSetWindowTheme = HRESULT(WINAPI*)(HWND hwnd, LPCWSTR pszSubAppName, LPCWSTR pszSubIdList);

void enable_menu_dark_mode(HWND hwnd) {
  HMODULE uxtheme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (!uxtheme) {
    uxtheme = GetModuleHandleW(L"uxtheme.dll");
  }
  if (uxtheme) {
    auto set_preferred_app_mode = reinterpret_cast<fnSetPreferredAppMode>(
        GetProcAddress(uxtheme, MAKEINTRESOURCEA(135)));
    if (set_preferred_app_mode) {
      set_preferred_app_mode(PreferredAppMode::ForceDark);
    }
    auto allow_dark_mode_for_window = reinterpret_cast<fnAllowDarkModeForWindow>(
        GetProcAddress(uxtheme, MAKEINTRESOURCEA(133)));
    if (allow_dark_mode_for_window) {
      allow_dark_mode_for_window(hwnd, true);
    }
    auto flush_menu_themes = reinterpret_cast<fnFlushMenuThemes>(
        GetProcAddress(uxtheme, MAKEINTRESOURCEA(136)));
    if (flush_menu_themes) {
      flush_menu_themes();
    }
    auto set_window_theme = reinterpret_cast<fnSetWindowTheme>(
        GetProcAddress(uxtheme, "SetWindowTheme"));
    if (set_window_theme) {
      set_window_theme(hwnd, L"DarkMode_Explorer", nullptr);
    }
  }
}

COLORREF to_color_ref(const UI::Theme::Color &color) {
  return RGB(color.red, color.green, color.blue);
}

using Zenvra::Utility::round_to_int;

RECT to_native_rect(const UI::Rect &rectangle) {
  return RECT{
      round_to_int(rectangle.x),
      round_to_int(rectangle.y),
      round_to_int(rectangle.right()),
      round_to_int(rectangle.bottom()),
  };
}

void fill_rectangle(HDC device_context, const UI::Rect &rectangle,
                    const UI::Theme::Color &color) {
  RECT native_rectangle = to_native_rect(rectangle);
  SetDCBrushColor(device_context, to_color_ref(color));
  FillRect(device_context, &native_rectangle,
           static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}

void fill_rounded_rectangle(HDC device_context, const UI::Rect &rectangle,
                            const UI::Theme::Color &color, int radius) {
  if (rectangle.is_empty()) {
    return;
  }

  int w = round_to_int(rectangle.width);
  int h = round_to_int(rectangle.height);
  if (w <= 0 || h <= 0)
    return;

  float r = std::min({static_cast<float>(radius), rectangle.width * 0.5f,
                      rectangle.height * 0.5f});
  if (r <= 0.0f) {
    RECT native_rectangle = to_native_rect(rectangle);
    SetDCBrushColor(device_context, to_color_ref(color));
    FillRect(device_context, &native_rectangle,
             static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    return;
  }

  BITMAPINFO bmi{};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = w;
  bmi.bmiHeader.biHeight = h;
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

  HDC memDC = CreateCompatibleDC(device_context);
  void *bits = nullptr;
  HBITMAP hBmp =
      CreateDIBSection(device_context, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (hBmp) {
    auto *pixels = static_cast<uint32_t *>(bits);
    const uint32_t col_r = color.red;
    const uint32_t col_g = color.green;
    const uint32_t col_b = color.blue;
    const uint32_t col_a = color.alpha;
    const int ri = static_cast<int>(r);
    const float w_minus_r = static_cast<float>(w) - r;
    const float h_minus_r = static_cast<float>(h) - r;

    // Fill fully opaque interior rows first (memset is very fast)
    std::memset(pixels, 0, static_cast<size_t>(w) * h * sizeof(uint32_t));
    const uint32_t base_pr = (col_r * col_a) / 255;
    const uint32_t base_pg = (col_g * col_a) / 255;
    const uint32_t base_pb = (col_b * col_a) / 255;
    const uint32_t opaque_pixel = (col_a << 24) | (base_pr << 16) | (base_pg << 8) | base_pb;

    // Only compute alpha for corner rows; interior rows are fully opaque
    for (int y = 0; y < h; ++y) {
      const float cy = static_cast<float>(y) + 0.5f;
      const bool in_corner_row = (y < ri) || (y >= h - ri);
      uint32_t *row = &pixels[(h - 1 - y) * w];

      if (!in_corner_row) {
        // Fully opaque row — fill all pixels
        std::fill(row, row + w, opaque_pixel);
        continue;
      }

      const float dy = std::max({r - cy, 0.0f, cy - h_minus_r});
      for (int x = 0; x < w; ++x) {
        const float cx = static_cast<float>(x) + 0.5f;
        const bool in_corner_col = (x < ri) || (x >= w - ri);
        if (!in_corner_col) {
          row[x] = opaque_pixel;
          continue;
        }

        const float dx = std::max({r - cx, 0.0f, cx - w_minus_r});
        const float dist = std::sqrt(dx * dx + dy * dy);
        float alpha_f = std::clamp(0.5f - (dist - r), 0.0f, 1.0f);

        if (alpha_f > 0.0f) {
          uint32_t a = static_cast<uint32_t>(alpha_f * static_cast<float>(col_a));
          uint32_t pr = (col_r * a) / 255;
          uint32_t pg = (col_g * a) / 255;
          uint32_t pb = (col_b * a) / 255;
          row[x] = (a << 24) | (pr << 16) | (pg << 8) | pb;
        }
      }
    }

    HGDIOBJ oldBmp = SelectObject(memDC, hBmp);

    BLENDFUNCTION bf{};
    bf.BlendOp = AC_SRC_OVER;
    bf.BlendFlags = 0;
    bf.SourceConstantAlpha = 255;
    bf.AlphaFormat = AC_SRC_ALPHA;

    AlphaBlend(device_context, round_to_int(rectangle.x),
               round_to_int(rectangle.y), w, h, memDC, 0, 0, w, h, bf);

    SelectObject(memDC, oldBmp);
    DeleteObject(hBmp);
  }
  DeleteDC(memDC);
}


void draw_centered_text(HDC device_context, const wchar_t *text, RECT rectangle,
                        const UI::Theme::Color &color) {
  SetBkMode(device_context, TRANSPARENT);
  SetTextColor(device_context, to_color_ref(color));
  DrawTextW(device_context, text, -1, &rectangle,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX |
                DT_END_ELLIPSIS);
}

int caption_button_state(UI::Chrome::WindowControl control,
                         UI::Chrome::WindowControl hovered_control,
                         UI::Chrome::WindowControl pressed_control) {
  if (pressed_control == control) {
    return MINBS_PUSHED;
  }
  if (hovered_control == control) {
    return MINBS_HOT;
  }
  return MINBS_NORMAL;
}

void draw_custom_caption_button(HDC device_context, const UI::Rect &bounds,
                                UI::Chrome::WindowControl control,
                                int theme_state, bool is_maximized,
                                const UI::Theme::StudioTheme &theme,
                                float scale) {
  if (theme_state == MINBS_HOT || theme_state == MINBS_PUSHED) {
    UI::Theme::Color bg_color =
        theme_state == MINBS_PUSHED ? theme.pressed : theme.hover;
    if (control == UI::Chrome::WindowControl::Close) {
      bg_color =
          theme_state == MINBS_PUSHED ? theme.pressed : theme.close_hover;
    }
    fill_rectangle(device_context, bounds, bg_color);
  }

  UI::Theme::Color icon_color = theme.text_primary;
  if (control == UI::Chrome::WindowControl::Close &&
      (theme_state == MINBS_HOT || theme_state == MINBS_PUSHED)) {
    icon_color = UI::Theme::Color{255, 255, 255, 255};
  }

  HPEN icon_pen = CreatePen(PS_SOLID, std::max(1, round_to_int(scale)),
                            to_color_ref(icon_color));
  HGDIOBJ previous_pen = SelectObject(device_context, icon_pen);
  HGDIOBJ previous_brush =
      SelectObject(device_context, GetStockObject(HOLLOW_BRUSH));

  const int center_x = round_to_int(bounds.x + bounds.width * 0.5F);
  const int center_y = round_to_int(bounds.y + bounds.height * 0.5F);
  const int icon_size = round_to_int(10.0F * scale);
  const int half_size = icon_size / 2;

  if (control == UI::Chrome::WindowControl::Minimize) {
    MoveToEx(device_context, center_x - half_size, center_y, nullptr);
    LineTo(device_context, center_x + half_size + 1, center_y);
  } else if (control == UI::Chrome::WindowControl::MaximizeRestore) {
    if (is_maximized) {
      const int offset = round_to_int(2.0F * scale);

      MoveToEx(device_context, center_x - half_size + offset,
               center_y - half_size, nullptr);
      LineTo(device_context, center_x + half_size + 1, center_y - half_size);
      LineTo(device_context, center_x + half_size + 1,
             center_y + half_size - offset + 1);

      MoveToEx(device_context, center_x - half_size + offset,
               center_y - half_size, nullptr);
      LineTo(device_context, center_x - half_size + offset,
             center_y - half_size + offset);

      Rectangle(device_context, center_x - half_size,
                center_y - half_size + offset,
                center_x + half_size - offset + 1, center_y + half_size + 1);
    } else {
      Rectangle(device_context, center_x - half_size, center_y - half_size,
                center_x + half_size + 1, center_y + half_size + 1);
    }
  } else if (control == UI::Chrome::WindowControl::Close) {
    MoveToEx(device_context, center_x - half_size, center_y - half_size,
             nullptr);
    LineTo(device_context, center_x + half_size + 1, center_y + half_size + 1);

    MoveToEx(device_context, center_x - half_size, center_y + half_size,
             nullptr);
    LineTo(device_context, center_x + half_size + 1, center_y - half_size - 1);
  }

  SelectObject(device_context, previous_brush);
  SelectObject(device_context, previous_pen);
  DeleteObject(icon_pen);
}

} // namespace

Win32Window::Win32Window(const WindowSpecification &specification)
    : m_instance_handle(GetModuleHandleW(nullptr)),
      m_specification(specification),
      m_window_title(utf8_to_wide(specification.title)) {
  m_capabilities.custom_chrome = true;
  m_capabilities.native_titlebar_hit_test = true;
  m_capabilities.native_resize = true;
  m_capabilities.native_snap = true;
  m_capabilities.per_monitor_dpi = true;

  const auto arch = HostSystem::get_native_architecture();
  if (arch == HostSystem::Architecture::Arm64) {
    m_run_config_state.active_architecture = UI::Toolbar::TargetArchitecture::Arm64;
  } else if (arch == HostSystem::Architecture::X86_64) {
    m_run_config_state.active_architecture = UI::Toolbar::TargetArchitecture::X86_64;
  }
  m_run_config_state.active_preset_name = HostSystem::get_system_info().default_preset_debug;
}

Win32Window::~Win32Window() {
  m_tray.destroy();
  Runtime::WinRTContext::shutdown();
  if (m_window_handle != nullptr && IsWindow(m_window_handle) != FALSE) {
    DestroyWindow(m_window_handle);
  }
  if (m_ui_font != nullptr) {
    DeleteObject(m_ui_font);
  }
}

bool Win32Window::initialize() {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

  INITCOMMONCONTROLSEX icex{};
  icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
  icex.dwICC = ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES | ICC_USEREX_CLASSES;
  InitCommonControlsEx(&icex);

  const int icon_big_cx = GetSystemMetrics(SM_CXICON);
  const int icon_big_cy = GetSystemMetrics(SM_CYICON);
  const int icon_sm_cx = GetSystemMetrics(SM_CXSMICON);
  const int icon_sm_cy = GetSystemMetrics(SM_CYSMICON);

  HICON h_icon_big = static_cast<HICON>(LoadImageW(
      m_instance_handle, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
      icon_big_cx > 0 ? icon_big_cx : 32,
      icon_big_cy > 0 ? icon_big_cy : 32,
      LR_DEFAULTCOLOR));
  if (!h_icon_big) {
    h_icon_big = LoadIconW(m_instance_handle, MAKEINTRESOURCEW(IDI_APP_ICON));
  }

  HICON h_icon_sm = static_cast<HICON>(LoadImageW(
      m_instance_handle, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
      icon_sm_cx > 0 ? icon_sm_cx : 16,
      icon_sm_cy > 0 ? icon_sm_cy : 16,
      LR_DEFAULTCOLOR));
  if (!h_icon_sm) {
    h_icon_sm = LoadIconW(m_instance_handle, MAKEINTRESOURCEW(IDI_APP_ICON));
  }

  WNDCLASSEXW window_class{};
  window_class.cbSize = sizeof(window_class);
  window_class.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
  window_class.lpfnWndProc = window_proc;
  window_class.hInstance = m_instance_handle;
  window_class.hIcon = h_icon_big;
  window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  window_class.hbrBackground = nullptr;
  window_class.lpszClassName = window_class_name;
  window_class.hIconSm = h_icon_sm;

  if (RegisterClassExW(&window_class) == 0 &&
      GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    return false;
  }

  m_window_handle = CreateWindowExW(
      0, window_class_name, m_window_title.c_str(), WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, CW_USEDEFAULT, static_cast<int>(m_specification.width),
      static_cast<int>(m_specification.height), nullptr, nullptr,
      m_instance_handle, this);

  if (m_window_handle == nullptr) {
    return false;
  }

  SendMessageW(m_window_handle, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(h_icon_big));
  SendMessageW(m_window_handle, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(h_icon_sm));

  const BOOL dark_mode_enabled = TRUE;
  DwmSetWindowAttribute(m_window_handle, dwm_immersive_dark_mode_attribute,
                        &dark_mode_enabled, sizeof(dark_mode_enabled));
  enable_menu_dark_mode(m_window_handle);

  const MARGINS frame_margins{0, 0, 0, 0};
  DwmExtendFrameIntoClientArea(m_window_handle, &frame_margins);

  if (!m_menubar.load(m_instance_handle) ||
      !m_menubar.attach(m_window_handle)) {
    std::clog << "Warning: the window menu resource could not be loaded.\n";
  }

  m_dpi = GetDpiForWindow(m_window_handle);
  refresh_ui_font();
  if (!m_workspace_renderer.initialize(m_dpi)) {
    std::cerr << "Fatal error: the Win32 workspace renderer could not be "
                 "initialized.\n";
    return false;
  }
  m_workspace_renderer.set_window_handle(m_window_handle);
  m_workspace_renderer.m_text_editor.set_window_handle(m_window_handle);
  Language::LanguageServerManager::instance().set_diagnostics_callback(
      [this](const std::string& uri, const std::vector<Language::Protocol::Diagnostic>& diags) {
          m_workspace_renderer.m_text_editor.on_diagnostics_updated(uri, diags);
      });
  Components::FileDropTarget::set_enabled(m_window_handle, true);
  static_cast<void>(
      SetTimer(m_window_handle, editor_caret_timer_id, 16, nullptr));
  refresh_chrome_layout();
  set_custom_chrome_enabled(m_specification.custom_chrome_enabled);
  apply_system_corner_preference();

  Runtime::WinRTContext::initialize();
  m_tray.create(m_window_handle, WM_TRAYICON, L"ZDE - Zenvra Development Environment");

  return true;
}

void Win32Window::show() {
  if (m_window_handle == nullptr) {
    return;
  }

  apply_system_corner_preference();
  ShowWindow(m_window_handle, SW_SHOWDEFAULT);
  apply_system_corner_preference();

  refresh_chrome_layout();
  InvalidateRect(m_window_handle, nullptr, TRUE);
  UpdateWindow(m_window_handle);
  SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
}

void Win32Window::poll_events() {
  MSG message{};
  while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != FALSE) {
    if (message.message == WM_QUIT) {
      m_should_close = true;
      continue;
    }

    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  if (!is_minimized() && m_workspace_renderer.tick_animations()) {
    InvalidateRect(m_window_handle, nullptr, FALSE);
    UpdateWindow(m_window_handle);
  }
}

bool Win32Window::should_close() const { return m_should_close; }

void Win32Window::minimize() {
  ShowWindow(m_window_handle, SW_MINIMIZE);
  SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
}

void Win32Window::maximize() {
  ShowWindow(m_window_handle, SW_MAXIMIZE);
  apply_system_corner_preference();
  refresh_chrome_layout();
  InvalidateRect(m_window_handle, nullptr, TRUE);
  UpdateWindow(m_window_handle);
}

void Win32Window::restore() {
  ShowWindow(m_window_handle, SW_RESTORE);
  apply_system_corner_preference();
  refresh_chrome_layout();
  InvalidateRect(m_window_handle, nullptr, TRUE);
  UpdateWindow(m_window_handle);
}

void Win32Window::minimize_to_tray() {
  if (m_window_handle != nullptr) {
    ShowWindow(m_window_handle, SW_HIDE);
    m_tray.show_notification(L"ZDE", L"ZDE is running in the background. Click the tray icon to restore.");
  }
}

void Win32Window::restore_from_tray() {
  if (m_window_handle != nullptr) {
    ShowWindow(m_window_handle, SW_SHOW);
    if (IsIconic(m_window_handle)) {
      ShowWindow(m_window_handle, SW_RESTORE);
    }
    apply_system_corner_preference();
    refresh_chrome_layout();
    SetForegroundWindow(m_window_handle);
    SetFocus(m_window_handle);
    InvalidateRect(m_window_handle, nullptr, TRUE);
    UpdateWindow(m_window_handle);
  }
}

void Win32Window::request_close() {
  if (m_window_handle != nullptr) {
    PostMessageW(m_window_handle, WM_CLOSE, 0, 0);
  }
}

void Win32Window::toggle_fullscreen() {
  if (m_window_handle == nullptr) return;
  const DWORD style = GetWindowLongW(m_window_handle, GWL_STYLE);
  if (!m_is_fullscreen) {
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    m_saved_placement.length = sizeof(m_saved_placement);
    if (GetWindowPlacement(m_window_handle, &m_saved_placement) &&
        GetMonitorInfoW(MonitorFromWindow(m_window_handle, MONITOR_DEFAULTTONEAREST), &mi)) {
      m_is_fullscreen = true;
      SetWindowLongW(m_window_handle, GWL_STYLE, (style & ~WS_OVERLAPPEDWINDOW) | WS_POPUP);
      SetWindowPos(m_window_handle, HWND_TOP,
                   mi.rcMonitor.left, mi.rcMonitor.top,
                   mi.rcMonitor.right - mi.rcMonitor.left,
                   mi.rcMonitor.bottom - mi.rcMonitor.top,
                   SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    }
  } else {
    m_is_fullscreen = false;
    SetWindowLongW(m_window_handle, GWL_STYLE, (style & ~WS_POPUP) | WS_OVERLAPPEDWINDOW);
    SetWindowPlacement(m_window_handle, &m_saved_placement);
    SetWindowPos(m_window_handle, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                 SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
  }
  apply_system_corner_preference();
  refresh_chrome_layout();
  InvalidateRect(m_window_handle, nullptr, TRUE);
  UpdateWindow(m_window_handle);
}

void Win32Window::reset_layout() {
  m_workspace_renderer.reset_layout();
  InvalidateRect(m_window_handle, nullptr, FALSE);
}

bool Win32Window::is_maximized() const {
  return m_window_handle != nullptr && IsZoomed(m_window_handle) != FALSE;
}

bool Win32Window::is_minimized() const {
  return m_window_handle != nullptr && IsIconic(m_window_handle) != FALSE;
}

bool Win32Window::is_focused() const {
  return m_window_handle != nullptr && GetForegroundWindow() == m_window_handle;
}

const WindowCapabilities &Win32Window::get_capabilities() const noexcept {
  return m_capabilities;
}

void *Win32Window::get_native_handle() const noexcept {
  return m_window_handle;
}

void Win32Window::set_custom_chrome_enabled(bool enabled) {
  m_custom_chrome_enabled = enabled && m_capabilities.custom_chrome;
  if (m_window_handle == nullptr) {
    return;
  }

  if (m_custom_chrome_enabled) {
    static_cast<void>(m_menubar.detach());
    const MARGINS frame_margins{0, 0, 0, 0};
    DwmExtendFrameIntoClientArea(m_window_handle, &frame_margins);
    apply_system_corner_preference();
  } else {
    close_menu_overlay();
    static_cast<void>(m_menubar.attach(m_window_handle));
    const MARGINS frame_margins{0, 0, 0, 0};
    DwmExtendFrameIntoClientArea(m_window_handle, &frame_margins);
  }

  SetWindowPos(m_window_handle, nullptr, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                   SWP_FRAMECHANGED);
  apply_system_corner_preference();
  refresh_chrome_layout();
  InvalidateRect(m_window_handle, nullptr, FALSE);
}

void Win32Window::set_titlebar_hit_test_callback(
    TitlebarHitTestCallback callback) {
  m_titlebar_hit_test_callback = std::move(callback);
}

void Win32Window::set_command_invoked_callback(
    CommandInvokedCallback callback) {
  m_command_invoked_callback = std::move(callback);
  m_menubar.set_command_invoked_callback([this](std::string_view command_id) {
    const std::optional<bool> editor_result =
        m_workspace_renderer.handle_editor_command(command_id);
    if (editor_result) {
      if (*editor_result && m_window_handle != nullptr) {
        InvalidateRect(m_window_handle, nullptr, FALSE);
      }
      return;
    }
    if (m_command_invoked_callback) {
      m_command_invoked_callback(command_id);
    }
  });
}

void Win32Window::set_command_state_query_callback(
    CommandStateQueryCallback callback) {
  m_command_state_query_callback = std::move(callback);
  m_menubar.set_command_state_query_callback(
      [this](std::string_view command_id) {
        const std::optional<bool> editor_enabled =
            m_workspace_renderer.is_editor_command_enabled(command_id);
        if (editor_enabled) {
          return CommandPresentationState{*editor_enabled, false};
        }
        return m_command_state_query_callback
                   ? m_command_state_query_callback(command_id)
                   : CommandPresentationState{true, false};
      });
}

bool Win32Window::open_project_folder() {
  const std::optional<std::filesystem::path> selected =
      Zenvra::Platform::open_folder_dialog();
  if (!selected || selected->empty()) {
    return true;
  }
  if (!m_workspace_renderer.set_workspace_root(*selected)) {
    std::cerr << "Could not open workspace folder: " << selected->string()
              << '\n';
    return true;
  }

  Language::LanguageServerManager::instance().set_workspace_root(*selected);

  std::error_code path_error;
  const std::filesystem::path canonical =
      std::filesystem::weakly_canonical(*selected, path_error);
  const std::filesystem::path display_root = path_error ? *selected : canonical;
  const std::string folder_name = display_root.filename().empty()
                                      ? display_root.string()
                                      : display_root.filename().string();
  m_window_title = utf8_to_wide(folder_name + " - " + m_specification.title);
  if (m_window_handle != nullptr) {
    SetWindowTextW(m_window_handle, m_window_title.c_str());
    InvalidateRect(m_window_handle, nullptr, FALSE);
  }
  return true;
}

bool Win32Window::close_project() {
  static_cast<void>(m_workspace_renderer.close_project());
  Language::LanguageServerManager::instance().set_workspace_root({});
  m_window_title = utf8_to_wide(m_specification.title);
  if (m_window_handle != nullptr) {
    SetWindowTextW(m_window_handle, m_window_title.c_str());
    InvalidateRect(m_window_handle, nullptr, FALSE);
  }
  return true;
}

void Win32Window::toggle_terminal() {
  static_cast<void>(m_workspace_renderer.toggle_terminal());
  refresh_chrome_layout();
  if (m_window_handle != nullptr) {
    InvalidateRect(m_window_handle, nullptr, FALSE);
  }
}

void Win32Window::toggle_shader_sandbox() {
  static_cast<void>(m_workspace_renderer.toggle_shader_sandbox());
  refresh_chrome_layout();
  if (m_window_handle != nullptr) {
    InvalidateRect(m_window_handle, nullptr, FALSE);
  }
}

LRESULT CALLBACK Win32Window::window_proc(HWND window_handle, UINT message,
                                          WPARAM w_param, LPARAM l_param) {
  Win32Window *window = nullptr;

  if (message == WM_NCCREATE) {
    const auto *create_data = reinterpret_cast<const CREATESTRUCTW *>(l_param);
    window = static_cast<Win32Window *>(create_data->lpCreateParams);
    window->m_window_handle = window_handle;
    SetWindowLongPtrW(window_handle, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(window));
  } else {
    window = reinterpret_cast<Win32Window *>(
        GetWindowLongPtrW(window_handle, GWLP_USERDATA));
  }

  if (window != nullptr) {
    return window->handle_message(window_handle, message, w_param, l_param);
  }

  return DefWindowProcW(window_handle, message, w_param, l_param);
}

LRESULT Win32Window::handle_message(HWND window_handle, UINT message,
                                    WPARAM w_param, LPARAM l_param) {
  switch (message) {
  case WM_DROPFILES: {
    const HDROP drop = reinterpret_cast<HDROP>(w_param);
    const std::vector<std::filesystem::path> dropped_paths =
        Components::FileDropTarget::collect_paths(drop);
    if (m_workspace_renderer.open_dropped_paths(dropped_paths) > 0) {
      SetFocus(window_handle);
      InvalidateRect(window_handle, nullptr, FALSE);
    }
    return 0;
  }

  case WM_NCCALCSIZE:
    if (m_custom_chrome_enabled) {
      if (w_param != FALSE && is_maximized()) {
        auto *parameters = reinterpret_cast<NCCALCSIZE_PARAMS *>(l_param);
        MONITORINFO monitor_info{};
        monitor_info.cbSize = sizeof(monitor_info);
        const HMONITOR monitor =
            MonitorFromWindow(window_handle, MONITOR_DEFAULTTONEAREST);
        if (GetMonitorInfoW(monitor, &monitor_info) != FALSE) {
          parameters->rgrc[0] = monitor_info.rcWork;
        }
      }
      return 0;
    }
    break;

  case WM_NCHITTEST:
    if (m_custom_chrome_enabled) {
      return hit_test_non_client(l_param);
    }
    break;

  case WM_NCRBUTTONUP:
    if (m_custom_chrome_enabled) {
      if (w_param == HTCAPTION || w_param == HTSYSMENU) {
        show_system_menu(GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param));
        return 0;
      }
    }
    break;

  case WM_NCLBUTTONDOWN:
    if (m_custom_chrome_enabled) {
      if (w_param == HTSYSMENU) {
        POINT screen_pt{
            static_cast<int>(m_chrome_layout.logo_bounds.x),
            static_cast<int>(m_chrome_layout.titlebar_bounds.bottom())};
        ClientToScreen(window_handle, &screen_pt);
        show_system_menu(screen_pt.x, screen_pt.y);
        return 0;
      }
      if (w_param == HTMINBUTTON) {
        m_pressed_control = UI::Chrome::WindowControl::Minimize;
        InvalidateRect(window_handle, nullptr, FALSE);
        return 0;
      }
      if (w_param == HTMAXBUTTON) {
        m_pressed_control = UI::Chrome::WindowControl::MaximizeRestore;
        InvalidateRect(window_handle, nullptr, FALSE);
        return 0;
      }
      if (w_param == HTCLOSE) {
        m_pressed_control = UI::Chrome::WindowControl::Close;
        InvalidateRect(window_handle, nullptr, FALSE);
        return 0;
      }
    }
    break;

  case WM_NCLBUTTONUP:
    if (m_custom_chrome_enabled &&
        m_pressed_control != UI::Chrome::WindowControl::NoControl) {
      const UI::Chrome::WindowControl pressed_control = m_pressed_control;
      m_pressed_control = UI::Chrome::WindowControl::NoControl;

      if (pressed_control == UI::Chrome::WindowControl::Minimize &&
          w_param == HTMINBUTTON) {
        minimize();
      } else if (pressed_control ==
                     UI::Chrome::WindowControl::MaximizeRestore &&
                 w_param == HTMAXBUTTON) {
        is_maximized() ? restore() : maximize();
      } else if (pressed_control == UI::Chrome::WindowControl::Close &&
                 w_param == HTCLOSE) {
        request_close();
      }

      InvalidateRect(window_handle, nullptr, FALSE);
      return 0;
    }
    break;

  case WM_NCMOUSEMOVE:
    if (m_custom_chrome_enabled) {
      UI::Chrome::WindowControl control = UI::Chrome::WindowControl::NoControl;
      if (w_param == HTMINBUTTON) {
        control = UI::Chrome::WindowControl::Minimize;
      } else if (w_param == HTMAXBUTTON) {
        control = UI::Chrome::WindowControl::MaximizeRestore;
      } else if (w_param == HTCLOSE) {
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
    if (m_custom_chrome_enabled && m_workspace_renderer.is_prompt_modal_visible()) {
      const float point_x = static_cast<float>(GET_X_LPARAM(l_param));
      const float point_y = static_cast<float>(GET_Y_LPARAM(l_param));
      RECT client_bounds{};
      GetClientRect(window_handle, &client_bounds);
      const UI::Rect viewport{0.0F, 0.0F, static_cast<float>(client_bounds.right - client_bounds.left),
                              static_cast<float>(client_bounds.bottom - client_bounds.top)};
      const auto layout = m_workspace_renderer.get_prompt_modal().calculate_layout(viewport, static_cast<float>(m_dpi) / 96.0F);
      if (m_workspace_renderer.get_prompt_modal().handle_pointer_move(point_x, point_y, layout)) {
        InvalidateRect(window_handle, nullptr, FALSE);
      }
      return 0;
    }
    if (m_custom_chrome_enabled && m_about_modal.is_visible()) {
      const float point_x = static_cast<float>(GET_X_LPARAM(l_param));
      const float point_y = static_cast<float>(GET_Y_LPARAM(l_param));
      RECT client_bounds{};
      GetClientRect(window_handle, &client_bounds);
      const UI::Rect viewport{0.0F, 0.0F, static_cast<float>(client_bounds.right - client_bounds.left),
                              static_cast<float>(client_bounds.bottom - client_bounds.top)};
      const auto layout = m_about_modal.calculate_layout(viewport, static_cast<float>(m_dpi) / 96.0F);
      if (m_about_modal.handle_pointer_move(point_x, point_y, layout)) {
        InvalidateRect(window_handle, nullptr, FALSE);
      }
      return 0;
    }
    if (m_custom_chrome_enabled) {
      const float point_x = static_cast<float>(GET_X_LPARAM(l_param));
      const float point_y = static_cast<float>(GET_Y_LPARAM(l_param));
      RECT client_bounds{};
      GetClientRect(window_handle, &client_bounds);
      if (m_workspace_pointer_captured) {
        HDC device_context = GetDC(window_handle);
        const bool changed = device_context != nullptr &&
                             m_workspace_renderer.handle_pointer_drag(
                                 device_context, point_x, point_y,
                                 client_bounds.right - client_bounds.left,
                                 client_bounds.bottom - client_bounds.top,
                                 m_chrome_layout.titlebar_bounds.bottom());
        if (device_context != nullptr) {
          ReleaseDC(window_handle, device_context);
        }
        if (changed) {
          InvalidateRect(window_handle, nullptr, FALSE);
        }
        return 0;
      }
      if (m_explorer_context_menu.visible) {
        std::optional<std::size_t> new_hover;
        for (std::size_t i = 0; i < m_explorer_context_menu.item_bounds.size(); ++i) {
          if (!m_explorer_context_menu.items[i].separator &&
              m_explorer_context_menu.item_bounds[i].contains(point_x, point_y)) {
            new_hover = i;
            break;
          }
        }
        if (new_hover != m_explorer_context_menu.hovered_index) {
          m_explorer_context_menu.hovered_index = new_hover;
          InvalidateRect(window_handle, nullptr, FALSE);
        }
        static_cast<void>(m_workspace_renderer.handle_pointer_move(
            -10000.0F, -10000.0F, client_bounds.right - client_bounds.left,
            client_bounds.bottom - client_bounds.top,
            m_chrome_layout.titlebar_bounds.bottom()));
        return 0;
      }
      const std::optional<std::size_t> root_menu_index =
          m_menu_overlay_open ? get_menu_overlay_index(point_x, point_y)
                              : std::nullopt;
      const std::optional<std::size_t> visible_menu_index =
          m_menu_overlay_open
              ? std::nullopt
              : m_chrome_layout.get_menu_index(point_x, point_y);
      const std::optional<std::size_t> popup_item_index =
          m_open_menu_index ? get_popup_menu_item_index(point_x, point_y)
                            : std::nullopt;
      const bool overflow_menu_hovered =
          m_chrome_layout.is_overflow_menu(point_x, point_y);
      const bool run_button_hovered =
          m_chrome_layout.is_run_button(point_x, point_y);
      const bool debug_button_hovered =
          m_chrome_layout.is_debug_button(point_x, point_y);
      const bool ellipsis_button_hovered =
          m_chrome_layout.is_ellipsis_button(point_x, point_y);
      const bool compiler_button_hovered =
          m_chrome_layout.is_compiler_button(point_x, point_y);
      const bool platform_button_hovered =
          m_chrome_layout.is_platform_button(point_x, point_y);
      const bool binary_button_hovered =
          m_chrome_layout.is_binary_button(point_x, point_y);
      const bool build_button_hovered =
          m_chrome_layout.is_build_button(point_x, point_y);
      const bool gear_button_hovered =
          m_chrome_layout.is_gear_button(point_x, point_y);
      const bool menu_open = m_menu_overlay_open ||
                             m_open_menu_index.has_value() ||
                             m_explorer_context_menu.visible ||
                             m_menu_pointer_tracking;
      const float ws_point_x = menu_open ? -10000.0F : point_x;
      const float ws_point_y = menu_open ? -10000.0F : point_y;
      const bool terminal_hover_changed =
          m_workspace_renderer.handle_pointer_move(
              ws_point_x, ws_point_y, client_bounds.right - client_bounds.left,
              client_bounds.bottom - client_bounds.top,
              m_chrome_layout.titlebar_bounds.bottom());
      std::optional<std::size_t> combined_menu_index = visible_menu_index;
      if (!m_menu_overlay_open && !combined_menu_index) {
        if (compiler_button_hovered)
          combined_menu_index = 10;
        else if (platform_button_hovered)
          combined_menu_index = 11;
        else if (binary_button_hovered)
          combined_menu_index = 12;
        else if (gear_button_hovered)
          combined_menu_index = 13;
        else if (ellipsis_button_hovered)
          combined_menu_index = 14;
      }

      const std::optional<std::size_t> menu_index =
          root_menu_index ? root_menu_index : combined_menu_index;
      bool menu_state_changed = false;

      const bool menu_interaction_active = m_open_menu_index.has_value() ||
                                           m_menu_overlay_open ||
                                           m_menu_pointer_tracking;

      if (m_menu_overlay_open && root_menu_index) {
        if (m_open_menu_index != root_menu_index) {
          m_open_menu_index = root_menu_index;
          m_hovered_popup_item_index.reset();
          menu_state_changed = true;
        }
      } else if (menu_interaction_active && combined_menu_index &&
                 (m_open_menu_index != combined_menu_index ||
                  m_menu_overlay_open)) {
        m_open_menu_index = combined_menu_index;
        m_menu_overlay_open = false;
        m_hovered_popup_item_index.reset();
        menu_state_changed = true;
      } else if (menu_interaction_active && overflow_menu_hovered &&
                 !m_menu_overlay_open) {
        m_open_menu_index.reset();
        m_menu_overlay_open = true;
        m_hovered_popup_item_index.reset();
        menu_state_changed = true;
      }

      if (menu_index != m_hovered_menu_index ||
          popup_item_index != m_hovered_popup_item_index ||
          overflow_menu_hovered != m_overflow_menu_hovered ||
          run_button_hovered != m_run_button_hovered ||
          debug_button_hovered != m_debug_button_hovered ||
          ellipsis_button_hovered != m_ellipsis_button_hovered ||
          compiler_button_hovered != m_compiler_button_hovered ||
          platform_button_hovered != m_platform_button_hovered ||
          binary_button_hovered != m_binary_button_hovered ||
          build_button_hovered != m_build_button_hovered ||
          gear_button_hovered != m_gear_button_hovered ||
          terminal_hover_changed || menu_state_changed) {
        m_hovered_menu_index = menu_index;
        m_hovered_popup_item_index = popup_item_index;
        m_overflow_menu_hovered = overflow_menu_hovered;
        m_run_button_hovered = run_button_hovered;
        m_debug_button_hovered = debug_button_hovered;
        m_ellipsis_button_hovered = ellipsis_button_hovered;
        m_compiler_button_hovered = compiler_button_hovered;
        m_platform_button_hovered = platform_button_hovered;
        m_binary_button_hovered = binary_button_hovered;
        m_build_button_hovered = build_button_hovered;
        m_gear_button_hovered = gear_button_hovered;
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
    if (m_custom_chrome_enabled) {
      POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
      ScreenToClient(window_handle, &point);
      RECT client_bounds{};
      GetClientRect(window_handle, &client_bounds);
      const float point_x = static_cast<float>(point.x);
      const float point_y = static_cast<float>(point.y);
      const int client_width = client_bounds.right - client_bounds.left;
      const int client_height = client_bounds.bottom - client_bounds.top;
      const float content_top = m_chrome_layout.titlebar_bounds.bottom();
      const bool over_editor =
          m_workspace_renderer.is_editor_point(point_x, point_y, client_width,
                                               client_height, content_top) ||
          m_workspace_renderer.is_minimap_point(point_x, point_y, client_width,
                                                client_height, content_top) ||
          m_workspace_renderer.is_scrollbar_point(
              point_x, point_y, client_width, client_height, content_top);
      const bool over_terminal = m_workspace_renderer.is_terminal_point(
          point_x, point_y, client_width, client_height, content_top);
      const bool over_tool_sidebar = m_workspace_renderer.is_tool_sidebar_point(
          point_x, point_y, client_width, client_height, content_top);
      const bool over_tab_bar = m_workspace_renderer.is_tab_bar_point(
          point_x, point_y, client_width, client_height, content_top);

      const short wheel_delta = GET_WHEEL_DELTA_WPARAM(w_param);
      const std::ptrdiff_t steps = (std::abs(wheel_delta) >= WHEEL_DELTA)
          ? (static_cast<std::ptrdiff_t>(wheel_delta) / WHEEL_DELTA) * 3
          : (wheel_delta > 0 ? 1 : (wheel_delta < 0 ? -1 : 0));
      const std::ptrdiff_t line_delta = -steps;
      Event::ScrollEvent scroll_event;
      scroll_event.is_mouse_wheel = true;
      scroll_event.point_x = point_x;
      scroll_event.point_y = point_y;
      if (GET_KEYSTATE_WPARAM(w_param) & MK_SHIFT) {
        scroll_event.delta_x = line_delta;
      } else {
        scroll_event.delta_y = line_delta;
      }

      if (over_tool_sidebar) {
        const std::ptrdiff_t sidebar_delta = (scroll_event.delta_y != 0) ? scroll_event.delta_y : line_delta;
        if (sidebar_delta != 0 &&
            m_workspace_renderer.handle_tool_sidebar_scroll(
                sidebar_delta, client_width, client_height, content_top)) {
          static_cast<void>(m_workspace_renderer.handle_pointer_move(
              point_x, point_y, client_width, client_height, content_top));
          const int ct = round_to_int(content_top);
          RECT content_rect{client_bounds.left, ct, client_bounds.right,
                            client_bounds.bottom};
          InvalidateRect(window_handle, &content_rect, FALSE);
        }
        return 0;
      }
      if (over_terminal &&
          (scroll_event.delta_x != 0 || scroll_event.delta_y != 0) &&
          m_workspace_renderer.handle_terminal_scroll(scroll_event)) {
        const int ct = round_to_int(content_top);
        RECT content_rect{client_bounds.left, ct, client_bounds.right,
                          client_bounds.bottom};
        InvalidateRect(window_handle, &content_rect, FALSE);
        return 0;
      }
      if ((over_editor || over_tab_bar) &&
          (scroll_event.delta_x != 0 || scroll_event.delta_y != 0) &&
          m_workspace_renderer.handle_scroll(scroll_event, client_width,
                                             client_height, content_top)) {
        if (over_tab_bar) {
          InvalidateRect(window_handle, nullptr, FALSE);
        } else {
          const int ct = round_to_int(content_top);
          RECT content_rect{client_bounds.left, ct, client_bounds.right,
                            client_bounds.bottom};
          InvalidateRect(window_handle, &content_rect, FALSE);
        }
      }
      return 0;
    }
    break;

  case WM_MOUSEHWHEEL:
    if (m_custom_chrome_enabled) {
      POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
      ScreenToClient(window_handle, &point);
      RECT client_bounds{};
      GetClientRect(window_handle, &client_bounds);
      const float point_x = static_cast<float>(point.x);
      const float point_y = static_cast<float>(point.y);
      const int client_width = client_bounds.right - client_bounds.left;
      const int client_height = client_bounds.bottom - client_bounds.top;
      const float content_top = m_chrome_layout.titlebar_bounds.bottom();
      const bool over_terminal = m_workspace_renderer.is_terminal_point(
          point_x, point_y, client_width, client_height, content_top);
      const bool over_editor = m_workspace_renderer.is_editor_point(
          point_x, point_y, client_width, client_height, content_top);
      const bool over_tab_bar = m_workspace_renderer.is_tab_bar_point(
          point_x, point_y, client_width, client_height, content_top);

      const short wheel_delta = GET_WHEEL_DELTA_WPARAM(w_param);
      const std::ptrdiff_t line_delta =
          wheel_delta == 0
              ? 0
              : (wheel_delta > 0 ? 3 : -3); // horizontal scroll direction

      Event::ScrollEvent scroll_event;
      scroll_event.is_mouse_wheel = true;
      scroll_event.point_x = point_x;
      scroll_event.point_y = point_y;
      scroll_event.delta_x = line_delta;

      if (over_terminal && scroll_event.delta_x != 0 &&
          m_workspace_renderer.handle_terminal_scroll(scroll_event)) {
        const int ct = round_to_int(content_top);
        RECT content_rect{client_bounds.left, ct, client_bounds.right,
                          client_bounds.bottom};
        InvalidateRect(window_handle, &content_rect, FALSE);
        return 0;
      }
      if ((over_editor || over_tab_bar) && scroll_event.delta_x != 0 &&
          m_workspace_renderer.handle_scroll(scroll_event, client_width,
                                             client_height, content_top)) {
        if (over_tab_bar) {
          InvalidateRect(window_handle, nullptr, FALSE);
        } else {
          const int ct = round_to_int(content_top);
          RECT content_rect{client_bounds.left, ct, client_bounds.right,
                            client_bounds.bottom};
          InvalidateRect(window_handle, &content_rect, FALSE);
        }
        return 0;
      }
      return 0;
    }
    break;

  case WM_MOUSELEAVE:
    m_hovered_menu_index.reset();
    m_hovered_popup_item_index.reset();
    m_overflow_menu_hovered = false;
    m_command_center_hovered = false;
    m_run_button_hovered = false;
    m_debug_button_hovered = false;
    m_ellipsis_button_hovered = false;
    m_compiler_button_hovered = false;
    m_binary_button_hovered = false;
    m_build_button_hovered = false;
    m_gear_button_hovered = false;
    {
      RECT client_bounds{};
      GetClientRect(window_handle, &client_bounds);
      static_cast<void>(m_workspace_renderer.handle_pointer_move(
          -10000.0F, -10000.0F, client_bounds.right - client_bounds.left,
          client_bounds.bottom - client_bounds.top,
          m_chrome_layout.titlebar_bounds.bottom()));
    }
    InvalidateRect(window_handle, nullptr, FALSE);
    return 0;

  case WM_LBUTTONDOWN:
    if (m_custom_chrome_enabled && m_workspace_renderer.is_prompt_modal_visible()) {
      const float point_x = static_cast<float>(GET_X_LPARAM(l_param));
      const float point_y = static_cast<float>(GET_Y_LPARAM(l_param));
      RECT client_bounds{};
      GetClientRect(window_handle, &client_bounds);
      const UI::Rect viewport{0.0F, 0.0F, static_cast<float>(client_bounds.right - client_bounds.left),
                              static_cast<float>(client_bounds.bottom - client_bounds.top)};
      const auto layout = m_workspace_renderer.get_prompt_modal().calculate_layout(viewport, static_cast<float>(m_dpi) / 96.0F);
      static_cast<void>(m_workspace_renderer.get_prompt_modal().handle_pointer_press(point_x, point_y, layout));
      InvalidateRect(window_handle, nullptr, FALSE);
      return 0;
    }
    if (m_custom_chrome_enabled && m_about_modal.is_visible()) {
      const float point_x = static_cast<float>(GET_X_LPARAM(l_param));
      const float point_y = static_cast<float>(GET_Y_LPARAM(l_param));
      RECT client_bounds{};
      GetClientRect(window_handle, &client_bounds);
      const UI::Rect viewport{0.0F, 0.0F, static_cast<float>(client_bounds.right - client_bounds.left),
                              static_cast<float>(client_bounds.bottom - client_bounds.top)};
      const auto layout = m_about_modal.calculate_layout(viewport, static_cast<float>(m_dpi) / 96.0F);
      static_cast<void>(m_about_modal.handle_pointer_press(point_x, point_y, layout));
      InvalidateRect(window_handle, nullptr, FALSE);
      return 0;
    }
    if (m_custom_chrome_enabled && m_explorer_context_menu.visible) {
      const float point_x = static_cast<float>(GET_X_LPARAM(l_param));
      const float point_y = static_cast<float>(GET_Y_LPARAM(l_param));
      for (std::size_t i = 0; i < m_explorer_context_menu.item_bounds.size(); ++i) {
        if (!m_explorer_context_menu.items[i].separator &&
            m_explorer_context_menu.item_bounds[i].contains(point_x, point_y)) {
          execute_explorer_context_menu_item(i);
          close_explorer_context_menu();
          InvalidateRect(window_handle, nullptr, FALSE);
          return 0;
        }
      }
      close_explorer_context_menu();
      InvalidateRect(window_handle, nullptr, FALSE);
      return 0;
    }
    if (m_custom_chrome_enabled) {
      const float point_x = static_cast<float>(GET_X_LPARAM(l_param));
      const float point_y = static_cast<float>(GET_Y_LPARAM(l_param));
      if (m_chrome_layout.is_overflow_menu(point_x, point_y)) {
        show_overflow_menu();
        return 0;
      }
      if (m_chrome_layout.is_run_button(point_x, point_y)) {
        const std::optional<bool> editor_result =
            m_workspace_renderer.handle_editor_command(
                Commands::CommandIds::run_start);
        if (!editor_result && m_command_invoked_callback) {
          m_command_invoked_callback(Commands::CommandIds::run_start);
        }
        InvalidateRect(window_handle, nullptr, FALSE);
        return 0;
      }
      if (m_chrome_layout.is_debug_button(point_x, point_y)) {
        const std::optional<bool> editor_result =
            m_workspace_renderer.handle_editor_command(
                Commands::CommandIds::view_problems);
        if (!editor_result && m_command_invoked_callback) {
          m_command_invoked_callback(Commands::CommandIds::view_problems);
        }
        InvalidateRect(window_handle, nullptr, FALSE);
        return 0;
      }

      static constexpr std::size_t compiler_menu_index = 10;
      static constexpr std::size_t platform_menu_index = 11;
      static constexpr std::size_t binary_menu_index = 12;
      static constexpr std::size_t gear_menu_index = 13;
      static constexpr std::size_t ellipsis_menu_index = 14;

      auto open_or_close_overlay = [&](std::size_t idx) {
        if (m_open_menu_index == idx) {
          close_menu_overlay();
          InvalidateRect(window_handle, nullptr, FALSE);
        } else {
          close_menu_overlay();
          show_menu(idx);
        }
      };

      if (m_chrome_layout.is_compiler_button(point_x, point_y)) {
        open_or_close_overlay(compiler_menu_index);
        return 0;
      }
      if (m_chrome_layout.is_platform_button(point_x, point_y)) {
        open_or_close_overlay(platform_menu_index);
        return 0;
      }
      if (m_chrome_layout.is_binary_button(point_x, point_y)) {
        open_or_close_overlay(binary_menu_index);
        return 0;
      }
      if (m_chrome_layout.is_gear_button(point_x, point_y)) {
        open_or_close_overlay(gear_menu_index);
        return 0;
      }
      if (m_chrome_layout.is_ellipsis_button(point_x, point_y)) {
        open_or_close_overlay(ellipsis_menu_index);
        return 0;
      }
      if (m_open_menu_index) {
        const std::optional<std::size_t> item_index =
            get_popup_menu_item_index(point_x, point_y);
        if (item_index &&
            is_popup_menu_item_enabled(*m_open_menu_index, *item_index)) {
          execute_menu_item(*m_open_menu_index, *item_index);
        } else if (m_menu_overlay_open) {
          const std::optional<std::size_t> menu_index =
              get_menu_overlay_index(point_x, point_y);
          if (menu_index) {
            show_menu(*menu_index);
          } else {
            close_menu_overlay();
            InvalidateRect(window_handle, nullptr, FALSE);
          }
        } else {
          close_menu_overlay();
          InvalidateRect(window_handle, nullptr, FALSE);
        }
        return 0;
      }
      if (m_menu_overlay_open) {
        const std::optional<std::size_t> menu_index =
            get_menu_overlay_index(point_x, point_y);
        if (menu_index) {
          show_menu(*menu_index);
        } else {
          close_menu_overlay();
          InvalidateRect(window_handle, nullptr, FALSE);
        }
        return 0;
      }
    }
    if (m_custom_chrome_enabled) {
      const float point_x = static_cast<float>(GET_X_LPARAM(l_param));
      const float point_y = static_cast<float>(GET_Y_LPARAM(l_param));
      RECT client_bounds{};
      GetClientRect(window_handle, &client_bounds);
      const bool editor_point = m_workspace_renderer.is_editor_point(
          point_x, point_y, client_bounds.right - client_bounds.left,
          client_bounds.bottom - client_bounds.top,
          m_chrome_layout.titlebar_bounds.bottom());
      const bool scrollbar_point = m_workspace_renderer.is_scrollbar_point(
          point_x, point_y, client_bounds.right - client_bounds.left,
          client_bounds.bottom - client_bounds.top,
          m_chrome_layout.titlebar_bounds.bottom());
      const bool minimap_point = m_workspace_renderer.is_minimap_point(
          point_x, point_y, client_bounds.right - client_bounds.left,
          client_bounds.bottom - client_bounds.top,
          m_chrome_layout.titlebar_bounds.bottom());
      const bool tab_bar_point = m_workspace_renderer.is_tab_bar_point(
          point_x, point_y, client_bounds.right - client_bounds.left,
          client_bounds.bottom - client_bounds.top,
          m_chrome_layout.titlebar_bounds.bottom());
      HDC device_context = GetDC(window_handle);
      std::string command_out;
      const bool handled = device_context != nullptr &&
                           m_workspace_renderer.handle_pointer_press(
                               device_context, point_x, point_y,
                               client_bounds.right - client_bounds.left,
                               client_bounds.bottom - client_bounds.top,
                               m_chrome_layout.titlebar_bounds.bottom(),
                               (w_param & MK_SHIFT) != 0, command_out);
      if (device_context != nullptr) {
        ReleaseDC(window_handle, device_context);
      }
      if (handled) {
        if (!command_out.empty()) {
          if (command_out == "zde.project.open") {
            static_cast<void>(open_project_folder());
          }
        }
        if (!m_workspace_renderer.is_prompt_modal_visible() &&
            !m_workspace_renderer.get_add_item_dialog().is_visible() &&
            !m_about_modal.is_visible()) {
          if (editor_point || scrollbar_point || minimap_point || tab_bar_point ||
              m_workspace_renderer.is_editor_split_resizing() ||
              m_workspace_renderer.is_terminal_resizing() ||
              m_workspace_renderer.is_sidebar_resizing() ||
              m_workspace_renderer.is_sidebar_dragging_item() ||
              m_workspace_renderer.is_sidebar_dragging_scrollbar() ||
              m_workspace_renderer.is_shader_sandbox_resizing() ||
              m_workspace_renderer.is_shader_sandbox_point(
                  point_x, point_y, client_bounds.right - client_bounds.left,
                  client_bounds.bottom - client_bounds.top,
                  m_chrome_layout.titlebar_bounds.bottom())) {
            m_workspace_pointer_captured = true;
            SetCapture(window_handle);
          }
          SetFocus(window_handle);
        }
        InvalidateRect(window_handle, nullptr, FALSE);
        return 0;
      }
    }
    break;

  case WM_LBUTTONDBLCLK:
    if (m_custom_chrome_enabled) {
      const float point_x = static_cast<float>(GET_X_LPARAM(l_param));
      const float point_y = static_cast<float>(GET_Y_LPARAM(l_param));
      RECT client_bounds{};
      GetClientRect(window_handle, &client_bounds);
      if (m_workspace_renderer.handle_double_click(
              point_x, point_y, client_bounds.right - client_bounds.left,
              client_bounds.bottom - client_bounds.top,
              m_chrome_layout.titlebar_bounds.bottom())) {
        m_workspace_pointer_captured = false;
        static_cast<void>(m_workspace_renderer.handle_pointer_release());
        if (GetCapture() == window_handle) {
          ReleaseCapture();
        }
        InvalidateRect(window_handle, nullptr, FALSE);
        return 0;
      }
    }
    break;

  case WM_LBUTTONUP:
    if (m_custom_chrome_enabled) {
      m_workspace_pointer_captured = false;
      static_cast<void>(m_workspace_renderer.handle_pointer_release());
      if (GetCapture() == window_handle) {
        ReleaseCapture();
      }
    } else if (GetCapture() == window_handle) {
      ReleaseCapture();
    }
    if (m_custom_chrome_enabled && m_workspace_renderer.is_prompt_modal_visible()) {
      const float point_x = static_cast<float>(GET_X_LPARAM(l_param));
      const float point_y = static_cast<float>(GET_Y_LPARAM(l_param));
      RECT client_bounds{};
      GetClientRect(window_handle, &client_bounds);
      const UI::Rect viewport{0.0F, 0.0F, static_cast<float>(client_bounds.right - client_bounds.left),
                              static_cast<float>(client_bounds.bottom - client_bounds.top)};
      const auto layout = m_workspace_renderer.get_prompt_modal().calculate_layout(viewport, static_cast<float>(m_dpi) / 96.0F);
      static_cast<void>(m_workspace_renderer.get_prompt_modal().handle_pointer_release(point_x, point_y, layout));
      InvalidateRect(window_handle, nullptr, FALSE);
      return 0;
    }
    if (m_custom_chrome_enabled && m_about_modal.is_visible()) {
      const float point_x = static_cast<float>(GET_X_LPARAM(l_param));
      const float point_y = static_cast<float>(GET_Y_LPARAM(l_param));
      RECT client_bounds{};
      GetClientRect(window_handle, &client_bounds);
      const UI::Rect viewport{0.0F, 0.0F, static_cast<float>(client_bounds.right - client_bounds.left),
                              static_cast<float>(client_bounds.bottom - client_bounds.top)};
      const auto layout = m_about_modal.calculate_layout(viewport, static_cast<float>(m_dpi) / 96.0F);
      static_cast<void>(m_about_modal.handle_pointer_release(point_x, point_y, layout, [this](const std::string& text) {
        copy_to_clipboard(text);
      }));
      InvalidateRect(window_handle, nullptr, FALSE);
      return 0;
    }
    if (m_custom_chrome_enabled && m_menu_pointer_tracking) {
      m_menu_pointer_tracking = false;
      if (GetCapture() == window_handle) {
        ReleaseCapture();
      }
      const float point_x = static_cast<float>(GET_X_LPARAM(l_param));
      const float point_y = static_cast<float>(GET_Y_LPARAM(l_param));
      const std::optional<std::size_t> menu_index =
          m_chrome_layout.get_menu_index(point_x, point_y);
      if (menu_index) {
        show_menu(*menu_index);
        return 0;
      }
      if (m_chrome_layout.is_overflow_menu(point_x, point_y)) {
        show_overflow_menu();
        return 0;
      }

      m_hovered_menu_index.reset();
      m_overflow_menu_hovered = false;
      InvalidateRect(window_handle, nullptr, FALSE);
      return 0;
    }
    break;

  case WM_RBUTTONDOWN:
  case WM_RBUTTONUP:
    if (m_custom_chrome_enabled) {
      const float point_x = static_cast<float>(GET_X_LPARAM(l_param));
      const float point_y = static_cast<float>(GET_Y_LPARAM(l_param));
      RECT client_bounds{};
      GetClientRect(window_handle, &client_bounds);
      const int client_width = client_bounds.right - client_bounds.left;
      const int client_height = client_bounds.bottom - client_bounds.top;
      const float content_top = m_chrome_layout.titlebar_bounds.bottom();

      if (m_chrome_layout.titlebar_bounds.contains(point_x, point_y)) {
        if (message == WM_RBUTTONUP) {
          POINT screen_pt{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
          ClientToScreen(window_handle, &screen_pt);
          show_system_menu(screen_pt.x, screen_pt.y);
        }
        return 0;
      }

      if (m_workspace_renderer.is_tool_sidebar_point(point_x, point_y, client_width, client_height, content_top)) {
        if (message == WM_RBUTTONUP) {
          const auto opt_target = m_workspace_renderer.handle_right_click(
              point_x, point_y, client_width, client_height, content_top);
          if (opt_target) {
            show_explorer_context_menu(*opt_target, static_cast<int>(point_x), static_cast<int>(point_y));
          }
        }
        InvalidateRect(window_handle, nullptr, FALSE);
        return 0;
      }
    }
    break;

  case WM_CONTEXTMENU:
    if (m_custom_chrome_enabled) {
      POINT screen_pt{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
      if (screen_pt.x == -1 && screen_pt.y == -1) {
        RECT window_rect{};
        GetWindowRect(window_handle, &window_rect);
        screen_pt.x = window_rect.left + static_cast<int>(m_chrome_layout.logo_bounds.x);
        screen_pt.y = window_rect.top + static_cast<int>(m_chrome_layout.titlebar_bounds.bottom());
        show_system_menu(screen_pt.x, screen_pt.y);
        return 0;
      }
      POINT client_pt = screen_pt;
      ScreenToClient(window_handle, &client_pt);
      if (m_chrome_layout.titlebar_bounds.contains(static_cast<float>(client_pt.x), static_cast<float>(client_pt.y))) {
        show_system_menu(screen_pt.x, screen_pt.y);
        return 0;
      }
    }
    break;

  case WM_CANCELMODE:
  case WM_CAPTURECHANGED:
    if (m_workspace_pointer_captured) {
      m_workspace_pointer_captured = false;
      static_cast<void>(m_workspace_renderer.handle_pointer_release());
    }
    if (m_menu_pointer_tracking) {
      m_menu_pointer_tracking = false;
      m_hovered_menu_index.reset();
      m_overflow_menu_hovered = false;
      InvalidateRect(window_handle, nullptr, FALSE);
    }
    break;

  case WM_SETCURSOR:
    if (m_custom_chrome_enabled && LOWORD(l_param) == HTCLIENT) {
      POINT cursor_position{};
      GetCursorPos(&cursor_position);
      ScreenToClient(window_handle, &cursor_position);
      RECT client_bounds{};
      GetClientRect(window_handle, &client_bounds);
      const float cur_x = static_cast<float>(cursor_position.x);
      const float cur_y = static_cast<float>(cursor_position.y);

      if (m_about_modal.is_visible()) {
        const float scale = static_cast<float>(m_dpi) / 96.0F;
        const UI::Rect viewport{0.0F, 0.0F, static_cast<float>(client_bounds.right - client_bounds.left),
                                static_cast<float>(client_bounds.bottom - client_bounds.top)};
        const auto layout = m_about_modal.calculate_layout(viewport, scale);
        if (layout.is_copy_button(cur_x, cur_y) ||
            layout.is_ok_button(cur_x, cur_y) ||
            layout.is_close_button(cur_x, cur_y)) {
          SetCursor(LoadCursorW(nullptr, IDC_HAND));
          return TRUE;
        }
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        return TRUE;
      }

      if (m_explorer_context_menu.visible) {
        if (m_explorer_context_menu.bounds.contains(cur_x, cur_y)) {
          bool over_item = false;
          for (std::size_t i = 0; i < m_explorer_context_menu.item_bounds.size(); ++i) {
            if (!m_explorer_context_menu.items[i].separator &&
                m_explorer_context_menu.item_bounds[i].contains(cur_x, cur_y)) {
              over_item = true;
              break;
            }
          }
          if (over_item) {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
          }
        }
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        return TRUE;
      }

      if (m_menu_overlay_open || m_open_menu_index) {
        bool over_menu_item = false;
        if (m_menu_overlay_open) {
          const MenuOverlayGeometry root_geom = calculate_menu_overlay_geometry();
          if (root_geom.bounds.contains(cur_x, cur_y)) {
            over_menu_item = true;
          }
        }
        if (!over_menu_item && m_open_menu_index) {
          if (const auto item_idx = get_popup_menu_item_index(cur_x, cur_y)) {
            if (is_popup_menu_item_enabled(*m_open_menu_index, *item_idx)) {
              over_menu_item = true;
            }
          }
        }
        if (over_menu_item) {
          SetCursor(LoadCursorW(nullptr, IDC_HAND));
          return TRUE;
        }
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        return TRUE;
      }

      const bool interactive =
          m_chrome_layout
              .get_menu_index(static_cast<float>(cursor_position.x),
                              static_cast<float>(cursor_position.y))
              .has_value() ||
          m_chrome_layout.is_overflow_menu(
              static_cast<float>(cursor_position.x),
              static_cast<float>(cursor_position.y)) ||
          m_chrome_layout.is_run_button(
              static_cast<float>(cursor_position.x),
              static_cast<float>(cursor_position.y)) ||
          m_chrome_layout.is_debug_button(
              static_cast<float>(cursor_position.x),
              static_cast<float>(cursor_position.y)) ||
          m_workspace_renderer.is_tab_bar_point(
              static_cast<float>(cursor_position.x),
              static_cast<float>(cursor_position.y),
              client_bounds.right - client_bounds.left,
              client_bounds.bottom - client_bounds.top,
              m_chrome_layout.titlebar_bounds.bottom()) ||
          m_workspace_renderer.is_activity_bar_point(
              static_cast<float>(cursor_position.x),
              static_cast<float>(cursor_position.y),
              client_bounds.right - client_bounds.left,
              client_bounds.bottom - client_bounds.top,
              m_chrome_layout.titlebar_bounds.bottom()) ||
          m_workspace_renderer.is_tool_sidebar_interactive_point(
              static_cast<float>(cursor_position.x),
              static_cast<float>(cursor_position.y),
              client_bounds.right - client_bounds.left,
              client_bounds.bottom - client_bounds.top,
              m_chrome_layout.titlebar_bounds.bottom()) ||
          m_workspace_renderer.is_fold_margin_point(
              static_cast<float>(cursor_position.x),
              static_cast<float>(cursor_position.y),
              client_bounds.right - client_bounds.left,
              client_bounds.bottom - client_bounds.top,
              m_chrome_layout.titlebar_bounds.bottom()) ||
          m_workspace_renderer.is_editor_interactive_point(
              static_cast<float>(cursor_position.x),
              static_cast<float>(cursor_position.y)) ||
          m_workspace_renderer.is_terminal_interactive_point(
              static_cast<float>(cursor_position.x),
              static_cast<float>(cursor_position.y),
              client_bounds.right - client_bounds.left,
              client_bounds.bottom - client_bounds.top,
              m_chrome_layout.titlebar_bounds.bottom());
      if (interactive) {
        SetCursor(LoadCursorW(nullptr, IDC_HAND));
        return TRUE;
      }
      if (m_workspace_renderer.is_terminal_resize_handle_point(
              static_cast<float>(cursor_position.x),
              static_cast<float>(cursor_position.y),
              client_bounds.right - client_bounds.left,
              client_bounds.bottom - client_bounds.top,
              m_chrome_layout.titlebar_bounds.bottom())) {
        SetCursor(LoadCursorW(nullptr, IDC_SIZENS));
        return TRUE;
      }
      if (m_workspace_renderer.is_sidebar_resize_handle_point(
              static_cast<float>(cursor_position.x),
              static_cast<float>(cursor_position.y),
              client_bounds.right - client_bounds.left,
              client_bounds.bottom - client_bounds.top,
              m_chrome_layout.titlebar_bounds.bottom()) ||
          m_workspace_renderer.is_shader_sandbox_resize_handle(
              static_cast<float>(cursor_position.x),
              static_cast<float>(cursor_position.y),
              client_bounds.right - client_bounds.left,
              client_bounds.bottom - client_bounds.top,
              m_chrome_layout.titlebar_bounds.bottom()) ||
          m_workspace_renderer.is_shader_sandbox_resizing() ||
          m_workspace_renderer.is_editor_split_resize_handle(
              static_cast<float>(cursor_position.x),
              static_cast<float>(cursor_position.y),
              client_bounds.right - client_bounds.left,
              client_bounds.bottom - client_bounds.top,
              m_chrome_layout.titlebar_bounds.bottom()) ||
          m_workspace_renderer.is_editor_split_resizing()) {
        SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
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
              m_chrome_layout.titlebar_bounds.bottom()) ||
          m_workspace_renderer.is_tool_sidebar_text_input_point(
              static_cast<float>(cursor_position.x),
              static_cast<float>(cursor_position.y),
              client_bounds.right - client_bounds.left,
              client_bounds.bottom - client_bounds.top,
              m_chrome_layout.titlebar_bounds.bottom())) {
        SetCursor(LoadCursorW(nullptr, IDC_IBEAM));
        return TRUE;
      }
    }
    break;

  case WM_KEYDOWN: {
    if (m_custom_chrome_enabled && m_workspace_renderer.is_prompt_modal_visible()) {
      if (w_param == VK_ESCAPE) {
        static_cast<void>(m_workspace_renderer.get_prompt_modal().handle_escape());
        InvalidateRect(window_handle, nullptr, FALSE);
        return 0;
      }
      if (w_param == VK_RETURN) {
        static_cast<void>(m_workspace_renderer.get_prompt_modal().handle_enter());
        InvalidateRect(window_handle, nullptr, FALSE);
        return 0;
      }
      if (w_param == VK_BACK) {
        static_cast<void>(m_workspace_renderer.get_prompt_modal().handle_backspace());
        InvalidateRect(window_handle, nullptr, FALSE);
        return 0;
      }
      return 0;
    }
    if (m_custom_chrome_enabled && m_about_modal.is_visible()) {
      if (w_param == VK_ESCAPE) {
        static_cast<void>(m_about_modal.handle_escape());
        InvalidateRect(window_handle, nullptr, FALSE);
        return 0;
      }
      if (w_param == VK_RETURN) {
        static_cast<void>(m_about_modal.handle_enter());
        InvalidateRect(window_handle, nullptr, FALSE);
        return 0;
      }
      return 0;
    }
    if (m_custom_chrome_enabled && (m_menu_overlay_open || m_open_menu_index || m_explorer_context_menu.visible) &&
        w_param == VK_ESCAPE) {
      close_menu_overlay();
      close_explorer_context_menu();
      InvalidateRect(window_handle, nullptr, FALSE);
      return 0;
    }

    auto dispatch_shortcut_command = [&](std::string_view cmd_id) -> bool {
      if (cmd_id == Commands::CommandIds::window_toggle_fullscreen) {
        toggle_fullscreen();
        return true;
      }
      if (cmd_id == Commands::CommandIds::window_minimize) {
        minimize();
        return true;
      }
      if (cmd_id == Commands::CommandIds::window_maximize) {
        if (is_maximized()) {
          restore();
        } else {
          maximize();
        }
        return true;
      }
      if (cmd_id == Commands::CommandIds::window_reset_layout) {
        reset_layout();
        return true;
      }
      if (cmd_id == Commands::CommandIds::window_close) {
        request_close();
        return true;
      }
      const std::optional<bool> editor_result =
          m_workspace_renderer.handle_editor_command(cmd_id);
      if (!editor_result && m_command_invoked_callback) {
        m_command_invoked_callback(cmd_id);
        InvalidateRect(window_handle, nullptr, FALSE);
        return true;
      }
      if (editor_result.value_or(false)) {
        InvalidateRect(window_handle, nullptr, FALSE);
        return true;
      }
      return false;
    };

    if (m_custom_chrome_enabled) {
      const bool alt_down = (GetKeyState(VK_MENU) & 0x8000) != 0;
      const bool ctrl_down = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
      const bool shift_down = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

      if (!ctrl_down && !alt_down && !shift_down) {
        if (w_param == VK_F11) {
          if (dispatch_shortcut_command(Commands::CommandIds::window_toggle_fullscreen)) return 0;
        } else if (w_param == VK_F5) {
          if (dispatch_shortcut_command(Commands::CommandIds::run_start)) return 0;
        }
      } else if (ctrl_down && shift_down && !alt_down) {
        if (w_param == 'W') {
          if (dispatch_shortcut_command(Commands::CommandIds::window_close)) return 0;
        } else if (w_param == 'S') {
          if (dispatch_shortcut_command(Commands::CommandIds::file_save_as)) return 0;
        } else if (w_param == 'B') {
          if (dispatch_shortcut_command(Commands::CommandIds::build_build_project)) return 0;
        } else if (w_param == 'F') {
          if (dispatch_shortcut_command(Commands::CommandIds::view_search)) return 0;
        } else if (w_param == 'E') {
          if (dispatch_shortcut_command(Commands::CommandIds::view_explorer)) return 0;
        } else if (w_param == 'G') {
          if (dispatch_shortcut_command(Commands::CommandIds::view_git_panel)) return 0;
        } else if (w_param == 'D') {
          if (dispatch_shortcut_command(Commands::CommandIds::view_debugger_panel)) return 0;
        } else if (w_param == 'X') {
          if (dispatch_shortcut_command(Commands::CommandIds::open_plugins)) return 0;
        } else if (w_param == 'M') {
          if (dispatch_shortcut_command(Commands::CommandIds::view_diagnostics)) return 0;
        } else if (w_param == 'P') {
          if (dispatch_shortcut_command(Commands::CommandIds::help_show_all_commands)) return 0;
        } else if (w_param == VK_TAB || w_param == VK_PRIOR) {
          if (dispatch_shortcut_command(Commands::CommandIds::window_prev_tab)) return 0;
        }
      } else if (ctrl_down && alt_down && !shift_down) {
        if (w_param == 'B') {
          if (dispatch_shortcut_command(Commands::CommandIds::view_toggle_right_dock)) return 0;
        }
      } else if (ctrl_down && !shift_down && !alt_down) {
        if (w_param == VK_TAB || w_param == VK_NEXT) {
          if (dispatch_shortcut_command(Commands::CommandIds::window_next_tab)) return 0;
        } else if (w_param == VK_OEM_5) {
          if (dispatch_shortcut_command(Commands::CommandIds::view_split_right)) return 0;
        } else if (w_param == 'B') {
          if (dispatch_shortcut_command(Commands::CommandIds::view_toggle_left_dock)) return 0;
        } else if (w_param == 'J' || w_param == VK_OEM_3) {
          if (dispatch_shortcut_command(Commands::CommandIds::view_terminal_panel)) return 0;
        } else if (w_param == VK_OEM_COMMA) {
          if (dispatch_shortcut_command(Commands::CommandIds::open_settings)) return 0;
        } else if (w_param == 'O') {
          if (dispatch_shortcut_command(Commands::CommandIds::file_open)) return 0;
        }
      }
    }
    if (m_custom_chrome_enabled && m_workspace_renderer.is_search_focused()) {
      const bool alt_pressed = (GetKeyState(VK_MENU) & 0x8000) != 0;
      const bool control_pressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
      const bool shift_pressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
      if (m_workspace_renderer.handle_search_key(static_cast<int>(w_param), control_pressed, shift_pressed, alt_pressed)) {
        InvalidateRect(window_handle, nullptr, FALSE);
        return 0;
      }
    }
    if (m_custom_chrome_enabled && m_workspace_renderer.is_terminal_focused()) {
      const bool shift_pressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
      if (shift_pressed && (w_param == VK_LEFT || w_param == VK_RIGHT ||
                            w_param == VK_UP || w_param == VK_DOWN)) {
        Event::ScrollEvent scroll_event;
        scroll_event.is_keyboard = true;
        if (w_param == VK_LEFT)
          scroll_event.delta_x = 3;
        else if (w_param == VK_RIGHT)
          scroll_event.delta_x = -3;
        else if (w_param == VK_UP)
          scroll_event.delta_y = 3;
        else if (w_param == VK_DOWN)
          scroll_event.delta_y = -3;

        if (m_workspace_renderer.handle_terminal_scroll(scroll_event)) {
          const int ct = round_to_int(m_chrome_layout.titlebar_bounds.bottom());
          RECT cr{0, ct, 32767, 32767};
          InvalidateRect(window_handle, &cr, FALSE);
        }
        return 0;
      }

      std::optional<Terminal::TerminalInputKey> terminal_key;
      switch (w_param) {
      case VK_ESCAPE:
        terminal_key = Terminal::TerminalInputKey::Escape;
        break;
      case VK_UP:
        terminal_key = Terminal::TerminalInputKey::ArrowUp;
        break;
      case VK_DOWN:
        terminal_key = Terminal::TerminalInputKey::ArrowDown;
        break;
      case VK_LEFT:
        terminal_key = Terminal::TerminalInputKey::ArrowLeft;
        break;
      case VK_RIGHT:
        terminal_key = Terminal::TerminalInputKey::ArrowRight;
        break;
      case VK_HOME:
        terminal_key = Terminal::TerminalInputKey::Home;
        break;
      case VK_END:
        terminal_key = Terminal::TerminalInputKey::End;
        break;
      case VK_DELETE:
        terminal_key = Terminal::TerminalInputKey::DeleteForward;
        break;
      default:
        break;
      }
      if (terminal_key) {
        if (m_workspace_renderer.handle_terminal_key(*terminal_key)) {
          const int ct = round_to_int(m_chrome_layout.titlebar_bounds.bottom());
          RECT cr{0, ct, 32767, 32767};
          InvalidateRect(window_handle, &cr, FALSE);
        }
        return 0;
      }
    }
    if (m_custom_chrome_enabled && m_workspace_renderer.is_editor_focused()) {
      const bool alt_pressed = (GetKeyState(VK_MENU) & 0x8000) != 0;
      const bool control_pressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
      const bool shift_pressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

      if (w_param == VK_ESCAPE) {
        if (m_workspace_renderer.handle_editor_input(UI::Editor::EditorInputCommand::Escape, false)) {
          const int ct = round_to_int(m_chrome_layout.titlebar_bounds.bottom());
          RECT cr{0, ct, 32767, 32767};
          InvalidateRect(window_handle, &cr, FALSE);
          return 0;
        }
      }

      if ((control_pressed && shift_pressed && (w_param == VK_UP || w_param == VK_DOWN)) ||
          (control_pressed && alt_pressed && (w_param == VK_UP || w_param == VK_DOWN))) {
        const auto cmd = (w_param == VK_UP) ? UI::Editor::EditorInputCommand::AddCursorAbove
                                             : UI::Editor::EditorInputCommand::AddCursorBelow;
        if (m_workspace_renderer.handle_editor_input(cmd, false)) {
          const int ct = round_to_int(m_chrome_layout.titlebar_bounds.bottom());
          RECT cr{0, ct, 32767, 32767};
          InvalidateRect(window_handle, &cr, FALSE);
        }
        return 0;
      }

      if (alt_pressed && !control_pressed && (w_param == VK_UP || w_param == VK_DOWN)) {
        const auto cmd = (w_param == VK_UP) ? UI::Editor::EditorInputCommand::MoveLineUp
                                             : UI::Editor::EditorInputCommand::MoveLineDown;
        if (m_workspace_renderer.handle_editor_input(cmd, false)) {
          const int ct = round_to_int(m_chrome_layout.titlebar_bounds.bottom());
          RECT cr{0, ct, 32767, 32767};
          InvalidateRect(window_handle, &cr, FALSE);
        }
        return 0;
      }
      std::optional<UI::Editor::EditorAction> action;
      if (control_pressed && shift_pressed && w_param == VK_DELETE) {
        action = UI::Editor::EditorAction::RemoveDocument;
      } else if (control_pressed) {
        switch (w_param) {
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
        case VK_OEM_2:
          action = UI::Editor::EditorAction::ToggleComment;
          break;
        case VK_SPACE:
          if (m_workspace_renderer.trigger_editor_autocomplete()) {
            const int ct = round_to_int(m_chrome_layout.titlebar_bounds.bottom());
            RECT cr{0, ct, 32767, 32767};
            InvalidateRect(window_handle, &cr, FALSE);
          }
          return 0;
        default:
          break;
        }
      }
      if (action) {
        if (m_workspace_renderer.handle_editor_action(*action)) {
          const int ct = round_to_int(m_chrome_layout.titlebar_bounds.bottom());
          RECT cr{0, ct, 32767, 32767};
          InvalidateRect(window_handle, &cr, FALSE);
        }
        return 0;
      }

      std::optional<UI::Editor::EditorInputCommand> editor_command;
      switch (w_param) {
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
      if (editor_command) {
        if (m_workspace_renderer.handle_editor_input(*editor_command,
                                                     shift_pressed)) {
          const int ct = round_to_int(m_chrome_layout.titlebar_bounds.bottom());
          RECT cr{0, ct, 32767, 32767};
          InvalidateRect(window_handle, &cr, FALSE);
        }
        return 0;
      }
    }
    break;
  }

  case WM_SYSKEYDOWN:
    if (m_custom_chrome_enabled && m_workspace_renderer.is_editor_focused()) {
      const bool control_pressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
      if (w_param == VK_UP || w_param == VK_DOWN) {
        const auto cmd = control_pressed
            ? ((w_param == VK_UP) ? UI::Editor::EditorInputCommand::AddCursorAbove
                                  : UI::Editor::EditorInputCommand::AddCursorBelow)
            : ((w_param == VK_UP) ? UI::Editor::EditorInputCommand::MoveLineUp
                                  : UI::Editor::EditorInputCommand::MoveLineDown);
        if (m_workspace_renderer.handle_editor_input(cmd, false)) {
          const int ct = round_to_int(m_chrome_layout.titlebar_bounds.bottom());
          RECT cr{0, ct, 32767, 32767};
          InvalidateRect(window_handle, &cr, FALSE);
        }
        return 0;
      }
    }
    break;

  case WM_SYSKEYUP:
    if (w_param == VK_UP || w_param == VK_DOWN) {
      return 0;
    }
    break;

  case WM_CHAR:
    if (m_custom_chrome_enabled && m_workspace_renderer.is_prompt_modal_visible()) {
      if (w_param >= 32) {
        static_cast<void>(m_workspace_renderer.get_prompt_modal().handle_char(static_cast<char32_t>(w_param)));
        InvalidateRect(window_handle, nullptr, FALSE);
      }
      return 0;
    }
    if (m_custom_chrome_enabled && m_workspace_renderer.is_search_focused()) {
      if (w_param >= 32) {
        if (m_workspace_renderer.handle_search_char(static_cast<char32_t>(w_param))) {
          InvalidateRect(window_handle, nullptr, FALSE);
          return 0;
        }
      }
    }
    if (m_custom_chrome_enabled && m_workspace_renderer.is_terminal_focused()) {
      bool changed = false;
      const wchar_t character = static_cast<wchar_t>(w_param);
      if (character == L'\r') {
        changed = m_workspace_renderer.handle_terminal_key(
            Terminal::TerminalInputKey::Enter);
      } else if (character == L'\b') {
        changed = m_workspace_renderer.handle_terminal_key(
            Terminal::TerminalInputKey::Backspace);
      } else if (character == L'\t') {
        changed = m_workspace_renderer.handle_terminal_key(
            Terminal::TerminalInputKey::Tab);
      } else if (character >= 1 && character <= 26) {
        changed = m_workspace_renderer.handle_terminal_control(
            static_cast<char>('A' + character - 1));
      } else if (character >= 0x20 && character != 0x7F) {
        std::wstring utf16;
        if (character >= 0xD800 && character <= 0xDBFF) {
          m_pending_high_surrogate = character;
          return 0;
        }
        if (character >= 0xDC00 && character <= 0xDFFF) {
          if (m_pending_high_surrogate == 0) {
            return 0;
          }
          utf16.push_back(m_pending_high_surrogate);
          m_pending_high_surrogate = 0;
        } else {
          m_pending_high_surrogate = 0;
        }
        utf16.push_back(character);
        const auto utf8 = Utility::wide_to_utf8(utf16);
        changed = utf8 && !utf8->empty() &&
                  m_workspace_renderer.handle_text_input(*utf8);
      }
      if (changed) {
        const int ct = round_to_int(m_chrome_layout.titlebar_bounds.bottom());
        RECT cr{0, ct, 32767, 32767};
        InvalidateRect(window_handle, &cr, FALSE);
      }
      return 0;
    }
    if (m_custom_chrome_enabled && m_workspace_renderer.is_editor_focused()) {
      bool changed = false;
      const wchar_t character = static_cast<wchar_t>(w_param);
      if (character == L'\r') {
        changed = m_workspace_renderer.handle_editor_input(
            UI::Editor::EditorInputCommand::InsertNewLine, false);
      } else if (character == L'\b') {
        changed = m_workspace_renderer.handle_editor_input(
            UI::Editor::EditorInputCommand::DeleteBackward, false);
      } else if (character == L'\t') {
        changed = m_workspace_renderer.handle_editor_input(
            UI::Editor::EditorInputCommand::InsertTab, false);
      } else if (character >= 0x20 && character != 0x7F) {
        std::wstring utf16;
        if (character >= 0xD800 && character <= 0xDBFF) {
          m_pending_high_surrogate = character;
          return 0;
        }
        if (character >= 0xDC00 && character <= 0xDFFF) {
          if (m_pending_high_surrogate == 0) {
            return 0;
          }
          utf16.push_back(m_pending_high_surrogate);
          m_pending_high_surrogate = 0;
        } else {
          m_pending_high_surrogate = 0;
        }
        utf16.push_back(character);
        const auto utf8 = Utility::wide_to_utf8(utf16);
        changed = utf8 && !utf8->empty() &&
                  m_workspace_renderer.handle_text_input(*utf8);
      }
      if (changed) {
        const int ct = round_to_int(m_chrome_layout.titlebar_bounds.bottom());
        RECT cr{0, ct, 32767, 32767};
        InvalidateRect(window_handle, &cr, FALSE);
      }
      return 0;
    }
    break;

  case WM_COMMAND: {
    const UINT cmd_id = LOWORD(w_param);
    if (m_menubar.handle_command(cmd_id)) {
      return 0;
    }
    switch (cmd_id) {
    case TrayCmdShow:
      restore_from_tray();
      return 0;
    case TrayCmdHide:
      minimize_to_tray();
      return 0;
    case TrayCmdNewFile: {
      restore_from_tray();
      const auto root = m_workspace_renderer.m_tool_sidebar.get_model().get_workspace_root();
      const std::string proj_name = root.filename().string();
      const auto target_dir = m_workspace_renderer.m_tool_sidebar.get_model().get_target_directory_for_creation();
      m_workspace_renderer.get_add_item_dialog().open(m_window_handle, target_dir, proj_name, [this](const std::string& name, const std::string& initial_content) {
        std::filesystem::path created_p;
        if (m_workspace_renderer.m_tool_sidebar.get_model().create_file(name, created_p)) {
          if (!initial_content.empty()) {
            std::ofstream out(created_p, std::ios::binary);
            if (out.is_open()) {
              out.write(initial_content.data(), initial_content.size());
              out.close();
            }
          }
          static_cast<void>(m_workspace_renderer.open_file(created_p));
        }
        InvalidateRect(m_window_handle, nullptr, FALSE);
      });
      InvalidateRect(m_window_handle, nullptr, FALSE);
      return 0;
    }
    case TrayCmdOpenFolder:
      restore_from_tray();
      static_cast<void>(open_project_folder());
      return 0;
    case TrayCmdExit:
      m_tray.destroy();
      PostMessageW(window_handle, WM_CLOSE, 0, 0);
      return 0;
    default:
      break;
    }
    break;
  }

  case WM_TIMER:
    if (w_param == editor_caret_timer_id) {
      if (!is_minimized() && m_workspace_renderer.tick_animations()) {
        InvalidateRect(window_handle, nullptr, FALSE);
      }
      return 0;
    }
    break;

  case WM_GETMINMAXINFO:
    if (m_custom_chrome_enabled) {
      auto *min_max_info = reinterpret_cast<MINMAXINFO *>(l_param);
      const float dpi_scale = static_cast<float>(m_dpi) / 96.0F;
      min_max_info->ptMinTrackSize.x = round_to_int(720.0F * dpi_scale);
      min_max_info->ptMinTrackSize.y = round_to_int(480.0F * dpi_scale);

      const HMONITOR monitor =
          MonitorFromWindow(window_handle, MONITOR_DEFAULTTONEAREST);
      if (monitor != nullptr) {
        MONITORINFO monitor_info{};
        monitor_info.cbSize = sizeof(monitor_info);
        if (GetMonitorInfoW(monitor, &monitor_info) != FALSE) {
          if (m_is_fullscreen) {
            min_max_info->ptMaxPosition.x = monitor_info.rcMonitor.left;
            min_max_info->ptMaxPosition.y = monitor_info.rcMonitor.top;
            min_max_info->ptMaxSize.x =
                std::abs(monitor_info.rcMonitor.right - monitor_info.rcMonitor.left);
            min_max_info->ptMaxSize.y =
                std::abs(monitor_info.rcMonitor.bottom - monitor_info.rcMonitor.top);
            min_max_info->ptMaxTrackSize.x = min_max_info->ptMaxSize.x;
            min_max_info->ptMaxTrackSize.y = min_max_info->ptMaxSize.y;
          } else {
            min_max_info->ptMaxPosition.x =
                std::abs(monitor_info.rcWork.left - monitor_info.rcMonitor.left);
            min_max_info->ptMaxPosition.y =
                std::abs(monitor_info.rcWork.top - monitor_info.rcMonitor.top);
            min_max_info->ptMaxSize.x =
                std::abs(monitor_info.rcWork.right - monitor_info.rcWork.left);
            min_max_info->ptMaxSize.y =
                std::abs(monitor_info.rcWork.bottom - monitor_info.rcWork.top);
            min_max_info->ptMaxTrackSize.x = min_max_info->ptMaxSize.x;
            min_max_info->ptMaxTrackSize.y = min_max_info->ptMaxSize.y;
          }
        }
      }
      return 0;
    }
    break;

  case WM_DPICHANGED: {
    m_dpi = HIWORD(w_param);
    const auto *suggested_bounds = reinterpret_cast<const RECT *>(l_param);
    SetWindowPos(window_handle, nullptr, suggested_bounds->left,
                 suggested_bounds->top,
                 suggested_bounds->right - suggested_bounds->left,
                 suggested_bounds->bottom - suggested_bounds->top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    refresh_ui_font();
    static_cast<void>(m_workspace_renderer.initialize(m_dpi));
    refresh_chrome_layout();
    return 0;
  }

  case WM_ENTERSIZEMOVE:
    break;

  case WM_EXITSIZEMOVE:
    if (m_custom_chrome_enabled) {
      refresh_chrome_layout();
      InvalidateRect(window_handle, nullptr, TRUE);
      UpdateWindow(window_handle);
    }
    break;

  case WM_SIZING:
    if (m_custom_chrome_enabled) {
      refresh_chrome_layout();
      InvalidateRect(window_handle, nullptr, FALSE);
    }
    break;

  case WM_SYNCPAINT:
    if (m_custom_chrome_enabled) {
      InvalidateRect(window_handle, nullptr, FALSE);
      return 0;
    }
    break;

  case WM_NCPAINT:
    if (m_custom_chrome_enabled) {
      return 0;
    }
    break;

  case WM_SHOWWINDOW:
    if (w_param != FALSE) {
      refresh_chrome_layout();
      if (m_custom_chrome_enabled) {
        InvalidateRect(window_handle, nullptr, TRUE);
      }
    }
    break;

  case WM_ACTIVATE:
    if (LOWORD(w_param) != WA_INACTIVE) {
      refresh_chrome_layout();
      static_cast<void>(m_workspace_renderer.m_text_editor.check_external_file_changes());
      if (m_custom_chrome_enabled) {
        InvalidateRect(window_handle, nullptr, FALSE);
      }
    }
    break;

  case WM_SETFOCUS:
    static_cast<void>(m_workspace_renderer.m_text_editor.check_external_file_changes());
    InvalidateRect(window_handle, nullptr, FALSE);
    break;

  case WM_WINDOWPOSCHANGED: {
    const auto *window_pos = reinterpret_cast<const WINDOWPOS *>(l_param);
    if ((window_pos->flags & SWP_NOSIZE) == 0 ||
        (window_pos->flags & SWP_SHOWWINDOW) != 0) {
      if (!is_minimized()) {
        apply_system_corner_preference();
        refresh_chrome_layout();
        if (m_custom_chrome_enabled) {
          InvalidateRect(window_handle, nullptr, FALSE);
        }
      }
    }
    break;
  }

  case WM_SIZE:
    if (w_param != SIZE_MINIMIZED) {
      apply_system_corner_preference();
      refresh_chrome_layout();
      if (m_custom_chrome_enabled) {
        InvalidateRect(window_handle, nullptr, FALSE);
      }
    }
    return 0;

  case WM_PAINT:
    if (m_custom_chrome_enabled) {
      paint_custom_chrome();
      return 0;
    }
    break;

  case WM_ERASEBKGND:
    if (m_custom_chrome_enabled) {
      return 1;
    }
    break;

  case WM_NCACTIVATE:
    if (m_custom_chrome_enabled) {
      InvalidateRect(window_handle, nullptr, FALSE);
      return TRUE;
    }
    break;

  case WM_DWMCOMPOSITIONCHANGED:
  case WM_SETTINGCHANGE: {
    apply_system_corner_preference();
    break;
  }

  case WM_TRAYICON: {
    const UINT event_msg = LOWORD(l_param);
    if (event_msg == WM_LBUTTONUP || event_msg == WM_LBUTTONDBLCLK || event_msg == NIN_SELECT || event_msg == NIN_KEYSELECT) {
      restore_from_tray();
      return 0;
    }
    if (event_msg == WM_RBUTTONUP || event_msg == WM_CONTEXTMENU) {
      POINT pt{};
      GetCursorPos(&pt);
      m_tray.show_context_menu(window_handle, pt);
      return 0;
    }
    return 0;
  }

  case WM_CLOSE:
    DestroyWindow(window_handle);
    return 0;

  case WM_DESTROY:
    m_tray.destroy();
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

LRESULT Win32Window::hit_test_non_client(LPARAM l_param) {
  POINT cursor_position{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
  ScreenToClient(m_window_handle, &cursor_position);

  const float point_x = static_cast<float>(cursor_position.x);
  const float point_y = static_cast<float>(cursor_position.y);
  RECT client_bounds{};
  GetClientRect(m_window_handle, &client_bounds);
  const bool tab_point = m_workspace_renderer.is_tab_bar_point(
      point_x, point_y, client_bounds.right - client_bounds.left,
      client_bounds.bottom - client_bounds.top,
      m_chrome_layout.titlebar_bounds.bottom());
  // Tabs sit close to the top frame; let a real tab hit win over the resize
  // border so the file buffer remains clickable at its rounded top edge.
  if (!tab_point) {
    const LRESULT resize_result = hit_test_resize_border(cursor_position);
    if (resize_result != HTNOWHERE) {
      return resize_result;
    }
  }

  switch (m_chrome_layout.get_window_control(point_x, point_y)) {
  case UI::Chrome::WindowControl::Minimize:
    return HTMINBUTTON;
  case UI::Chrome::WindowControl::MaximizeRestore:
    return HTMAXBUTTON;
  case UI::Chrome::WindowControl::Close:
    return HTCLOSE;
  case UI::Chrome::WindowControl::NoControl:
    break;
  }

  if (m_chrome_layout.logo_bounds.contains(point_x, point_y)) {
    return HTSYSMENU;
  }

  if (tab_point) {
    return HTCLIENT;
  }

  const bool is_drag_region =
      m_titlebar_hit_test_callback
          ? m_titlebar_hit_test_callback(point_x, point_y)
          : m_chrome_layout.is_drag_region(point_x, point_y);
  return is_drag_region ? HTCAPTION : HTCLIENT;
}

LRESULT Win32Window::hit_test_resize_border(POINT client_position) const {
  if (is_maximized()) {
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

  if (on_top && on_left) {
    return HTTOPLEFT;
  }
  if (on_top && on_right) {
    return HTTOPRIGHT;
  }
  if (on_bottom && on_left) {
    return HTBOTTOMLEFT;
  }
  if (on_bottom && on_right) {
    return HTBOTTOMRIGHT;
  }
  if (on_left) {
    return HTLEFT;
  }
  if (on_right) {
    return HTRIGHT;
  }
  if (on_top) {
    return HTTOP;
  }
  if (on_bottom) {
    return HTBOTTOM;
  }
  return HTNOWHERE;
}

void Win32Window::apply_system_corner_preference() {
  if (m_window_handle == nullptr) {
    return;
  }

  // Let the OS decide window corner rounding, adapting to the Windows
  // version the app is running on:
  // - Windows 10: corners are naturally square (attribute is ignored).
  // - Windows 11: follows the system's own corner preference.
  // No corner policy is forced here; the app stays consistent with the
  // platform default while keeping the native frame and drop shadow.
  constexpr DWORD dwm_window_corner_preference_attribute = 33;
  const DWORD corner_preference = 0; // DWMWCP_DEFAULT
  DwmSetWindowAttribute(m_window_handle, dwm_window_corner_preference_attribute,
                        &corner_preference, sizeof(corner_preference));

  // Clear any custom clipping region so DWM renders the native frame
  SetWindowRgn(m_window_handle, nullptr, TRUE);
}

void Win32Window::paint_custom_chrome() {
  PAINTSTRUCT paint_data{};
  HDC window_context = BeginPaint(m_window_handle, &paint_data);

  if (is_minimized()) {
    EndPaint(m_window_handle, &paint_data);
    return;
  }

  RECT client_bounds{};
  GetClientRect(m_window_handle, &client_bounds);
  const int client_width = client_bounds.right - client_bounds.left;
  const int client_height = client_bounds.bottom - client_bounds.top;
  if (client_width <= 0 || client_height <= 0) {
    EndPaint(m_window_handle, &paint_data);
    return;
  }

  HDC buffer_context = CreateCompatibleDC(window_context);
  HBITMAP buffer_bitmap =
      CreateCompatibleBitmap(window_context, client_width, client_height);
  HGDIOBJ previous_bitmap = SelectObject(buffer_context, buffer_bitmap);

  fill_rectangle(buffer_context,
                 UI::Rect{0.0F, 0.0F, static_cast<float>(client_width),
                          static_cast<float>(client_height)},
                 m_theme.window_background);
  fill_rectangle(buffer_context, m_chrome_layout.titlebar_bounds,
                 m_theme.titlebar_background);

  SetBkMode(buffer_context, TRANSPARENT);
  HGDIOBJ previous_font = SelectObject(buffer_context, m_ui_font);

  if (m_chrome_layout.has_overflow_menu()) {
    const bool hidden_menu_open =
        m_open_menu_index &&
        *m_open_menu_index >= m_chrome_layout.first_overflow_menu_index;
    if (m_overflow_menu_hovered || m_menu_overlay_open || hidden_menu_open) {
      UI::Rect hover_bounds = m_chrome_layout.overflow_menu_bounds;
      hover_bounds.y += 4.0F * m_chrome_layout.dpi_scale;
      hover_bounds.height -= 8.0F * m_chrome_layout.dpi_scale;
      fill_rounded_rectangle(buffer_context, hover_bounds, m_theme.hover, 4);
    }
    HPEN menu_pen = CreatePen(
        PS_SOLID, std::max(1, round_to_int(m_chrome_layout.dpi_scale)),
        to_color_ref(m_theme.text_primary));
    HGDIOBJ previous_pen = SelectObject(buffer_context, menu_pen);
    const int center_x =
        round_to_int(m_chrome_layout.overflow_menu_bounds.x +
                     m_chrome_layout.overflow_menu_bounds.width * 0.5F);
    const int center_y =
        round_to_int(m_chrome_layout.overflow_menu_bounds.y +
                     m_chrome_layout.overflow_menu_bounds.height * 0.5F);
    const int half_width =
        std::max(round_to_int(6.0F * m_chrome_layout.dpi_scale), 4);
    const int gap = std::max(round_to_int(4.0F * m_chrome_layout.dpi_scale), 3);
    for (int row = -1; row <= 1; ++row) {
      MoveToEx(buffer_context, center_x - half_width, center_y + row * gap,
               nullptr);
      LineTo(buffer_context, center_x + half_width + 1, center_y + row * gap);
    }
    SelectObject(buffer_context, previous_pen);
    DeleteObject(menu_pen);
  }

  const float scale = m_chrome_layout.dpi_scale;
  const float logo_size = 22.0F * scale;
  const UI::Rect logo_mark{
      m_chrome_layout.logo_bounds.x +
          (m_chrome_layout.logo_bounds.width - logo_size) * 0.5F,
      m_chrome_layout.logo_bounds.y +
          (m_chrome_layout.logo_bounds.height - logo_size) * 0.5F,
      logo_size,
      logo_size,
  };
  // Try the large class icon first (set by RegisterClassExW), then the small
  // icon, then load directly from the resource. This avoids the common case
  // where GCLP_HICONSM is nullptr and the logo falls back to the "Z" glyph.
  HICON app_icon =
      reinterpret_cast<HICON>(GetClassLongPtrW(m_window_handle, GCLP_HICON));
  if (app_icon == nullptr) {
    app_icon = reinterpret_cast<HICON>(
        GetClassLongPtrW(m_window_handle, GCLP_HICONSM));
  }
  if (app_icon == nullptr) {
    app_icon = LoadIconW(m_instance_handle, MAKEINTRESOURCEW(IDI_APP_ICON));
  }
  if (app_icon != nullptr) {
    DrawIconEx(buffer_context, static_cast<int>(logo_mark.x),
               static_cast<int>(logo_mark.y), app_icon,
               static_cast<int>(logo_size), static_cast<int>(logo_size), 0,
               nullptr, DI_NORMAL);
  } else {
    fill_rounded_rectangle(buffer_context, logo_mark, m_theme.accent,
                           static_cast<int>(logo_size * 0.25F));
    RECT logo_text_bounds = to_native_rect(logo_mark);
    draw_centered_text(buffer_context, L"Z", logo_text_bounds,
                       UI::Theme::Color{255, 255, 255, 255});
  }


  static_cast<void>(m_workspace_renderer.tick_animations());
  m_workspace_renderer.render(buffer_context, client_width, client_height,
                              m_chrome_layout.titlebar_bounds.bottom());

  // Draw titlebar bottom separator border across full width with proper z-index above content
  const int titlebar_bottom_y =
      round_to_int(m_chrome_layout.titlebar_bounds.bottom()) - 1;
  HPEN titlebar_border_pen =
      CreatePen(PS_SOLID, 1, to_color_ref(m_theme.titlebar_border));
  HGDIOBJ prev_border_pen = SelectObject(buffer_context, titlebar_border_pen);
  MoveToEx(buffer_context, 0, titlebar_bottom_y, nullptr);
  LineTo(buffer_context, client_width, titlebar_bottom_y);
  SelectObject(buffer_context, prev_border_pen);
  DeleteObject(titlebar_border_pen);

  const int minimize_state =
      caption_button_state(UI::Chrome::WindowControl::Minimize,
                           m_hovered_control, m_pressed_control);
  const int maximize_state =
      caption_button_state(UI::Chrome::WindowControl::MaximizeRestore,
                           m_hovered_control, m_pressed_control);
  const int close_state = caption_button_state(
      UI::Chrome::WindowControl::Close, m_hovered_control, m_pressed_control);

  draw_custom_caption_button(buffer_context, m_chrome_layout.minimize_bounds,
                             UI::Chrome::WindowControl::Minimize,
                             minimize_state, is_maximized(), m_theme, scale);
  draw_custom_caption_button(buffer_context, m_chrome_layout.maximize_bounds,
                             UI::Chrome::WindowControl::MaximizeRestore,
                             maximize_state, is_maximized(), m_theme, scale);
  draw_custom_caption_button(buffer_context, m_chrome_layout.close_bounds,
                             UI::Chrome::WindowControl::Close, close_state,
                             is_maximized(), m_theme, scale);

  auto draw_toolbar_hover = [&](const UI::Rect &bounds) {
    UI::Rect hover_bounds = bounds;
    hover_bounds.y += 4.0F * scale;
    hover_bounds.height -= 8.0F * scale;
    hover_bounds.x += 2.0F * scale;
    hover_bounds.width -= 4.0F * scale;
    fill_rounded_rectangle(buffer_context, hover_bounds, m_theme.hover, 4);
  };

  if (!m_chrome_layout.build_bounds.is_empty()) {
    if (m_build_button_hovered) {
      draw_toolbar_hover(m_chrome_layout.build_bounds);
    }
    const int center_x =
        static_cast<int>(m_chrome_layout.build_bounds.x +
                         m_chrome_layout.build_bounds.width * 0.5F);
    const int center_y =
        static_cast<int>(m_chrome_layout.build_bounds.y +
                         m_chrome_layout.build_bounds.height * 0.5F);
    const int icon_size = std::max(static_cast<int>(16.0F * scale), 14);
    m_workspace_renderer.draw_svg_icon(
        buffer_context, "Assets/icons/build.svg", center_x, center_y, icon_size,
        m_workspace_renderer.m_palette.text_primary,
        m_build_button_hovered ? m_theme.hover : m_theme.titlebar_background);
  }

  if (!m_chrome_layout.run_bounds.is_empty()) {
    if (m_run_button_hovered) {
      draw_toolbar_hover(m_chrome_layout.run_bounds);
    }
    const int center_x = static_cast<int>(
        m_chrome_layout.run_bounds.x + m_chrome_layout.run_bounds.width * 0.5F);
    const int center_y =
        static_cast<int>(m_chrome_layout.run_bounds.y +
                         m_chrome_layout.run_bounds.height * 0.5F);
    const int icon_size = std::max(static_cast<int>(20.0F * scale), 14);
    m_workspace_renderer.draw_svg_icon(
        buffer_context, "Assets/icons/play.svg", center_x, center_y, icon_size,
        m_workspace_renderer.m_palette.success,
        m_run_button_hovered ? m_theme.hover : m_theme.titlebar_background);
  }

  if (!m_chrome_layout.debug_bounds.is_empty()) {
    if (m_debug_button_hovered) {
      draw_toolbar_hover(m_chrome_layout.debug_bounds);
    }
    const int center_x =
        static_cast<int>(m_chrome_layout.debug_bounds.x +
                         m_chrome_layout.debug_bounds.width * 0.5F);
    const int center_y =
        static_cast<int>(m_chrome_layout.debug_bounds.y +
                         m_chrome_layout.debug_bounds.height * 0.5F);
    const int icon_size = std::max(static_cast<int>(18.0F * scale), 14);
    m_workspace_renderer.draw_svg_icon(
        buffer_context, "Assets/icons/bug.svg", center_x, center_y, icon_size,
        m_workspace_renderer.m_palette.warning,
        m_debug_button_hovered ? m_theme.hover : m_theme.titlebar_background);
  }

  if (!m_chrome_layout.gear_bounds.is_empty()) {
    if (m_gear_button_hovered) {
      draw_toolbar_hover(m_chrome_layout.gear_bounds);
    }
    const int center_x =
        static_cast<int>(m_chrome_layout.gear_bounds.x +
                         m_chrome_layout.gear_bounds.width * 0.5F);
    const int center_y =
        static_cast<int>(m_chrome_layout.gear_bounds.y +
                         m_chrome_layout.gear_bounds.height * 0.5F);
    const int icon_size = std::max(static_cast<int>(16.0F * scale), 14);
    m_workspace_renderer.draw_svg_icon(
        buffer_context, "Assets/icons/gear.svg", center_x, center_y, icon_size,
        m_workspace_renderer.m_palette.text_primary,
        m_gear_button_hovered ? m_theme.hover : m_theme.titlebar_background);
  }

  if (!m_chrome_layout.ellipsis_bounds.is_empty()) {
    if (m_ellipsis_button_hovered) {
      draw_toolbar_hover(m_chrome_layout.ellipsis_bounds);
    }
    const int center_x =
        static_cast<int>(m_chrome_layout.ellipsis_bounds.x +
                         m_chrome_layout.ellipsis_bounds.width * 0.5F);
    const int center_y =
        static_cast<int>(m_chrome_layout.ellipsis_bounds.y +
                         m_chrome_layout.ellipsis_bounds.height * 0.5F);
    const int icon_size = std::max(static_cast<int>(16.0F * scale), 14);
    m_workspace_renderer.draw_svg_icon(
        buffer_context, "Assets/icons/ellipsis.svg", center_x, center_y,
        icon_size,
        m_workspace_renderer.m_palette.text_primary,
        m_ellipsis_button_hovered ? m_theme.hover
                                  : m_theme.titlebar_background);
  }

  // Build toolbar: Compiler | Binary
  if (!m_chrome_layout.compiler_bounds.is_empty()) {
    if (m_compiler_button_hovered) {
      draw_toolbar_hover(m_chrome_layout.compiler_bounds);
    }
    RECT text_rect = {
        static_cast<LONG>(m_chrome_layout.compiler_bounds.x + 10.0F * scale),
        static_cast<LONG>(m_chrome_layout.compiler_bounds.y),
        static_cast<LONG>(m_chrome_layout.compiler_bounds.right() - 22.0F * scale),
        static_cast<LONG>(m_chrome_layout.compiler_bounds.bottom())};
    std::string mode_str(UI::Toolbar::to_string(m_run_config_state.active_mode));
    std::wstring mode_wstr(mode_str.begin(), mode_str.end());
    draw_centered_text(buffer_context, mode_wstr.c_str(), text_rect,
                       m_theme.text_primary);
    const int chevron_x = static_cast<int>(
        m_chrome_layout.compiler_bounds.right() - 10.0F * scale);
    const int chevron_y =
        static_cast<int>(m_chrome_layout.compiler_bounds.y +
                         m_chrome_layout.compiler_bounds.height * 0.5F);
    m_workspace_renderer.draw_svg_icon(
        buffer_context, "Assets/icons/chevron-down.svg", chevron_x, chevron_y,
        std::max(static_cast<int>(10.0F * scale), 8),
        m_compiler_button_hovered ? m_theme.text_primary : m_workspace_renderer.m_palette.text_muted,
        m_compiler_button_hovered ? m_theme.hover : m_theme.titlebar_background);
  }

  if (!m_chrome_layout.platform_bounds.is_empty()) {
    if (m_platform_button_hovered) {
      draw_toolbar_hover(m_chrome_layout.platform_bounds);
    }
    RECT text_rect = {
        static_cast<LONG>(m_chrome_layout.platform_bounds.x + 10.0F * scale),
        static_cast<LONG>(m_chrome_layout.platform_bounds.y),
        static_cast<LONG>(m_chrome_layout.platform_bounds.right() -
                          22.0F * scale),
        static_cast<LONG>(m_chrome_layout.platform_bounds.bottom())};
    std::string arch_str(UI::Toolbar::to_string(m_run_config_state.active_architecture));
    std::wstring arch_wstr(arch_str.begin(), arch_str.end());
    draw_centered_text(buffer_context, arch_wstr.c_str(), text_rect,
                       m_theme.text_primary);
    const int chevron_x = static_cast<int>(
        m_chrome_layout.platform_bounds.right() - 10.0F * scale);
    const int chevron_y =
        static_cast<int>(m_chrome_layout.platform_bounds.y +
                         m_chrome_layout.platform_bounds.height * 0.5F);
    m_workspace_renderer.draw_svg_icon(
        buffer_context, "Assets/icons/chevron-down.svg", chevron_x, chevron_y,
        std::max(static_cast<int>(10.0F * scale), 8),
        m_platform_button_hovered ? m_theme.text_primary : m_workspace_renderer.m_palette.text_muted,
        m_platform_button_hovered ? m_theme.hover
                                  : m_theme.titlebar_background);
  }

  if (!m_chrome_layout.binary_bounds.is_empty()) {
    if (m_binary_button_hovered) {
      draw_toolbar_hover(m_chrome_layout.binary_bounds);
    }
    const int binary_icon_size = std::max(static_cast<int>(16.0F * scale), 14);
    m_workspace_renderer.draw_svg_icon(
        buffer_context, "Assets/icons/terminal.svg",
        static_cast<int>(m_chrome_layout.binary_bounds.x + 16.0F * scale),
        static_cast<int>(m_chrome_layout.binary_bounds.y +
                         m_chrome_layout.binary_bounds.height * 0.5F),
        binary_icon_size,
        m_theme.text_primary,
        m_binary_button_hovered ? m_theme.hover : m_theme.titlebar_background);
    RECT text_rect = {
        static_cast<LONG>(m_chrome_layout.binary_bounds.x + 36.0F * scale),
        static_cast<LONG>(m_chrome_layout.binary_bounds.y),
        static_cast<LONG>(m_chrome_layout.binary_bounds.right() -
                          16.0F * scale),
        static_cast<LONG>(m_chrome_layout.binary_bounds.bottom())};
    std::wstring bin_wstr(m_run_config_state.active_target_name.begin(), m_run_config_state.active_target_name.end());
    draw_centered_text(buffer_context, bin_wstr.c_str(), text_rect,
                       m_theme.text_primary);
    const int chevron_x =
        static_cast<int>(m_chrome_layout.binary_bounds.right() - 14.0F * scale);
    const int chevron_y =
        static_cast<int>(m_chrome_layout.binary_bounds.y +
                         m_chrome_layout.binary_bounds.height * 0.5F);
    m_workspace_renderer.draw_svg_icon(
        buffer_context, "Assets/icons/chevron-down.svg", chevron_x, chevron_y,
        std::max(static_cast<int>(12.0F * scale), 10),
        m_binary_button_hovered ? m_theme.text_primary : m_workspace_renderer.m_palette.text_muted,
        m_binary_button_hovered ? m_theme.hover : m_theme.titlebar_background);
  }

  if (!is_maximized()) {
    HPEN window_border_pen =
        CreatePen(PS_SOLID, 1, to_color_ref(m_theme.titlebar_border));
    HGDIOBJ prev_w_pen = SelectObject(buffer_context, window_border_pen);
    HGDIOBJ prev_w_brush =
        SelectObject(buffer_context, GetStockObject(NULL_BRUSH));
    Rectangle(buffer_context, 0, 0, client_width, client_height);
    SelectObject(buffer_context, prev_w_brush);
    SelectObject(buffer_context, prev_w_pen);
    DeleteObject(window_border_pen);
  }

  draw_menu_overlay(buffer_context);
  draw_explorer_context_menu(buffer_context);
  draw_about_modal(buffer_context, client_width, client_height);
  m_workspace_renderer.render_prompt_modal(buffer_context, client_width, client_height);
  m_workspace_renderer.render_add_item_dialog(buffer_context, client_width, client_height, m_theme);

  SelectObject(buffer_context, previous_font);

  BitBlt(window_context, 0, 0, client_width, client_height, buffer_context,
         0, 0, SRCCOPY);
  SelectObject(buffer_context, previous_bitmap);
  DeleteObject(buffer_bitmap);
  DeleteDC(buffer_context);
  EndPaint(m_window_handle, &paint_data);
}

void Win32Window::refresh_chrome_layout() {
  if (m_window_handle == nullptr) {
    return;
  }

  RECT client_bounds{};
  GetClientRect(m_window_handle, &client_bounds);
  UI::Chrome::WindowChromeLayoutOptions options;
  options.hamburger_only = true; // All menus behind the hamburger popup
  m_chrome_layout = m_chrome_layout_engine.calculate(
      static_cast<float>(client_bounds.right - client_bounds.left),
      static_cast<float>(m_dpi) / 96.0F,
      options);
}

void Win32Window::refresh_ui_font() {
  if (m_ui_font != nullptr) {
    DeleteObject(m_ui_font);
  }

  m_ui_font = CreateFontW(
      -MulDiv(9, static_cast<int>(m_dpi), 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE,
      FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

void Win32Window::show_menu(std::size_t menu_index) {
  const std::span<const UI::Components::Menu> menus =
      UI::Components::get_window_menus();
  if (menu_index >= menus.size()) {
    return;
  }

  // Keep the overflow list open while its child menu is displayed. This
  // makes the hierarchy explicit: File -> New File, Open File, ...
  const bool opened_from_overflow =
      m_menu_overlay_open && m_chrome_layout.has_overflow_menu() &&
      menu_index >= m_chrome_layout.first_overflow_menu_index;
  if (!opened_from_overflow) {
    m_menu_overlay_open = false;
  }
  m_open_menu_index = menu_index;
  m_hovered_menu_index.reset();
  m_hovered_popup_item_index.reset();
  InvalidateRect(m_window_handle, nullptr, FALSE);
}

void Win32Window::show_overflow_menu() {
  if (!m_chrome_layout.has_overflow_menu()) {
    return;
  }

  const bool should_open = !m_menu_overlay_open;
  close_menu_overlay();
  m_menu_overlay_open = should_open;
  InvalidateRect(m_window_handle, nullptr, FALSE);
}

void Win32Window::close_menu_overlay() {
  m_menu_overlay_open = false;
  m_open_menu_index.reset();
  m_hovered_menu_index.reset();
  m_hovered_popup_item_index.reset();
  m_menu_pointer_tracking = false;
}

void Win32Window::execute_menu_item(std::size_t menu_index,
                                    std::size_t item_index) {
  const std::span<const UI::Components::Menu> menus =
      UI::Components::get_window_menus();
  if (menu_index >= menus.size() ||
      item_index >= menus[menu_index].items.size()) {
    close_menu_overlay();
    return;
  }

  const std::string_view command_id =
      menus[menu_index].items[item_index].command_id;
  close_menu_overlay();
  if (command_id == Commands::CommandIds::help_about) {
    show_about_dialog();
    return;
  }
  if (command_id == Commands::CommandIds::window_toggle_fullscreen) {
    toggle_fullscreen();
    return;
  }
  if (command_id == Commands::CommandIds::window_minimize) {
    minimize();
    return;
  }
  if (command_id == Commands::CommandIds::window_maximize) {
    if (is_maximized()) {
      restore();
    } else {
      maximize();
    }
    return;
  }
  if (command_id == Commands::CommandIds::window_reset_layout) {
    reset_layout();
    return;
  }
  if (command_id == Commands::CommandIds::window_close) {
    request_close();
    return;
  }
  if (command_id == Commands::CommandIds::build_debug) {
    m_run_config_state.active_mode = UI::Toolbar::BuildConfigurationMode::Debug;
  } else if (command_id == Commands::CommandIds::build_release) {
    m_run_config_state.active_mode = UI::Toolbar::BuildConfigurationMode::Release;
  } else if (command_id == Commands::CommandIds::platform_arm64 ||
             command_id == Commands::CommandIds::platform_aarch64 ||
             command_id == Commands::CommandIds::platform_apple_arm) {
    m_run_config_state.active_architecture = UI::Toolbar::TargetArchitecture::Arm64;
  } else if (command_id == Commands::CommandIds::platform_x64) {
    m_run_config_state.active_architecture = UI::Toolbar::TargetArchitecture::X86_64;
  } else if (command_id == Commands::CommandIds::run_zde) {
    m_run_config_state.active_target_name = "ZDE";
  } else if (command_id == Commands::CommandIds::run_tests) {
    m_run_config_state.active_target_name = "ZDEUnitTests";
  }

  if (!command_id.empty()) {
    const std::optional<bool> editor_result =
        m_workspace_renderer.handle_editor_command(command_id);
    if (!editor_result && m_command_invoked_callback) {
      m_command_invoked_callback(command_id);
    }
  }
  InvalidateRect(m_window_handle, nullptr, FALSE);
}

Win32Window::MenuOverlayGeometry
Win32Window::calculate_menu_overlay_geometry() const noexcept {
  MenuOverlayGeometry geometry;
  if (!m_chrome_layout.has_overflow_menu()) {
    return geometry;
  }

  const std::span<const UI::Components::Menu> menus =
      UI::Components::get_window_menus();
  const float row_height = 24.0F * m_chrome_layout.dpi_scale;
  const float vertical_padding = 4.0F * m_chrome_layout.dpi_scale;
  const std::size_t first_menu_index =
      m_chrome_layout.first_overflow_menu_index;
  if (first_menu_index >= menus.size()) {
    return geometry;
  }
  float popup_width = 160.0F * m_chrome_layout.dpi_scale;
  for (std::size_t menu_index = first_menu_index; menu_index < menus.size();
       ++menu_index) {
    const UI::Components::Menu &menu = menus[menu_index];
    popup_width =
        std::max(popup_width, static_cast<float>(menu.label.size()) * 7.0F *
                                      m_chrome_layout.dpi_scale +
                                  34.0F * m_chrome_layout.dpi_scale);
  }
  geometry.item_count =
      std::min(menus.size() - first_menu_index, geometry.item_bounds.size());
  const float floating_gap = 4.0F * m_chrome_layout.dpi_scale;
  const float window_right = m_chrome_layout.titlebar_bounds.right();
  float popup_x = m_chrome_layout.overflow_menu_bounds.x;
  if (popup_x + popup_width > window_right - 8.0F * m_chrome_layout.dpi_scale) {
    popup_x = window_right - popup_width - 8.0F * m_chrome_layout.dpi_scale;
  }
  if (popup_x < 8.0F * m_chrome_layout.dpi_scale) {
    popup_x = 8.0F * m_chrome_layout.dpi_scale;
  }

  geometry.bounds = {
      popup_x,
      m_chrome_layout.titlebar_bounds.bottom() + floating_gap,
      popup_width,
      row_height * static_cast<float>(geometry.item_count) + vertical_padding * 2.0F,
  };
  for (std::size_t index = 0; index < geometry.item_count; ++index) {
    geometry.item_bounds[index] = {
        geometry.bounds.x,
        geometry.bounds.y + vertical_padding + row_height * static_cast<float>(index),
        geometry.bounds.width,
        row_height,
    };
  }
  return geometry;
}

Win32Window::PopupMenuGeometry Win32Window::calculate_popup_menu_geometry(
    std::size_t menu_index) const noexcept {
  PopupMenuGeometry geometry;
  const std::span<const UI::Components::Menu> menus =
      UI::Components::get_window_menus();
  if (menu_index >= menus.size()) {
    return geometry;
  }

  const UI::Components::Menu &menu = menus[menu_index];
  const float row_height = 24.0F * m_chrome_layout.dpi_scale;
  const float separator_height = 7.0F * m_chrome_layout.dpi_scale;
  const float vertical_padding = 4.0F * m_chrome_layout.dpi_scale;
  float popup_width = 220.0F * m_chrome_layout.dpi_scale;
  for (const UI::Components::MenuItem &item : menu.items) {
    if (item.separator) {
      continue;
    }
    float item_width = static_cast<float>(item.label.size()) * 7.0F *
                           m_chrome_layout.dpi_scale +
                       42.0F * m_chrome_layout.dpi_scale;
    if (!item.shortcut.empty()) {
      item_width += static_cast<float>(item.shortcut.size()) * 7.0F *
                        m_chrome_layout.dpi_scale +
                    30.0F * m_chrome_layout.dpi_scale;
    }
    popup_width = std::max(popup_width, item_width);
  }
  popup_width = std::min(popup_width, 460.0F * m_chrome_layout.dpi_scale);

  const float window_right = m_chrome_layout.titlebar_bounds.right();
  const float floating_gap = 4.0F * m_chrome_layout.dpi_scale;

  if (m_menu_overlay_open) {
    const MenuOverlayGeometry root_geometry = calculate_menu_overlay_geometry();
    const std::size_t root_index =
        menu_index - m_chrome_layout.first_overflow_menu_index;
    if (root_index >= root_geometry.item_count) {
      return geometry;
    }
    geometry.bounds.x =
        root_geometry.bounds.right() + 2.0F * m_chrome_layout.dpi_scale;
    geometry.bounds.y = root_geometry.item_bounds[root_index].y;
  } else {
    geometry.bounds.x = m_chrome_layout.overflow_menu_bounds.x;
    geometry.bounds.y = m_chrome_layout.titlebar_bounds.bottom() + floating_gap;
    for (std::size_t index = 0; index < m_chrome_layout.visible_menu_count;
         ++index) {
      if (m_chrome_layout.menu_regions[index].menu_index == menu_index) {
        geometry.bounds.x = m_chrome_layout.menu_regions[index].bounds.x;
        break;
      }
    }
    if (menu_index == 10)
      geometry.bounds.x = m_chrome_layout.compiler_bounds.x;
    else if (menu_index == 11)
      geometry.bounds.x = m_chrome_layout.platform_bounds.x;
    else if (menu_index == 12)
      geometry.bounds.x = m_chrome_layout.binary_bounds.x;
    else if (menu_index == 13) // Gear menu -> Align to right of button and expand to left
      geometry.bounds.x = m_chrome_layout.gear_bounds.right() - popup_width;
    else if (menu_index == 14) // Ellipsis menu -> Align to right of button and expand to left
      geometry.bounds.x = m_chrome_layout.ellipsis_bounds.right() - popup_width;
  }

  // Prevent right-edge overflow or clipping (facing left away from window border)
  if (geometry.bounds.x + popup_width > window_right - 8.0F * m_chrome_layout.dpi_scale) {
    geometry.bounds.x = window_right - popup_width - 8.0F * m_chrome_layout.dpi_scale;
  }
  if (geometry.bounds.x < 8.0F * m_chrome_layout.dpi_scale) {
    geometry.bounds.x = 8.0F * m_chrome_layout.dpi_scale;
  }

  geometry.bounds.width = popup_width;
  geometry.item_count =
      std::min(menu.items.size(), geometry.item_bounds.size());
  float current_y = geometry.bounds.y + vertical_padding;
  for (std::size_t index = 0; index < geometry.item_count; ++index) {
    const float item_height =
        menu.items[index].separator ? separator_height : row_height;
    geometry.item_bounds[index] = {
        geometry.bounds.x,
        current_y,
        geometry.bounds.width,
        item_height,
    };
    current_y += item_height;
  }
  geometry.bounds.height = (current_y + vertical_padding) - geometry.bounds.y;
  return geometry;
}

std::optional<std::size_t>
Win32Window::get_menu_overlay_index(float point_x,
                                    float point_y) const noexcept {
  if (!m_menu_overlay_open) {
    return std::nullopt;
  }
  const MenuOverlayGeometry geometry = calculate_menu_overlay_geometry();
  for (std::size_t index = 0; index < geometry.item_count; ++index) {
    if (geometry.item_bounds[index].contains(point_x, point_y)) {
      return m_chrome_layout.first_overflow_menu_index + index;
    }
  }
  return std::nullopt;
}

std::optional<std::size_t>
Win32Window::get_popup_menu_item_index(float point_x,
                                       float point_y) const noexcept {
  if (!m_open_menu_index) {
    return std::nullopt;
  }
  const PopupMenuGeometry geometry =
      calculate_popup_menu_geometry(*m_open_menu_index);
  const std::span<const UI::Components::Menu> menus =
      UI::Components::get_window_menus();
  if (*m_open_menu_index >= menus.size()) {
    return std::nullopt;
  }
  for (std::size_t index = 0; index < geometry.item_count; ++index) {
    if (!menus[*m_open_menu_index].items[index].separator &&
        geometry.item_bounds[index].contains(point_x, point_y)) {
      return index;
    }
  }
  return std::nullopt;
}

bool Win32Window::is_popup_menu_item_enabled(std::size_t menu_index,
                                             std::size_t item_index) const {
  const std::span<const UI::Components::Menu> menus =
      UI::Components::get_window_menus();
  if (menu_index >= menus.size() ||
      item_index >= menus[menu_index].items.size()) {
    return false;
  }
  const UI::Components::MenuItem &item = menus[menu_index].items[item_index];
  if (item.separator || item.command_id.empty()) {
    return false;
  }
  if (const std::optional<bool> editor_enabled =
          m_workspace_renderer.is_editor_command_enabled(item.command_id)) {
    return *editor_enabled;
  }
  return !m_command_state_query_callback ||
         m_command_state_query_callback(item.command_id).enabled;
}

void Win32Window::draw_menu_overlay(HDC device_context) const {
  const bool drawing_root = m_menu_overlay_open;
  if (!drawing_root && !m_open_menu_index) {
    return;
  }

  const float scale = m_chrome_layout.dpi_scale;
  const int radius = std::max(round_to_int(6.0F * scale), 5);
  const auto draw_panel = [&](const UI::Rect &panel_bounds) {
    if (panel_bounds.is_empty()) {
      return;
    }
    // macOS ultra-thin, soft diffuse ambient shadow
    Utility::for_each_shadow_layer(
        panel_bounds, static_cast<float>(radius), scale,
        Utility::macos_card_shadows,
        [&](const UI::Rect &layer_rect, const UI::Theme::Color &color,
            float layer_radius) {
          fill_rounded_rectangle(device_context, layer_rect, color,
                                 static_cast<int>(layer_radius));
        });

    // macOS Dark Acrylic Card
    fill_rounded_rectangle(device_context, panel_bounds, m_theme.panel_background, radius);

    // macOS Hairline Border (subtle translucent border)
    const RECT native_bounds = to_native_rect(panel_bounds);
    HPEN border_pen =
        CreatePen(PS_SOLID, 1, RGB(70, 72, 80));
    HGDIOBJ previous_brush = SelectObject(device_context, GetStockObject(NULL_BRUSH));
    HGDIOBJ previous_pen = SelectObject(device_context, border_pen);
    RoundRect(device_context, native_bounds.left, native_bounds.top,
              native_bounds.right, native_bounds.bottom, radius * 2,
              radius * 2);
    SelectObject(device_context, previous_pen);
    SelectObject(device_context, previous_brush);
    DeleteObject(border_pen);
  };

  if (drawing_root) {
    draw_panel(calculate_menu_overlay_geometry().bounds);
  }
  if (m_open_menu_index) {
    draw_panel(calculate_popup_menu_geometry(*m_open_menu_index).bounds);
  }

  const std::span<const UI::Components::Menu> menus =
      UI::Components::get_window_menus();
  if (drawing_root) {
    const MenuOverlayGeometry geometry = calculate_menu_overlay_geometry();
    for (std::size_t index = 0; index < geometry.item_count; ++index) {
      const std::size_t menu_index =
          m_chrome_layout.first_overflow_menu_index + index;
      const bool hovered =
          m_hovered_menu_index == menu_index || m_open_menu_index == menu_index;
      UI::Rect item_bounds = geometry.item_bounds[index];
      if (hovered) {
        UI::Rect hover_bounds = item_bounds;
        hover_bounds.x += 5.0F * scale;
        hover_bounds.width -= 10.0F * scale;
        hover_bounds.y += 1.0F * scale;
        hover_bounds.height -= 2.0F * scale;
        fill_rounded_rectangle(device_context, hover_bounds, UI::Theme::Color{53, 132, 228, 240},
                               std::max(round_to_int(4.0F * scale), 3));
      }
      RECT text_bounds = to_native_rect(item_bounds);
      text_bounds.left += round_to_int(12.0F * scale);
      SetTextColor(device_context,
                   to_color_ref(hovered ? UI::Theme::Color{255, 255, 255, 255}
                                        : m_theme.text_primary));
      DrawTextW(device_context, utf8_to_wide(menus[menu_index].label).c_str(),
                -1, &text_bounds,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX |
                    DT_END_ELLIPSIS);

      const int chevron_x = round_to_int(item_bounds.right() - 14.0F * scale);
      const int chevron_y =
          round_to_int(item_bounds.y + item_bounds.height * 0.5F);
      m_workspace_renderer.draw_svg_icon(
          device_context, "Assets/icons/chevron-right.svg", chevron_x,
          chevron_y, std::max(round_to_int(11.0F * scale), 9),
          hovered ? UI::Theme::Color{255, 255, 255, 255} : m_workspace_renderer.m_palette.text_muted,
          hovered ? m_theme.accent : m_theme.panel_background);
    }
  }

  if (!m_open_menu_index) {
    return;
  }
  const std::size_t menu_index = *m_open_menu_index;
  if (menu_index >= menus.size()) {
    return;
  }
  const PopupMenuGeometry geometry = calculate_popup_menu_geometry(menu_index);
  const UI::Components::Menu &menu = menus[menu_index];
  for (std::size_t index = 0; index < geometry.item_count; ++index) {
    const UI::Components::MenuItem &item = menu.items[index];
    const UI::Rect &item_bounds = geometry.item_bounds[index];
    if (item.separator) {
      fill_rectangle(device_context,
                     UI::Rect{
                         item_bounds.x + 10.0F * scale,
                         item_bounds.y + item_bounds.height * 0.5F,
                         item_bounds.width - 20.0F * scale,
                         1.0F,
                     },
                     m_theme.titlebar_border);
      continue;
    }

    const bool enabled = is_popup_menu_item_enabled(menu_index, index);
    const bool hovered = enabled && m_hovered_popup_item_index == index;
    if (hovered) {
      UI::Rect hover_bounds = item_bounds;
      hover_bounds.x += 5.0F * scale;
      hover_bounds.width -= 10.0F * scale;
      hover_bounds.y += 1.0F * scale;
      hover_bounds.height -= 2.0F * scale;
      fill_rounded_rectangle(device_context, hover_bounds, UI::Theme::Color{53, 132, 228, 240},
                             std::max(round_to_int(4.0F * scale), 3));
    }

    RECT text_bounds = to_native_rect(item_bounds);
    text_bounds.left += round_to_int(24.0F * scale);
    if (!item.shortcut.empty()) {
      text_bounds.right -= round_to_int(static_cast<float>(item.shortcut.size()) * 7.0F * scale + 24.0F * scale);
    }
    SetTextColor(device_context,
                 to_color_ref(!enabled  ? m_theme.text_secondary
                              : hovered ? UI::Theme::Color{255, 255, 255, 255}
                                        : m_theme.text_primary));
    DrawTextW(
        device_context, utf8_to_wide(item.label).c_str(), -1, &text_bounds,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

    if (!item.shortcut.empty()) {
      RECT shortcut_bounds = to_native_rect(item_bounds);
      shortcut_bounds.right -= round_to_int(14.0F * scale);
      SetTextColor(
          device_context,
          to_color_ref(!enabled  ? m_theme.text_secondary
                       : hovered ? UI::Theme::Color{255, 255, 255, 220}
                                 : m_theme.text_secondary));
      DrawTextW(device_context, utf8_to_wide(item.shortcut).c_str(), -1,
                &shortcut_bounds,
                DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    if (m_command_state_query_callback && !item.command_id.empty()) {
      const auto state = m_command_state_query_callback(item.command_id);
      if (state.checked) {
        HPEN check_pen = CreatePen(
            PS_SOLID, 2,
            to_color_ref(hovered ? UI::Theme::Color{255, 255, 255, 255}
                                 : m_theme.text_primary));
        HGDIOBJ prev_pen = SelectObject(device_context, check_pen);
        const int check_x = round_to_int(item_bounds.x + 10.0F * scale);
        const int check_y =
            round_to_int(item_bounds.y + item_bounds.height * 0.5F);
        MoveToEx(device_context, check_x, check_y, nullptr);
        LineTo(device_context, check_x + 3, check_y + 3);
        LineTo(device_context, check_x + 8, check_y - 3);
        SelectObject(device_context, prev_pen);
        DeleteObject(check_pen);
      }
    }
  }
}

void Win32Window::show_about_dialog() {
  close_menu_overlay();
  m_about_modal.open();
  if (m_window_handle) {
    InvalidateRect(m_window_handle, nullptr, FALSE);
  }
}

bool Win32Window::is_modal_active() const {
  return m_about_modal.is_visible();
}

void Win32Window::copy_to_clipboard(const std::string& text) {
  if (OpenClipboard(m_window_handle)) {
    EmptyClipboard();
    std::wstring wide = utf8_to_wide(text);
    const std::size_t bytes = (wide.length() + 1) * sizeof(wchar_t);
    HGLOBAL hGlob = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hGlob) {
      void* ptr = GlobalLock(hGlob);
      if (ptr) {
        memcpy(ptr, wide.c_str(), bytes);
        GlobalUnlock(hGlob);
        SetClipboardData(CF_UNICODETEXT, hGlob);
      }
    }
    CloseClipboard();
  }
}

void apply_backdrop_blur(HDC device_context, int width, int height, float scale) {
  if (width <= 0 || height <= 0 || device_context == nullptr) return;

  // 4x downscale gives an immediate smooth area-average pre-filter and huge speedup
  const int down_w = std::max(width / 4, 1);
  const int down_h = std::max(height / 4, 1);

  BITMAPINFO bmi{};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = down_w;
  bmi.bmiHeader.biHeight = -down_h; // top-down
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

  HDC memDC = CreateCompatibleDC(device_context);
  void* bits = nullptr;
  HBITMAP hBmp = CreateDIBSection(device_context, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (!hBmp || !bits) {
    if (memDC) DeleteDC(memDC);
    return;
  }

  HGDIOBJ oldBmp = SelectObject(memDC, hBmp);

  SetStretchBltMode(memDC, HALFTONE);
  StretchBlt(memDC, 0, 0, down_w, down_h, device_context, 0, 0, width, height, SRCCOPY);

  auto* pixels = static_cast<uint32_t*>(bits);
  const int total_pixels = down_w * down_h;

  const int radius = std::max(static_cast<int>(14.0f * scale), 8);
  std::vector<uint32_t> temp(total_pixels);

  // Fast O(1) sliding-window box blur passes (3 passes mathematically converge to true Gaussian Blur)
  auto blur_horizontal = [&](const uint32_t* src, uint32_t* dst, int r) {
    const float inv_w = 1.0f / static_cast<float>(2 * r + 1);
    for (int y = 0; y < down_h; ++y) {
      const int row = y * down_w;
      int sum_r = 0, sum_g = 0, sum_b = 0;

      for (int x = -r; x <= r; ++x) {
        const int clamped_x = std::clamp(x, 0, down_w - 1);
        const uint32_t px = src[row + clamped_x];
        sum_r += (px >> 16) & 0xFF;
        sum_g += (px >> 8) & 0xFF;
        sum_b += px & 0xFF;
      }

      for (int x = 0; x < down_w; ++x) {
        dst[row + x] = (static_cast<uint32_t>(sum_r * inv_w) << 16) |
                       (static_cast<uint32_t>(sum_g * inv_w) << 8) |
                       static_cast<uint32_t>(sum_b * inv_w);

        const int remove_x = std::clamp(x - r, 0, down_w - 1);
        const int add_x = std::clamp(x + r + 1, 0, down_w - 1);
        const uint32_t p_remove = src[row + remove_x];
        const uint32_t p_add = src[row + add_x];

        sum_r += ((p_add >> 16) & 0xFF) - ((p_remove >> 16) & 0xFF);
        sum_g += ((p_add >> 8) & 0xFF) - ((p_remove >> 8) & 0xFF);
        sum_b += (p_add & 0xFF) - (p_remove & 0xFF);
      }
    }
  };

  auto blur_vertical = [&](const uint32_t* src, uint32_t* dst, int r) {
    const float inv_h = 1.0f / static_cast<float>(2 * r + 1);
    for (int x = 0; x < down_w; ++x) {
      int sum_r = 0, sum_g = 0, sum_b = 0;

      for (int y = -r; y <= r; ++y) {
        const int clamped_y = std::clamp(y, 0, down_h - 1);
        const uint32_t px = src[clamped_y * down_w + x];
        sum_r += (px >> 16) & 0xFF;
        sum_g += (px >> 8) & 0xFF;
        sum_b += px & 0xFF;
      }

      for (int y = 0; y < down_h; ++y) {
        dst[y * down_w + x] = (static_cast<uint32_t>(sum_r * inv_h) << 16) |
                              (static_cast<uint32_t>(sum_g * inv_h) << 8) |
                              static_cast<uint32_t>(sum_b * inv_h);

        const int remove_y = std::clamp(y - r, 0, down_h - 1);
        const int add_y = std::clamp(y + r + 1, 0, down_h - 1);
        const uint32_t p_remove = src[remove_y * down_w + x];
        const uint32_t p_add = src[add_y * down_w + x];

        sum_r += ((p_add >> 16) & 0xFF) - ((p_remove >> 16) & 0xFF);
        sum_g += ((p_add >> 8) & 0xFF) - ((p_remove >> 8) & 0xFF);
        sum_b += (p_add & 0xFF) - (p_remove & 0xFF);
      }
    }
  };

  // Pass 1
  blur_horizontal(pixels, temp.data(), radius);
  blur_vertical(temp.data(), pixels, radius);

  // Pass 2
  blur_horizontal(pixels, temp.data(), radius);
  blur_vertical(temp.data(), pixels, radius);

  // Pass 3 (Gaussian convergence for Windows 10 Acrylic)
  blur_horizontal(pixels, temp.data(), radius);
  blur_vertical(temp.data(), pixels, radius);

  // Windows 10 Taskbar / Start Menu Acrylic Compositing: Saturation Boost + Deep Acrylic Tint + Frosted Glass Noise
  const float tint_r = 16.0f, tint_g = 18.0f, tint_b = 24.0f;
  const float tint_a = 0.35f;
  const float saturation = 1.40f;

  for (int y = 0; y < down_h; ++y) {
    const int row = y * down_w;
    for (int x = 0; x < down_w; ++x) {
      const uint32_t px = pixels[row + x];
      float br = static_cast<float>((px >> 16) & 0xFF);
      float bg = static_cast<float>((px >> 8) & 0xFF);
      float bb = static_cast<float>(px & 0xFF);

      float lum = br * 0.2126f + bg * 0.7152f + bb * 0.0722f;
      float sr = lum + (br - lum) * saturation;
      float sg = lum + (bg - lum) * saturation;
      float sb = lum + (bb - lum) * saturation;

      float mr = sr * (1.0f - tint_a) + tint_r * tint_a;
      float mg = sg * (1.0f - tint_a) + tint_g * tint_a;
      float mb = sb * (1.0f - tint_a) + tint_b * tint_a;

      float noise = std::fmod(52.9829189f * std::fmod(static_cast<float>(x) * 0.06711056f + static_cast<float>(y) * 0.00583715f, 1.0f), 1.0f) - 0.5f;
      float grain = noise * 3.5f;

      uint32_t fr = static_cast<uint32_t>(std::clamp(mr + grain, 0.0f, 255.0f));
      uint32_t fg = static_cast<uint32_t>(std::clamp(mg + grain, 0.0f, 255.0f));
      uint32_t fb = static_cast<uint32_t>(std::clamp(mb + grain, 0.0f, 255.0f));

      pixels[row + x] = (fr << 16) | (fg << 8) | fb;
    }
  }

  SetStretchBltMode(device_context, HALFTONE);
  StretchBlt(device_context, 0, 0, width, height, memDC, 0, 0, down_w, down_h, SRCCOPY);

  SelectObject(memDC, oldBmp);
  DeleteObject(hBmp);
  DeleteDC(memDC);
}

void Win32Window::draw_about_modal(HDC device_context, int client_width, int client_height) {
  if (!m_about_modal.is_visible()) {
    return;
  }

  const float scale = static_cast<float>(m_dpi) / 96.0F;
  const UI::Rect viewport{0.0F, 0.0F, static_cast<float>(client_width), static_cast<float>(client_height)};
  const auto layout = m_about_modal.calculate_layout(viewport, scale);

  // 1. Draw heavy Windows 10 Acrylic Taskbar frosted glass backdrop overlay
  apply_backdrop_blur(device_context, client_width, client_height, scale);

  // 2. Dialog Container (Fluent / Acrylic Dark Card with elevation drop shadow)
  const UI::Theme::Color dialog_bg{28, 29, 34, 255};
  const UI::Theme::Color border_col{70, 74, 88, 255};

  // Floating elevation drop shadow behind modal
  struct ShadowLayer {
    float dx;
    float dy;
    float spread;
    uint8_t alpha;
  };
  const ShadowLayer shadow_layers[] = {
    {0.0F, 12.0F, 24.0F, 22},
    {0.0F,  8.0F, 16.0F, 34},
    {0.0F,  4.0F,  8.0F, 50},
    {0.0F,  1.5F,  2.0F, 70},
  };
  for (const auto &layer : shadow_layers) {
    const float spread = layer.spread * scale;
    const UI::Rect layer_rect{
        layout.base_layout.dialog_bounds.x - spread + layer.dx * scale,
        layout.base_layout.dialog_bounds.y - spread + layer.dy * scale,
        layout.base_layout.dialog_bounds.width + spread * 2.0F,
        layout.base_layout.dialog_bounds.height + spread * 2.0F,
    };
    fill_rounded_rectangle(device_context, layer_rect,
                           UI::Theme::Color{0, 0, 0, layer.alpha},
                           static_cast<int>(8.0F * scale + spread));
  }

  fill_rounded_rectangle(device_context, layout.base_layout.dialog_bounds, dialog_bg, static_cast<int>(8.0F * scale));

  // Border outline
  {
    HPEN border_pen = CreatePen(PS_SOLID, 1, to_color_ref(border_col));
    HGDIOBJ old_pen = SelectObject(device_context, border_pen);
    HGDIOBJ old_brush = SelectObject(device_context, GetStockObject(NULL_BRUSH));
    RoundRect(device_context,
              round_to_int(layout.base_layout.dialog_bounds.x),
              round_to_int(layout.base_layout.dialog_bounds.y),
              round_to_int(layout.base_layout.dialog_bounds.right()),
              round_to_int(layout.base_layout.dialog_bounds.bottom()),
              static_cast<int>(16.0F * scale), static_cast<int>(16.0F * scale));
    SelectObject(device_context, old_brush);
    SelectObject(device_context, old_pen);
    DeleteObject(border_pen);
  }

  // 3. Draw Header Icon from Assets/icons/zenvra_logo.png
  const int logo_center_x = round_to_int(layout.logo_bounds.x + layout.logo_bounds.width * 0.5F);
  const int logo_center_y = round_to_int(layout.logo_bounds.y + layout.logo_bounds.height * 0.5F);
  const int logo_size = round_to_int(layout.logo_bounds.width);

  m_workspace_renderer.draw_png_icon(
      device_context, "zenvra_logo.png",
      logo_center_x, logo_center_y, logo_size, dialog_bg);

  // 4. Header Text (Title + Subtitle in clean neutral white and grey)
  HFONT title_font = Utility::AntialiasedText::create_cleartype_font(
      L"Segoe UI", round_to_int(15.0F * scale), FW_SEMIBOLD);
  HGDIOBJ prev_font = SelectObject(device_context, title_font);

  RECT head_r = to_native_rect(layout.headline_bounds);
  Utility::AntialiasedText::draw_text(
      device_context, L"Zenvra Development Studio", head_r, RGB(242, 244, 248));

  HFONT sub_font = Utility::AntialiasedText::create_cleartype_font(
      L"Segoe UI", round_to_int(11.0F * scale), FW_NORMAL);
  SelectObject(device_context, sub_font);

  RECT edition_r = to_native_rect(layout.edition_bounds);
  Utility::AntialiasedText::draw_text(
      device_context, L"Community & Pro Edition v0.1.0 (Windows - 64Bit)", edition_r, RGB(145, 150, 162));

  // 5. Subtle Separator Line
  const int sep_y = round_to_int(layout.base_layout.dialog_bounds.y + 74.0F * scale);
  const int sep_x1 = round_to_int(layout.base_layout.dialog_bounds.x + 24.0F * scale);
  const int sep_x2 = round_to_int(layout.base_layout.dialog_bounds.right() - 24.0F * scale);
  HPEN sep_pen = CreatePen(PS_SOLID, 1, RGB(48, 52, 60));
  HGDIOBJ old_sep = SelectObject(device_context, sep_pen);
  MoveToEx(device_context, sep_x1, sep_y, nullptr);
  LineTo(device_context, sep_x2, sep_y);
  SelectObject(device_context, old_sep);
  DeleteObject(sep_pen);

  // 6. Specs Table rows (Key-Value)
  HFONT spec_font = Utility::AntialiasedText::create_cleartype_font(
      L"JetBrainsMonoNL Nerd Font", round_to_int(11.0F * scale), FW_NORMAL);
  if (spec_font == nullptr) {
    spec_font = Utility::AntialiasedText::create_cleartype_font(
        L"Hack", round_to_int(11.0F * scale), FW_NORMAL);
  }
  if (spec_font == nullptr) {
    spec_font = Utility::AntialiasedText::create_cleartype_font(
        L"Segoe UI", round_to_int(11.0F * scale), FW_NORMAL);
  }
  SelectObject(device_context, spec_font);

  const auto& specs = m_about_modal.get_specs();
  const int key_col_width = round_to_int(125.0F * scale);
  for (std::size_t i = 0; i < specs.size() && i < layout.spec_row_bounds.size(); ++i) {
    RECT row_r = to_native_rect(layout.spec_row_bounds[i]);
    RECT key_r = row_r;
    key_r.right = key_r.left + key_col_width;
    RECT val_r = row_r;
    val_r.left = key_r.right + round_to_int(10.0F * scale);

    std::wstring wide_key = utf8_to_wide(specs[i].key + ":");
    std::wstring wide_val = utf8_to_wide(specs[i].value);

    // Key in muted neutral grey
    Utility::AntialiasedText::draw_text(
        device_context, wide_key, key_r, RGB(138, 144, 155));

    // Value in clean readable light grey/white
    Utility::AntialiasedText::draw_text(
        device_context, wide_val, val_r, RGB(220, 224, 232), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
  }

  DeleteObject(spec_font);
  DeleteObject(sub_font);
  DeleteObject(title_font);

  // 7. Action Buttons: "Copy" and "OK"
  // "Copy" button (secondary neutral button)
  const bool copy_h = m_about_modal.is_copy_hovered();
  const UI::Theme::Color copy_bg = copy_h ? UI::Theme::Color{58, 62, 72, 255} : UI::Theme::Color{44, 48, 56, 255};
  fill_rounded_rectangle(device_context, layout.copy_button_bounds, copy_bg, static_cast<int>(4.0F * scale));
  RECT copy_r = to_native_rect(layout.copy_button_bounds);
  draw_centered_text(device_context, L"Copy", copy_r, UI::Theme::Color{215, 220, 228, 255});

  // "OK" button (primary VS Code accent button #0E639C)
  const bool ok_h = m_about_modal.is_ok_hovered();
  const UI::Theme::Color ok_bg = ok_h ? UI::Theme::Color{17, 119, 187, 255} : UI::Theme::Color{14, 99, 156, 255};
  fill_rounded_rectangle(device_context, layout.ok_button_bounds, ok_bg, static_cast<int>(4.0F * scale));
  RECT ok_r = to_native_rect(layout.ok_button_bounds);
  draw_centered_text(device_context, L"OK", ok_r, UI::Theme::Color{255, 255, 255, 255});

  // Top-right Close Button
  if (m_about_modal.is_close_hovered()) {
    fill_rounded_rectangle(device_context, layout.close_button_bounds, m_theme.close_hover, static_cast<int>(4.0F * scale));
  }
  const int about_cx = round_to_int(layout.close_button_bounds.x + layout.close_button_bounds.width * 0.5F);
  const int about_cy = round_to_int(layout.close_button_bounds.y + layout.close_button_bounds.height * 0.5F);
  const int about_icon_sz = std::max(round_to_int(12.0F * scale), 10);
  m_workspace_renderer.draw_svg_icon(
      device_context, "Assets/icons/diagnostic-error.svg", about_cx, about_cy, about_icon_sz,
      m_about_modal.is_close_hovered() ? UI::Theme::Color{255, 255, 255, 255} : UI::Theme::Color{175, 180, 190, 255},
      m_about_modal.is_close_hovered() ? m_theme.close_hover : m_theme.panel_background);

  SelectObject(device_context, prev_font);
}

void Win32Window::update_hovered_control(UI::Chrome::WindowControl control) {
  if (m_hovered_control != control) {
    m_hovered_control = control;
    InvalidateRect(m_window_handle, nullptr, FALSE);
  }
}

std::wstring Win32Window::utf8_to_wide(std::string_view text) {
  return Utility::utf8_to_wide(text).value_or(std::wstring{});
}

void Win32Window::close_explorer_context_menu() {
  if (m_explorer_context_menu.visible) {
    m_explorer_context_menu.visible = false;
    m_explorer_context_menu.hovered_index.reset();
  }
}

void Win32Window::show_explorer_context_menu(const std::filesystem::path& target_path, int client_x, int client_y) {
  close_menu_overlay();
  m_explorer_context_menu.visible = true;
  m_explorer_context_menu.target_path = target_path;
  m_explorer_context_menu.hovered_index.reset();

  enum ContextCmd : uint32_t {
    CmdNewFile = 50001,
    CmdNewFolder = 50002,
    CmdOpenToSide = 50003,
    CmdReveal = 50004,
    CmdOpenTerminal = 50005,
    CmdCut = 50006,
    CmdCopy = 50007,
    CmdPaste = 50008,
    CmdCopyPath = 50009,
    CmdCopyRelativePath = 50010,
    CmdRename = 50011,
    CmdDelete = 50012,
  };

  m_explorer_context_menu.items = {
    {"New File...", "", false, CmdNewFile},
    {"New Folder...", "", false, CmdNewFolder},
    {"", "", true, 0},
    {"Open to the Side", "Ctrl+Enter", false, CmdOpenToSide},
    {"Reveal in File Explorer", "Shift+Alt+R", false, CmdReveal},
    {"Open in Integrated Terminal", "", false, CmdOpenTerminal},
    {"", "", true, 0},
    {"Cut", "Ctrl+X", false, CmdCut},
    {"Copy", "Ctrl+C", false, CmdCopy},
    {"Paste", "Ctrl+V", false, CmdPaste},
    {"", "", true, 0},
    {"Copy Path", "Shift+Alt+C", false, CmdCopyPath},
    {"Copy Relative Path", "Ctrl+K Ctrl+Shift+C", false, CmdCopyRelativePath},
    {"", "", true, 0},
    {"Rename...", "F2", false, CmdRename},
    {"Delete", "Delete", false, CmdDelete}
  };

  const float scale = m_chrome_layout.dpi_scale;
  const float row_height = 24.0F * scale;
  const float sep_height = 7.0F * scale;
  const float vertical_padding = 4.0F * scale;
  float total_h = vertical_padding * 2.0F;
  float popup_width = 210.0F * scale;

  for (const auto& item : m_explorer_context_menu.items) {
    if (item.separator) {
      total_h += sep_height;
      continue;
    }
    total_h += row_height;
    float item_width = static_cast<float>(item.label.size()) * 7.0F * scale + 42.0F * scale;
    if (!item.shortcut.empty()) {
      item_width += static_cast<float>(item.shortcut.size()) * 7.0F * scale + 30.0F * scale;
    }
    popup_width = std::max(popup_width, item_width);
  }
  popup_width = std::min(popup_width, 420.0F * scale);

  RECT client_rect{};
  GetClientRect(m_window_handle, &client_rect);
  const float client_w = static_cast<float>(client_rect.right - client_rect.left);
  const float client_h = static_cast<float>(client_rect.bottom - client_rect.top);

  float menu_x = static_cast<float>(client_x);
  float menu_y = static_cast<float>(client_y);

  if (menu_x + popup_width > client_w - 8.0F * scale) {
    menu_x = std::max(8.0F * scale, client_w - popup_width - 8.0F * scale);
  }
  if (menu_y + total_h > client_h - 8.0F * scale) {
    menu_y = std::max(8.0F * scale, client_h - total_h - 8.0F * scale);
  }

  m_explorer_context_menu.bounds = {menu_x, menu_y, popup_width, total_h};
  m_explorer_context_menu.item_bounds.clear();

  float curr_y = menu_y + vertical_padding;
  for (const auto& item : m_explorer_context_menu.items) {
    if (item.separator) {
      m_explorer_context_menu.item_bounds.push_back({menu_x, curr_y, popup_width, sep_height});
      curr_y += sep_height;
    } else {
      m_explorer_context_menu.item_bounds.push_back({menu_x, curr_y, popup_width, row_height});
      curr_y += row_height;
    }
  }

  InvalidateRect(m_window_handle, nullptr, FALSE);
}

void Win32Window::draw_explorer_context_menu(HDC device_context) const {
  if (!m_explorer_context_menu.visible) return;

  const float scale = m_chrome_layout.dpi_scale;
  const auto& bounds = m_explorer_context_menu.bounds;
  const int radius = std::max(round_to_int(6.0F * scale), 5);

  // 1. macOS ultra-thin, soft diffuse ambient shadow
  struct ShadowLayer {
    float dx;
    float dy;
    float spread;
    uint8_t alpha;
  };
  const ShadowLayer shadow_layers[] = {
    {0.0F, 8.0F, 16.0F,  8}, // Ambient ultra-soft atmospheric haze
    {0.0F, 5.0F,  9.0F, 14}, // Soft outer glow
    {0.0F, 3.0F,  4.5F, 22}, // Soft mid-shadow
    {0.0F, 1.5F,  2.0F, 32}, // Soft near-shadow
    {0.0F, 0.5F,  0.8F, 42}, // Ultra-thin contact shadow
  };
  for (const auto &layer : shadow_layers) {
    const float spread = layer.spread * scale;
    const UI::Rect layer_rect{
        bounds.x - spread + layer.dx * scale,
        bounds.y - spread + layer.dy * scale,
        bounds.width + spread * 2.0F,
        bounds.height + spread * 2.0F,
    };
    fill_rounded_rectangle(device_context, layer_rect,
                           UI::Theme::Color{0, 0, 0, layer.alpha},
                           static_cast<int>(static_cast<float>(radius) + spread));
  }

  // 2. macOS Dark Acrylic Card
  fill_rounded_rectangle(device_context, bounds, m_theme.panel_background, radius);

  // 3. macOS Hairline Border (subtle translucent border)
  const RECT native_bounds = to_native_rect(bounds);
  HPEN border_pen = CreatePen(PS_SOLID, 1, RGB(70, 72, 80));
  HGDIOBJ previous_brush = SelectObject(device_context, GetStockObject(NULL_BRUSH));
  HGDIOBJ previous_pen = SelectObject(device_context, border_pen);
  RoundRect(device_context, native_bounds.left, native_bounds.top,
            native_bounds.right, native_bounds.bottom, radius * 2, radius * 2);
  SelectObject(device_context, previous_pen);
  SelectObject(device_context, previous_brush);
  DeleteObject(border_pen);

  // 4. Draw items exactly matching draw_menu_overlay
  for (std::size_t i = 0; i < m_explorer_context_menu.items.size() && i < m_explorer_context_menu.item_bounds.size(); ++i) {
    const auto& item = m_explorer_context_menu.items[i];
    const auto& item_bounds = m_explorer_context_menu.item_bounds[i];

    if (item.separator) {
      fill_rectangle(device_context,
                     UI::Rect{
                         item_bounds.x + 10.0F * scale,
                         item_bounds.y + item_bounds.height * 0.5F,
                         item_bounds.width - 20.0F * scale,
                         1.0F,
                     },
                     m_theme.titlebar_border);
      continue;
    }

    const bool hovered = (m_explorer_context_menu.hovered_index && *m_explorer_context_menu.hovered_index == i);
    if (hovered) {
      UI::Rect hover_bounds = item_bounds;
      hover_bounds.x += 5.0F * scale;
      hover_bounds.width -= 10.0F * scale;
      hover_bounds.y += 1.0F * scale;
      hover_bounds.height -= 2.0F * scale;
      fill_rounded_rectangle(device_context, hover_bounds, UI::Theme::Color{53, 132, 228, 240},
                             std::max(round_to_int(4.0F * scale), 3));
    }

    RECT text_bounds = to_native_rect(item_bounds);
    text_bounds.left += round_to_int(24.0F * scale);
    if (!item.shortcut.empty()) {
      text_bounds.right -= round_to_int(static_cast<float>(item.shortcut.size()) * 7.0F * scale + 24.0F * scale);
    }
    SetTextColor(device_context,
                 to_color_ref(hovered ? UI::Theme::Color{255, 255, 255, 255}
                                      : m_theme.text_primary));
    DrawTextW(
        device_context, utf8_to_wide(item.label).c_str(), -1, &text_bounds,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

    if (!item.shortcut.empty()) {
      RECT shortcut_bounds = to_native_rect(item_bounds);
      shortcut_bounds.right -= round_to_int(14.0F * scale);
      SetTextColor(
          device_context,
          to_color_ref(hovered ? UI::Theme::Color{255, 255, 255, 220}
                               : m_theme.text_secondary));
      DrawTextW(device_context, utf8_to_wide(item.shortcut).c_str(), -1,
                &shortcut_bounds,
                DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
  }
}

void Win32Window::execute_explorer_context_menu_item(std::size_t item_index) {
  if (item_index >= m_explorer_context_menu.items.size()) return;
  const auto& item = m_explorer_context_menu.items[item_index];
  if (item.separator) return;

  const auto target_path = m_explorer_context_menu.target_path;

  enum ContextCmd : uint32_t {
    CmdNewFile = 50001,
    CmdNewFolder = 50002,
    CmdOpenToSide = 50003,
    CmdReveal = 50004,
    CmdOpenTerminal = 50005,
    CmdCut = 50006,
    CmdCopy = 50007,
    CmdPaste = 50008,
    CmdCopyPath = 50009,
    CmdCopyRelativePath = 50010,
    CmdRename = 50011,
    CmdDelete = 50012,
  };

  switch (item.command_id) {
  case CmdNewFile: {
    const auto root = m_workspace_renderer.m_tool_sidebar.get_model().get_workspace_root();
    const std::string proj_name = root.filename().string();
    m_workspace_renderer.get_add_item_dialog().open(m_window_handle, target_path, proj_name, [this](const std::string& name, const std::string& initial_content) {
      std::filesystem::path created_p;
      if (m_workspace_renderer.m_tool_sidebar.get_model().create_file(name, created_p)) {
        if (!initial_content.empty()) {
          std::ofstream out(created_p, std::ios::binary);
          if (out.is_open()) {
            out.write(initial_content.data(), initial_content.size());
            out.close();
          }
        }
        static_cast<void>(m_workspace_renderer.open_file(created_p));
      }
      InvalidateRect(m_window_handle, nullptr, FALSE);
    });
    InvalidateRect(m_window_handle, nullptr, FALSE);
    break;
  }
  case CmdNewFolder: {
    m_workspace_renderer.get_prompt_modal().open_new_folder(target_path, [this](const std::string& name) {
      std::filesystem::path created_p;
      static_cast<void>(m_workspace_renderer.m_tool_sidebar.get_model().create_directory(name, created_p));
      InvalidateRect(m_window_handle, nullptr, FALSE);
    });
    InvalidateRect(m_window_handle, nullptr, FALSE);
    break;
  }
  case CmdOpenToSide: {
    std::error_code ec;
    if (!std::filesystem::is_directory(target_path, ec)) {
      static_cast<void>(m_workspace_renderer.open_file(target_path));
    }
    break;
  }
  case CmdReveal: {
    const std::wstring full_w = target_path.wstring();
    const std::wstring params = L"/select,\"" + full_w + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", params.c_str(), nullptr, SW_SHOWNORMAL);
    break;
  }
  case CmdOpenTerminal: {
    std::error_code ec;
    std::filesystem::path term_dir = std::filesystem::is_directory(target_path, ec) ? target_path : target_path.parent_path();
    if (!m_workspace_renderer.m_terminal_panel.is_visible()) {
      static_cast<void>(m_workspace_renderer.m_terminal_panel.toggle());
    }
    std::string cd_cmd = "cd \"" + term_dir.string() + "\"\r";
    static_cast<void>(m_workspace_renderer.m_terminal_panel.handle_text_input(cd_cmd));
    break;
  }
  case CmdCut:
  case CmdCopy:
  case CmdCopyPath: {
    const std::wstring path_w = target_path.wstring();
    if (OpenClipboard(m_window_handle)) {
      EmptyClipboard();
      const size_t bytes = (path_w.length() + 1) * sizeof(wchar_t);
      HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
      if (hMem) {
        memcpy(GlobalLock(hMem), path_w.c_str(), bytes);
        GlobalUnlock(hMem);
        SetClipboardData(CF_UNICODETEXT, hMem);
      }
      CloseClipboard();
    }
    break;
  }
  case CmdCopyRelativePath: {
    const auto root = m_workspace_renderer.m_tool_sidebar.get_model().get_workspace_root();
    std::error_code ec;
    const auto rel = std::filesystem::relative(target_path, root, ec);
    const std::wstring path_w = ec ? target_path.wstring() : rel.wstring();
    if (OpenClipboard(m_window_handle)) {
      EmptyClipboard();
      const size_t bytes = (path_w.length() + 1) * sizeof(wchar_t);
      HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
      if (hMem) {
        memcpy(GlobalLock(hMem), path_w.c_str(), bytes);
        GlobalUnlock(hMem);
        SetClipboardData(CF_UNICODETEXT, hMem);
      }
      CloseClipboard();
    }
    break;
  }
  case CmdRename: {
    m_workspace_renderer.get_prompt_modal().open_rename(target_path, [this, target_path](const std::string& new_name) {
      std::filesystem::path new_p;
      if (m_workspace_renderer.m_tool_sidebar.get_model().rename_item(target_path, new_name, new_p)) {
        static_cast<void>(m_workspace_renderer.m_text_editor.close_file(target_path));
        static_cast<void>(m_workspace_renderer.open_file(new_p));
      }
      InvalidateRect(m_window_handle, nullptr, FALSE);
    });
    InvalidateRect(m_window_handle, nullptr, FALSE);
    break;
  }
  case CmdDelete: {
    m_workspace_renderer.get_prompt_modal().open_delete(target_path, [this, target_path]() {
      static_cast<void>(m_workspace_renderer.m_text_editor.close_file(target_path));
      static_cast<void>(m_workspace_renderer.m_tool_sidebar.get_model().delete_item(target_path));
      InvalidateRect(m_window_handle, nullptr, FALSE);
    });
    InvalidateRect(m_window_handle, nullptr, FALSE);
    break;
  }
  default:
    break;
  }
}

void Win32Window::show_system_menu(int screen_x, int screen_y) {
  HMENU system_menu = GetSystemMenu(m_window_handle, FALSE);
  if (system_menu == nullptr) {
    return;
  }

  const bool maximized = is_maximized();
  const bool minimized = is_minimized();

  EnableMenuItem(system_menu, SC_RESTORE, MF_BYCOMMAND | (maximized || minimized ? MF_ENABLED : MF_GRAYED));
  EnableMenuItem(system_menu, SC_MOVE, MF_BYCOMMAND | (!maximized && !minimized ? MF_ENABLED : MF_GRAYED));
  EnableMenuItem(system_menu, SC_SIZE, MF_BYCOMMAND | (!maximized && !minimized ? MF_ENABLED : MF_GRAYED));
  EnableMenuItem(system_menu, SC_MINIMIZE, MF_BYCOMMAND | (!minimized ? MF_ENABLED : MF_GRAYED));
  EnableMenuItem(system_menu, SC_MAXIMIZE, MF_BYCOMMAND | (!maximized ? MF_ENABLED : MF_GRAYED));
  EnableMenuItem(system_menu, SC_CLOSE, MF_BYCOMMAND | MF_ENABLED);

  SetMenuDefaultItem(system_menu, maximized ? SC_RESTORE : SC_MAXIMIZE, FALSE);

  const UINT command = TrackPopupMenuEx(
      system_menu,
      TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_RIGHTBUTTON,
      screen_x, screen_y, m_window_handle, nullptr);

  if (command > 0) {
    PostMessageW(m_window_handle, WM_SYSCOMMAND, command, 0);
  }
}

} // namespace Zenvra::Platform::Win32
