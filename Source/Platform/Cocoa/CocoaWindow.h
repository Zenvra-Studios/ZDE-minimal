#pragma once

#include "Platform/IPlatformWindow.h"
#include "Platform/Cocoa/Components/CocoaChromeRenderer.h"
#include "UI/Chrome/WindowChromeLayout.h"

#include <string>
#include <memory>
#include <functional>

namespace Zenvra::Platform::Cocoa
{

class CocoaWindow final : public IPlatformWindow
{
public:
    explicit CocoaWindow(const WindowSpecification& specification);
    ~CocoaWindow() override;
    
    // Non-copyable/movable
    CocoaWindow(const CocoaWindow&) = delete;
    CocoaWindow& operator=(const CocoaWindow&) = delete;

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

    [[nodiscard]] bool open_project_folder() override;
    void toggle_terminal() override;

    // Workspace & Chrome integration
    [[nodiscard]] Components::CocoaChromeRenderer& get_renderer() { return m_renderer; }

private:
    void refresh_chrome_layout();
    void center_traffic_lights(void* window_handle, CGFloat strip_height);

    WindowSpecification m_specification;
    WindowCapabilities m_capabilities;
    bool m_should_close = false;
    bool m_custom_chrome_enabled = false;
    
    void* m_window_handle = nullptr;
    void* m_delegate = nullptr;
    void* m_content_view = nullptr; // ZenvraContentView

    TitlebarHitTestCallback m_titlebar_hit_test_callback;
    CommandInvokedCallback m_command_invoked_callback;
    CommandStateQueryCallback m_command_state_query_callback;

    UI::Chrome::WindowChromeLayout m_chrome_layout_engine;
    UI::Chrome::WindowChromeLayoutResult m_chrome_layout;
    
    Components::CocoaChromeRenderer m_renderer;
};

} // namespace Zenvra::Platform::Cocoa
