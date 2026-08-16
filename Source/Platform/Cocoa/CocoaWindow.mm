#import <Cocoa/Cocoa.h>

#include "Language/LanguageServerManager.h"
#include "Platform/Cocoa/CocoaWindow.h"

#include "Platform/Cocoa/Runtime/CocoaContext.h"
#include "Platform/Cocoa/Runtime/CocoaMenuBridge.h"
#import "Platform/Cocoa/Components/ZenvraContentView.h"

#include <filesystem>
#include <iostream>
#include <utility>
#include <functional>
#include <algorithm>

@interface ZenvraWindowDelegate : NSObject <NSWindowDelegate>
{
    std::function<void()> _onResize;
    std::function<void(bool)> _onFullscreenChange;
}
@property(assign) bool* should_close_pointer;
- (void)setOnResize:(std::function<void()>)callback;
- (void)setOnFullscreenChange:(std::function<void(bool)>)callback;
@end

@implementation ZenvraWindowDelegate
- (BOOL)windowShouldClose:(id)sender
{
    (void)sender;
    if (self.should_close_pointer != nullptr)
    {
        *self.should_close_pointer = true;
    }
    return YES;
}

- (void)setOnResize:(std::function<void()>)callback
{
    _onResize = std::move(callback);
}

- (void)setOnFullscreenChange:(std::function<void(bool)>)callback
{
    _onFullscreenChange = std::move(callback);
}

- (void)windowDidResize:(NSNotification *)notification
{
    (void)notification;
    if (_onResize)
    {
        _onResize();
    }
}

- (void)windowWillEnterFullScreen:(NSNotification *)notification
{
    (void)notification;
    if (_onFullscreenChange)
    {
        _onFullscreenChange(true);
    }
}

- (void)windowDidEnterFullScreen:(NSNotification *)notification
{
    (void)notification;
    if (_onFullscreenChange)
    {
        _onFullscreenChange(true);
    }
}

- (void)windowWillExitFullScreen:(NSNotification *)notification
{
    (void)notification;
    if (_onFullscreenChange)
    {
        _onFullscreenChange(false);
    }
}

- (void)windowDidExitFullScreen:(NSNotification *)notification
{
    (void)notification;
    if (_onFullscreenChange)
    {
        _onFullscreenChange(false);
    }
}
@end

