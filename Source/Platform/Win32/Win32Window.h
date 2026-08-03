#pragma once

#include "Platform/IPlatformWindow.h"
#include "Platform/Win32/Components/Menubar.h"
#include "UI/Chrome/WindowChromeLayout.h"
#include "UI/Theme/StudioTheme.h"

#include <windows.h>

#include <string>
#include <optional>

namespace Zenvra::Platform::Win32
{

class Win32Window final : public IPlatformWindow
{
public:
    explicit Win32Window(const WindowSpecification& specification);
    ~Win32Window() override;

    Win32Window(const Win32Window&) = delete;
    Win32Window& operator=(const Win32Window&) = delete;

    [[nodiscard]] bool initialize() override;
    void show() override;
    void poll_events() override;
    [[nodiscard]] bool should_close() const override;

    void minimize() override;
    void maximize() override;
    void restore() override;
    void request_close() override;

    [[nodiscard]] bool is_maximized() const override;
    [[nodiscard]] bool is_minimized() const override;
    [[nodiscard]] bool is_focused() const override;
    [[nodiscard]] const WindowCapabilities& get_capabilities() const noexcept override;
    [[nodiscard]] void* get_native_handle() const noexcept override;

    void set_custom_chrome_enabled(bool enabled) override;
    void set_titlebar_hit_test_callback(TitlebarHitTestCallback callback) override;
    void set_command_invoked_callback(CommandInvokedCallback callback) override;
    void set_command_state_query_callback(CommandStateQueryCallback callback) override;

private:
    static LRESULT CALLBACK window_proc(HWND window_handle, UINT message, WPARAM w_param, LPARAM l_param);
    LRESULT handle_message(HWND window_handle, UINT message, WPARAM w_param, LPARAM l_param);
    [[nodiscard]] LRESULT hit_test_non_client(LPARAM l_param);
    [[nodiscard]] LRESULT hit_test_resize_border(POINT client_position) const;
    void paint_custom_chrome();
    void refresh_chrome_layout();
    void refresh_ui_font();
    void show_menu(std::size_t menu_index);
    void show_overflow_menu();
    void update_hovered_control(UI::Chrome::WindowControl control);
    static std::wstring utf8_to_wide(std::string_view text);

    HWND m_window_handle = nullptr;
    HINSTANCE m_instance_handle = nullptr;
    WindowSpecification m_specification;
    WindowCapabilities m_capabilities;
    std::wstring m_window_title;
    bool m_should_close = false;
    bool m_custom_chrome_enabled = false;
    TitlebarHitTestCallback m_titlebar_hit_test_callback;
    Components::Menubar m_menubar;
    UI::Theme::StudioTheme m_theme = UI::Theme::StudioTheme::zenvra_dark();
    UI::Chrome::WindowChromeLayout m_chrome_layout_engine;
    UI::Chrome::WindowChromeLayoutResult m_chrome_layout;
    UI::Chrome::WindowControl m_hovered_control = UI::Chrome::WindowControl::NoControl;
    UI::Chrome::WindowControl m_pressed_control = UI::Chrome::WindowControl::NoControl;
    std::optional<std::size_t> m_hovered_menu_index;
    bool m_overflow_menu_hovered = false;
    bool m_menu_pointer_tracking = false;
    bool m_command_center_hovered = false;
    HFONT m_ui_font = nullptr;
    UINT m_dpi = 96;

    static constexpr const wchar_t* window_class_name = L"ZenvraPlatformWindow";
};

} // namespace Zenvra::Platform::Win32
