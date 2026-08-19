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
    static bool s_initialized = false;
    if (s_initialized)
    {
        return true;
    }
    s_initialized = true;

    // Initialize NSApplication
    [NSApplication sharedApplication];
    
    static ZenvraAppDelegate* appDelegate = [[ZenvraAppDelegate alloc] init];
    [NSApp setDelegate:appDelegate];
    
    // Set the app to be a regular app (shows up in Dock and has a Menu Bar)
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    
    // Load and set the Application Dock Icon explicitly
    NSImage* app_icon = nil;
    NSString* icon_path = [[NSBundle mainBundle] pathForResource:@"AppIcon" ofType:@"icns"];
    if (icon_path) {
        app_icon = [[NSImage alloc] initWithContentsOfFile:icon_path];
    }
    if (!app_icon) {
        NSString* res_path = [[NSBundle mainBundle] resourcePath];
        if (res_path) {
            NSString* fallback_icon = [res_path stringByAppendingPathComponent:@"Assets/icons/AppIcon.icns"];
            if ([[NSFileManager defaultManager] fileExistsAtPath:fallback_icon]) {
                app_icon = [[NSImage alloc] initWithContentsOfFile:fallback_icon];
            } else {
                NSString* png_icon = [res_path stringByAppendingPathComponent:@"Assets/icons/zenvra_logo_512x512.png"];
                if ([[NSFileManager defaultManager] fileExistsAtPath:png_icon]) {
                    app_icon = [[NSImage alloc] initWithContentsOfFile:png_icon];
                }
            }
        }
    }
    if (app_icon) {
        [NSApp setApplicationIconImage:app_icon];
    }

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
