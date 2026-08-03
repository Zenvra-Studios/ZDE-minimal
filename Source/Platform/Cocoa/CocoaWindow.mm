#import <Cocoa/Cocoa.h>

#include "Platform/Cocoa/CocoaWindow.h"

#include "Platform/Cocoa/Runtime/CocoaContext.h"

#include <utility>

@interface ZenvraWindowDelegate : NSObject <NSWindowDelegate>
@property(assign) bool* should_close_pointer;
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
@end

namespace Zenvra::Platform::Cocoa
{

CocoaWindow::CocoaWindow(const WindowSpecification& specification)
    : m_specification(specification)
{
    m_capabilities.native_resize = true;
    m_capabilities.native_snap = true;
    m_capabilities.per_monitor_dpi = true;
}

CocoaWindow::~CocoaWindow()
{
    if (m_window_handle != nullptr)
    {
        NSWindow* window = (__bridge_transfer NSWindow*)m_window_handle;
        [window close];
    }
    if (m_delegate != nullptr)
    {
        ZenvraWindowDelegate* delegate = (__bridge_transfer ZenvraWindowDelegate*)m_delegate;
        delegate = nil;
    }
}

bool CocoaWindow::initialize()
{
    if (!Runtime::CocoaContext::initialize())
    {
        return false;
    }

    const NSRect rectangle = NSMakeRect(0, 0, m_specification.width, m_specification.height);
    const NSWindowStyleMask style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
        NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable;

    NSWindow* window = [[NSWindow alloc] initWithContentRect:rectangle
                                                   styleMask:style
                                                     backing:NSBackingStoreBuffered
                                                       defer:NO];
    NSString* title = [NSString stringWithUTF8String:m_specification.title.c_str()];
    [window setTitle:title];
    [window center];

    ZenvraWindowDelegate* delegate = [[ZenvraWindowDelegate alloc] init];
    delegate.should_close_pointer = &m_should_close;
    [window setDelegate:delegate];

    m_window_handle = (__bridge_retained void*)window;
    m_delegate = (__bridge_retained void*)delegate;
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
    NSEvent* event = nil;
    do
    {
        event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                   untilDate:nil
                                      inMode:NSDefaultRunLoopMode
                                     dequeue:YES];
        if (event != nil)
        {
            [NSApp sendEvent:event];
            [NSApp updateWindows];
        }
    } while (event != nil);
}

bool CocoaWindow::should_close() const
{
    return m_should_close;
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
    (void)enabled;
}

void CocoaWindow::set_titlebar_hit_test_callback(TitlebarHitTestCallback callback)
{
    m_titlebar_hit_test_callback = std::move(callback);
}

void CocoaWindow::set_command_invoked_callback(CommandInvokedCallback callback)
{
    m_command_invoked_callback = std::move(callback);
}

void CocoaWindow::set_command_state_query_callback(CommandStateQueryCallback callback)
{
    m_command_state_query_callback = std::move(callback);
}

} // namespace Zenvra::Platform::Cocoa
