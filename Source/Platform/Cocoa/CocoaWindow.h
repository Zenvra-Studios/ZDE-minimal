#pragma once

#include "Platform/IPlatformWindow.h"

namespace Zenvra::Platform::Cocoa
{

class CocoaWindow final : public IPlatformWindow
{
public:
    explicit CocoaWindow(const WindowSpecification& specification);
    ~CocoaWindow() override;

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
    void* m_window_handle = nullptr;
    void* m_delegate = nullptr;
    WindowSpecification m_specification;
    WindowCapabilities m_capabilities;
    bool m_should_close = false;
    TitlebarHitTestCallback m_titlebar_hit_test_callback;
    CommandInvokedCallback m_command_invoked_callback;
    CommandStateQueryCallback m_command_state_query_callback;
};

} // namespace Zenvra::Platform::Cocoa
