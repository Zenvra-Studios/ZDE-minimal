#import <Cocoa/Cocoa.h>
#include "CocoaWindow.h"

// Objective-C Delegate to handle window events
@interface ZenvraWindowDelegate : NSObject <NSWindowDelegate>
@property (assign) bool* shouldClosePtr;
@end

@implementation ZenvraWindowDelegate
- (BOOL)windowShouldClose:(id)sender {
    if (self.shouldClosePtr) {
        *self.shouldClosePtr = true;
    }
    return YES;
}
@end

namespace Zenvra {
namespace Platform {
namespace Cocoa {

CocoaWindow::CocoaWindow(const std::string& title, int width, int height)
    : m_window(nullptr), m_delegate(nullptr), m_title(title), m_width(width), m_height(height), m_should_close(false)
{
}

CocoaWindow::~CocoaWindow()
{
    if (m_window) {
        NSWindow* window = (__bridge_transfer NSWindow*)m_window;
        [window close];
    }
    if (m_delegate) {
        ZenvraWindowDelegate* delegate = (__bridge_transfer ZenvraWindowDelegate*)m_delegate;
        delegate = nil;
    }
}

bool CocoaWindow::initialize()
{
    NSRect rect = NSMakeRect(0, 0, m_width, m_height);
    NSWindowStyleMask style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable;
    
    NSWindow* window = [[NSWindow alloc] initWithContentRect:rect
                                                   styleMask:style
                                                     backing:NSBackingStoreBuffered
                                                       defer:NO];
    
    NSString* ns_title = [NSString stringWithUTF8String:m_title.c_str()];
    [window setTitle:ns_title];
    [window center];
    
    ZenvraWindowDelegate* delegate = [[ZenvraWindowDelegate alloc] init];
    delegate.shouldClosePtr = &m_should_close;
    [window setDelegate:delegate];
    
    // Store objects using C++ void pointers with ARC bridging
    m_window = (__bridge_retained void*)window;
    m_delegate = (__bridge_retained void*)delegate;

    return true;
}

void CocoaWindow::show()
{
    if (m_window) {
        NSWindow* window = (__bridge NSWindow*)m_window;
        [window makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
    }
}

void CocoaWindow::update()
{
    // Pump the runloop manually so we don't block the C++ custom main loop
    NSEvent* event;
    do {
        event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                   untilDate:nil
                                      inMode:NSDefaultRunLoopMode
                                     dequeue:YES];
        if (event) {
            [NSApp sendEvent:event];
            [NSApp updateWindows];
        }
    } while (event != nil);
}

void CocoaWindow::on_resize(int width, int height)
{
    // Can be overridden by subclasses or events
}

} // namespace Cocoa
} // namespace Platform
} // namespace Zenvra
