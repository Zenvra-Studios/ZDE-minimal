#import <Cocoa/Cocoa.h>
#include "CocoaContext.h"
#include "CocoaMenuBridge.h"
#include <iostream>

@interface ZenvraAppDelegate : NSObject <NSApplicationDelegate>
@end

@implementation ZenvraAppDelegate
- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    [NSApp activateIgnoringOtherApps:YES];
}
@end

namespace Zenvra::Platform::Cocoa::Runtime {

bool CocoaContext::initialize()
{
    // Initialize NSApplication
    [NSApplication sharedApplication];
    
    static ZenvraAppDelegate* appDelegate = [[ZenvraAppDelegate alloc] init];
    [NSApp setDelegate:appDelegate];
    
    // Set the app to be a regular app (shows up in Dock and has a Menu Bar)
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    
    // The menu bar will be constructed by CocoaMenuBridge
    CocoaMenuBridge::build_native_menu_bar();

    // Finish launching setup
    [NSApp finishLaunching];
    
    return true;
}

void CocoaContext::shutdown()
{
    // macOS normally handles cleanup upon application termination natively.
}

} // namespace Zenvra
