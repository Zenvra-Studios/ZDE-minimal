#import <Cocoa/Cocoa.h>

#include "Platform/Cocoa/Runtime/CocoaMenuBridge.h"
#include "Commands/CommandIds.h"
#include "UI/Chrome/WindowMenuModel.h"

#include <string>
#include <utility>

static Zenvra::Platform::CommandInvokedCallback g_command_callback;
static Zenvra::Platform::CommandStateQueryCallback g_command_state_query_callback;

@interface ZenvraMenuDelegate : NSObject <NSMenuItemValidation>
- (void)menuItemClicked:(NSMenuItem*)sender;
@end

@implementation ZenvraMenuDelegate
- (void)menuItemClicked:(NSMenuItem*)sender {
    NSLog(@"[DEBUG] menuItemClicked: %@", sender.title);
    if (g_command_callback) {
        NSString* command_id = sender.representedObject;
        if (command_id) {
            NSLog(@"[DEBUG] Invoking command: %@", command_id);
            g_command_callback([command_id UTF8String]);
        } else {
            NSLog(@"[DEBUG] No command_id attached to item: %@", sender.title);
        }
    } else {
        NSLog(@"[DEBUG] No g_command_callback registered!");
    }
}

- (BOOL)validateMenuItem:(NSMenuItem *)menuItem {
    // If no state callback is set yet, keep all items enabled
    if (!g_command_state_query_callback) {
        return YES;
    }

    NSString* command_id = menuItem.representedObject;
    if (!command_id || [command_id length] == 0) {
        // Items without a command_id (e.g. placeholder labels) stay enabled
        return YES;
    }

    auto state = g_command_state_query_callback([command_id UTF8String]);
    menuItem.state = state.checked ? NSControlStateValueOn : NSControlStateValueOff;
    
    // NSLog(@"[DEBUG] validateMenuItem: %@ -> %d", menuItem.title, state.enabled);
    return state.enabled ? YES : NO;
}
@end

namespace Zenvra::Platform::Cocoa::Runtime
{

void CocoaMenuBridge::set_command_callback(CommandInvokedCallback callback) {
    g_command_callback = std::move(callback);
}

void CocoaMenuBridge::set_command_state_query_callback(CommandStateQueryCallback callback) {
    g_command_state_query_callback = std::move(callback);
}

void CocoaMenuBridge::build_native_menu_bar() {
    NSMenu* main_menu = [[NSMenu alloc] init];
    [main_menu setAutoenablesItems:NO];
    
    // Application Menu (macOS specific)
    NSMenuItem* app_menu_item = [[NSMenuItem alloc] init];
    NSMenu* app_menu = [[NSMenu alloc] initWithTitle:@"ZDE"];
    [app_menu setAutoenablesItems:NO];
    NSString* app_name = [[NSProcessInfo processInfo] processName];

    NSMenuItem* about_item = [[NSMenuItem alloc] initWithTitle:[@"About " stringByAppendingString:app_name]
                                                        action:nil
                                                 keyEquivalent:@""];
    [about_item setEnabled:YES];
    [app_menu addItem:about_item];
    [app_menu addItem:[NSMenuItem separatorItem]];

    NSMenuItem* quit_item = [[NSMenuItem alloc] initWithTitle:[@"Quit " stringByAppendingString:app_name]
                                                       action:@selector(terminate:)
                                                keyEquivalent:@"q"];
    [quit_item setEnabled:YES];
    [app_menu addItem:quit_item];
    [app_menu_item setSubmenu:app_menu];
    [main_menu addItem:app_menu_item];
    
    static ZenvraMenuDelegate* shared_delegate = [[ZenvraMenuDelegate alloc] init];
    
    auto window_menus = UI::Chrome::get_window_menu_model();
    for (const auto& menu_model : window_menus) {
        NSString* menu_title = [[NSString alloc] initWithBytes:menu_model.label.data()
                                                        length:menu_model.label.length()
                                                      encoding:NSUTF8StringEncoding];
        NSMenuItem* menu_item = [[NSMenuItem alloc] initWithTitle:menu_title action:nil keyEquivalent:@""];
        [menu_item setEnabled:YES];
        NSMenu* submenu = [[NSMenu alloc] initWithTitle:menu_title];
        // Disable auto-validation so the menu always opens even if commands are
        // not yet registered or return enabled=false.  We still validate items
        // manually through our ZenvraMenuDelegate.
        [submenu setAutoenablesItems:NO];
        
        for (const auto& item_model : menu_model.items) {
            if (item_model.separator) {
                [submenu addItem:[NSMenuItem separatorItem]];
            } else {
                NSString* item_title = [[NSString alloc] initWithBytes:item_model.label.data()
                                                                length:item_model.label.length()
                                                              encoding:NSUTF8StringEncoding];
                NSMenuItem* sub_item = [[NSMenuItem alloc] initWithTitle:item_title
                                                                 action:@selector(menuItemClicked:)
                                                          keyEquivalent:@""];
                sub_item.target = shared_delegate;
                [sub_item setEnabled:YES];
                if (!item_model.command_id.empty()) {
                    sub_item.representedObject = [[NSString alloc] initWithBytes:item_model.command_id.data()
                                                                          length:item_model.command_id.length()
                                                                        encoding:NSUTF8StringEncoding];
                    if (item_model.command_id == Zenvra::Commands::CommandIds::view_terminal_panel) {
                        sub_item.keyEquivalent = @"`";
                        sub_item.keyEquivalentModifierMask = NSEventModifierFlagControl;
                    }
                }
                [submenu addItem:sub_item];
            }
        }
        
        [menu_item setSubmenu:submenu];
        [main_menu addItem:menu_item];
    }
    
    [NSApp setMainMenu:main_menu];
}

} // namespace Zenvra::Platform::Cocoa::Runtime
