#import <Cocoa/Cocoa.h>
#include "CocoaContext.h"
#include <iostream>

namespace Zenvra {
namespace Platform {
namespace Cocoa {
namespace Runtime {

bool CocoaContext::initialize()
{
    // Initialize the shared NSApplication instance
    [NSApplication sharedApplication];
    
    // Set the app to be a regular app (shows up in Dock and has a Menu Bar)
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    
    // Create a basic default menu so the app has a name in the menu bar and a Quit option
    id menubar = [[NSMenu alloc] new];
    id app_menu_item = [[NSMenuItem alloc] new];
    [menubar addItem:app_menu_item];
    [NSApp setMainMenu:menubar];
    
    id app_menu = [[NSMenu alloc] new];
    id app_name = [[NSProcessInfo processInfo] processName];
    id quit_title = [@"Quit " stringByAppendingString:app_name];
    id quit_menu_item = [[NSMenuItem alloc] initWithTitle:quit_title action:@selector(terminate:) keyEquivalent:@"q"];
    [app_menu addItem:quit_menu_item];
    [app_menu_item setSubmenu:app_menu];

    // Finish launching setup
    [NSApp finishLaunching];
    
    return true;
}

void CocoaContext::shutdown()
{
    // macOS normally handles cleanup upon application termination natively.
}

} // namespace Runtime
} // namespace Cocoa
} // namespace Platform
} // namespace Zenvra