namespace Zenvra::Platform::Cocoa
{

CocoaWindow::CocoaWindow(const WindowSpecification& specification)
    : m_specification(specification)
{
    m_capabilities.custom_chrome = true;
    m_capabilities.native_resize = true;
    m_capabilities.native_snap = true;
    m_capabilities.per_monitor_dpi = true;
}

CocoaWindow::~CocoaWindow()
{
    Language::LanguageServerManager::instance().set_diagnostics_callback(nullptr);
    Runtime::CocoaMenuBridge::set_command_callback(nullptr);
    Runtime::CocoaMenuBridge::set_command_state_query_callback(nullptr);

    if (m_content_view != nullptr)
    {
        ZenvraContentView* view = (__bridge ZenvraContentView*)m_content_view;
        [view setRenderer:nullptr];
        [view setCommandInvokedCallback:nullptr];
        [view setCommandCallback:nullptr];
        m_content_view = nullptr;
    }
    if (m_delegate != nullptr)
    {
        ZenvraWindowDelegate* delegate = (__bridge ZenvraWindowDelegate*)m_delegate;
        delegate.should_close_pointer = nullptr;
        [delegate setOnResize:nullptr];
        [delegate setOnFullscreenChange:nullptr];
    }
    if (m_window_handle != nullptr)
    {
        NSWindow* window = (__bridge NSWindow*)m_window_handle;
        [window setDelegate:nil];
        [window setContentView:nil];
        [window close];
        [window release];
        m_window_handle = nullptr;
    }
    if (m_delegate != nullptr)
    {
        ZenvraWindowDelegate* delegate = (__bridge ZenvraWindowDelegate*)m_delegate;
        [delegate release];
        m_delegate = nullptr;
    }
}

bool CocoaWindow::initialize()
{
    if (!Runtime::CocoaContext::initialize())
    {
        return false;
    }

    m_custom_chrome_enabled =
        m_specification.custom_chrome_enabled && m_capabilities.custom_chrome;

    const NSRect rectangle = NSMakeRect(0, 0, m_specification.width, m_specification.height);
    // Clamp the initial size to the visible screen so the toolbar is never
    // wider than the display (e.g. 1600pt spec on a 1366pt screen).
    NSRect clamped_rectangle = rectangle;
    const NSRect screen_visible = [[NSScreen mainScreen] visibleFrame];
    if (screen_visible.size.width > 0.0 && screen_visible.size.height > 0.0)
    {
        clamped_rectangle.size.width =
            std::min(rectangle.size.width, screen_visible.size.width);
        clamped_rectangle.size.height =
            std::min(rectangle.size.height, screen_visible.size.height);
    }
    NSWindowStyleMask style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
        NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable;
    if (m_custom_chrome_enabled)
    {
        style |= NSWindowStyleMaskFullSizeContentView;
    }

    NSWindow* window = [[NSWindow alloc] initWithContentRect:clamped_rectangle
                                                   styleMask:style
                                                     backing:NSBackingStoreBuffered
                                                       defer:NO];
    NSString* title = [NSString stringWithUTF8String:m_specification.title.c_str()];
    [window setTitle:title];
    [window setBackgroundColor:[NSColor colorWithSRGBRed:30.0/255.0 green:31.0/255.0 blue:34.0/255.0 alpha:1.0]];
    [window center];

    if (m_custom_chrome_enabled)
    {
        // Configure the window before attaching the content view so the
        // native traffic lights stay at their default position and the
        // content view extends underneath the titlebar.
        window.titlebarAppearsTransparent = YES;
        window.titleVisibility = NSWindowTitleHidden;
    }

    ZenvraWindowDelegate* delegate = [[ZenvraWindowDelegate alloc] init];
    delegate.should_close_pointer = &m_should_close;
    [delegate setOnResize:[this]() {
        refresh_chrome_layout();
    }];
    [delegate setOnFullscreenChange:[this](bool fs) {
        m_renderer.set_fullscreen(fs);
        refresh_chrome_layout();
    }];
    [window setDelegate:delegate];

    ZenvraContentView* content_view = [[ZenvraContentView alloc] initWithFrame:clamped_rectangle];
    [content_view setRenderer:&m_renderer];
    // The application wires the command callbacks before the window is
    // initialized (Application.cpp), so forward the stored ones now that the
    // content view exists.
    [content_view setCommandInvokedCallback:m_command_invoked_callback];
    [content_view setCommandCallback:m_command_state_query_callback];
    [window setContentView:content_view];

    m_window_handle = (__bridge_retained void*)window;
    m_delegate = (__bridge_retained void*)delegate;
    m_content_view = (__bridge_retained void*)content_view;

    // Initialize the Cocoa/workspace renderer exactly like the Win32 backend
    // does (Win32Window.cpp:311). Without this, fonts, palette colors and the
    // icon asset root stay uninitialized, so the text editor buffer content,
    // the explorer sidebar and every toolkit icon fail to render.
    const CGFloat dpi_scale = [window backingScaleFactor];
    if (!m_renderer.initialize(
            static_cast<float>(dpi_scale), UI::Theme::StudioTheme::zenvra_dark()))
    {
        std::cerr << "Fatal error: the Cocoa workspace renderer could not be "
                     "initialized.\n";
        return false;
    }

    Language::LanguageServerManager::instance().set_diagnostics_callback(
        [this](const std::string& uri, const std::vector<Language::Protocol::Diagnostic>& diags) {
            m_renderer.get_text_editor().on_diagnostics_updated(uri, diags);
            if (m_content_view != nullptr)
            {
                ZenvraContentView* view = (__bridge ZenvraContentView*)m_content_view;
                [view setNeedsDisplay:YES];
            }
        });

    set_custom_chrome_enabled(m_specification.custom_chrome_enabled);
    return true;
}

void CocoaWindow::show()
{
    NSWindow* window = (__bridge NSWindow*)m_window_handle;
    [window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

void CocoaWindow::poll_events()
{
    @autoreleasepool {
        NSEvent* event = nil;
        while ((event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                           untilDate:[NSDate distantPast]
                                              inMode:NSDefaultRunLoopMode
                                             dequeue:YES]) != nil)
        {
            [NSApp sendEvent:event];
        }
        [NSApp updateWindows];

        if (m_window_handle != nullptr)
        {
            NSWindow* window = (__bridge NSWindow*)m_window_handle;
            const bool is_fs = ([window styleMask] & NSWindowStyleMaskFullScreen) != 0;
            if (m_renderer.is_fullscreen() != is_fs)
            {
                m_renderer.set_fullscreen(is_fs);
            }
        }

        // Poll terminal sessions, editor carets and other animations; redraw
        // when anything changed (mirrors Win32Window.cpp:344).
        if (m_renderer.tick_animations() && m_content_view != nullptr)
        {
            ZenvraContentView* view = (__bridge ZenvraContentView*)m_content_view;
            [view setNeedsDisplay:YES];
        }
    }
}

void CocoaWindow::toggle_terminal()
{
    if (m_renderer.toggle_terminal() && m_content_view != nullptr)
    {
        ZenvraContentView* view = (__bridge ZenvraContentView*)m_content_view;
        [view updateLayout:m_chrome_layout];
        [view setNeedsDisplay:YES];
    }
}

void CocoaWindow::toggle_shader_sandbox()
{
    if (m_renderer.toggle_shader_sandbox() && m_content_view != nullptr)
    {
        ZenvraContentView* view = (__bridge ZenvraContentView*)m_content_view;
        [view updateLayout:m_chrome_layout];
        [view setNeedsDisplay:YES];
    }
}

bool CocoaWindow::should_close() const
{
    return m_should_close;
}

bool CocoaWindow::open_project_folder()
{
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    [panel setTitle:@"Open Project Folder"];
    [panel setPrompt:@"Open"];
    [panel setCanChooseFiles:NO];
    [panel setCanChooseDirectories:YES];
    [panel setAllowsMultipleSelection:NO];
    [panel setCanCreateDirectories:NO];

    if ([panel runModal] != NSModalResponseOK)
    {
        return true;
    }
    NSURL* url = [[panel URLs] firstObject];
    if (url == nil || ![url isFileURL])
    {
        return true;
    }
    const std::filesystem::path root = [[url path] UTF8String];
    Language::LanguageServerManager::instance().set_workspace_root(root);
    if (m_renderer.set_workspace_root(root))
    {
        if (m_content_view != nullptr)
        {
            ZenvraContentView* view = (__bridge ZenvraContentView*)m_content_view;
            [view setNeedsDisplay:YES];
        }
    }
    return true;
}

void CocoaWindow::minimize()
{
    [(__bridge NSWindow*)m_window_handle miniaturize:nil];
}

void CocoaWindow::maximize()
{
    NSWindow* window = (__bridge NSWindow*)m_window_handle;
    if (![window isZoomed])
    {
        [window zoom:nil];
    }
}

void CocoaWindow::restore()
{
    NSWindow* window = (__bridge NSWindow*)m_window_handle;
    if ([window isMiniaturized])
    {
        [window deminiaturize:nil];
    }
    if ([window isZoomed])
    {
        [window zoom:nil];
    }
}

void CocoaWindow::request_close()
{
    [(__bridge NSWindow*)m_window_handle performClose:nil];
}

void CocoaWindow::toggle_fullscreen()
{
    if (m_window_handle == nullptr) return;
    NSWindow* window = (__bridge NSWindow*)m_window_handle;
    [window toggleFullScreen:nil];
}

bool CocoaWindow::is_fullscreen() const
{
    if (m_window_handle == nullptr) return false;
    NSWindow* window = (__bridge NSWindow*)m_window_handle;
    return ([window styleMask] & NSWindowStyleMaskFullScreen) != 0;
}

bool CocoaWindow::is_maximized() const
{
    return [(__bridge NSWindow*)m_window_handle isZoomed];
}

bool CocoaWindow::is_minimized() const
{
    return [(__bridge NSWindow*)m_window_handle isMiniaturized];
}

bool CocoaWindow::is_focused() const
{
    return [(__bridge NSWindow*)m_window_handle isKeyWindow];
}

const WindowCapabilities& CocoaWindow::get_capabilities() const noexcept
{
    return m_capabilities;
}

void* CocoaWindow::get_native_handle() const noexcept
{
    return m_window_handle;
}

void CocoaWindow::set_custom_chrome_enabled(bool enabled)
{
    m_custom_chrome_enabled = enabled && m_capabilities.custom_chrome;
    
    if (m_window_handle != nullptr)
    {
        NSWindow* window = (__bridge NSWindow*)m_window_handle;
        if (m_custom_chrome_enabled)
        {
            window.titlebarAppearsTransparent = YES;
            window.titleVisibility = NSWindowTitleHidden;
            window.styleMask |= NSWindowStyleMaskFullSizeContentView;
        }
        else
        {
            window.titlebarAppearsTransparent = NO;
            window.titleVisibility = NSWindowTitleVisible;
            window.styleMask &= ~NSWindowStyleMaskFullSizeContentView;
        }
    }
    
    refresh_chrome_layout();
}

void CocoaWindow::refresh_chrome_layout()
{
    if (m_window_handle == nullptr)
    {
        return;
    }
    
    NSWindow* window = (__bridge NSWindow*)m_window_handle;
    const bool is_fs = ([window styleMask] & NSWindowStyleMaskFullScreen) != 0;
    m_renderer.set_fullscreen(is_fs);

    NSRect content_rect = [window contentRectForFrameRect:[window frame]];
    float client_width = static_cast<float>(content_rect.size.width);
    float dpi_scale = static_cast<float>([window backingScaleFactor]);
    
    UI::Chrome::WindowChromeLayoutOptions options;
    options.show_window_controls = false; // Let macOS native traffic lights handle this
    options.show_titlebar = m_custom_chrome_enabled;
    options.hamburger_only = false;
    options.chrome_style = m_custom_chrome_enabled ? UI::Chrome::ChromeStyle::FullCustom : UI::Chrome::ChromeStyle::NativeMacOS;
    if (m_custom_chrome_enabled) {
        options.titlebar_height = 36.0F; // Taller custom strip; traffic lights are re-centered below
        options.force_all_menus = true; // Keep every menu inline; never show a hamburger icon on macOS
        options.show_menu_labels = false; // The native macOS menu bar owns the menus; no labels in the titlebar
        if (!is_fs) {
            center_traffic_lights(window, options.titlebar_height * dpi_scale);
        }

        NSButton* zoom_btn = [window standardWindowButton:NSWindowZoomButton];
        if (zoom_btn && !is_fs) {
            options.left_padding = static_cast<float>(NSMaxX([zoom_btn frame]) + 16.0) * dpi_scale;
        } else {
            options.left_padding = is_fs ? 0.0F : 72.0F * dpi_scale;
        }
    }
    m_chrome_layout = m_chrome_layout_engine.calculate(client_width, dpi_scale, options);

    if (m_content_view != nullptr)
    {
        ZenvraContentView* view = (__bridge ZenvraContentView*)m_content_view;
        [view updateLayout:m_chrome_layout];
        [view setNeedsDisplay:YES];
    }
}



void CocoaWindow::center_traffic_lights(void* window_handle, CGFloat strip_height)
{
    // Keep the native traffic lights vertically centered inside the custom
    // titlebar strip. Horizontal positions are shifted slightly to the right.
    NSWindow* window = (__bridge NSWindow*)window_handle;
    const NSWindowButton buttons[] = {
        NSWindowCloseButton,
        NSWindowMiniaturizeButton,
        NSWindowZoomButton,
    };
    
    CGFloat base_x = 12.5F; // Shifted slightly to the left to match VS Code's traffic light position
    for (NSWindowButton button_type : buttons)
    {
        NSButton* button = [window standardWindowButton:button_type];
        if (button == nil) continue;
        NSView* container = [button superview];
        if (container == nil) continue;
        const NSRect container_frame = [container frame];
        const CGFloat button_height = [button frame].size.height;
        // The strip and the button container share the top edge of the
        // window; center the button on the strip's midpoint in content
        // view coordinates.
        const CGFloat center_in_content = strip_height * 0.5F;
        const CGFloat center_in_container =
            [container isFlipped]
                ? center_in_content - container_frame.origin.y
                : container_frame.size.height -
                      (center_in_content - container_frame.origin.y);
        const CGFloat desired_y = center_in_container - button_height * 0.5F;
        
        NSRect frame = [button frame];
        bool changed = false;
        
        if (std::abs(frame.origin.y - desired_y) > 0.5)
        {
            frame.origin.y = desired_y;
            changed = true;
        }
        
        if (std::abs(frame.origin.x - base_x) > 0.5)
        {
            frame.origin.x = base_x;
            changed = true;
        }
        
        if (changed)
        {
            [button setFrameOrigin:frame.origin];
        }
        
        base_x += 20.0F; // Standard spacing between macOS window buttons
    }
}

void CocoaWindow::set_titlebar_hit_test_callback(TitlebarHitTestCallback callback)
{
    m_titlebar_hit_test_callback = std::move(callback);
}

void CocoaWindow::set_command_invoked_callback(CommandInvokedCallback callback)
{
    m_command_invoked_callback = std::move(callback);
    auto dispatcher = [this](std::string_view command_id) {
        const std::optional<bool> editor_result =
            m_renderer.handle_editor_command(command_id);
        if (editor_result)
        {
            if (m_content_view != nullptr)
            {
                ZenvraContentView* view = (__bridge ZenvraContentView*)m_content_view;
                [view setNeedsDisplay:YES];
            }
            return;
        }
        if (m_command_invoked_callback)
        {
            m_command_invoked_callback(command_id);
        }
    };
    Runtime::CocoaMenuBridge::set_command_callback(dispatcher);
    if (m_content_view != nullptr)
    {
        ZenvraContentView* view = (__bridge ZenvraContentView*)m_content_view;
        [view setCommandInvokedCallback:dispatcher];
    }
}

void CocoaWindow::set_command_state_query_callback(CommandStateQueryCallback callback)
{
    m_command_state_query_callback = std::move(callback);
    auto query_dispatcher = [this](std::string_view command_id) {
        const std::optional<bool> editor_enabled =
            m_renderer.is_editor_command_enabled(command_id);
        if (editor_enabled)
        {
            return CommandPresentationState{*editor_enabled, false};
        }
        return m_command_state_query_callback
                   ? m_command_state_query_callback(command_id)
                   : CommandPresentationState{true, false};
    };
    Runtime::CocoaMenuBridge::set_command_state_query_callback(query_dispatcher);
    if (m_content_view != nullptr)
    {
        ZenvraContentView* view = (__bridge ZenvraContentView*)m_content_view;
        [view setCommandCallback:query_dispatcher];
    }
}

} // namespace Zenvra::Platform::Cocoa
