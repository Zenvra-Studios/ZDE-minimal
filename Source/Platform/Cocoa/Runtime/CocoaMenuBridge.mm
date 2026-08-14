#import <Cocoa/Cocoa.h>

#include "Platform/Cocoa/Runtime/CocoaMenuBridge.h"
#include "Commands/CommandIds.h"

#include <string>
#include <utility>

static Zenvra::Platform::CommandInvokedCallback g_command_callback;
static Zenvra::Platform::CommandStateQueryCallback g_command_state_query_callback;

@interface ZenvraMenuDelegate : NSObject <NSMenuDelegate, NSMenuItemValidation>
- (void)menuItemClicked:(NSMenuItem*)sender;
- (void)menuNeedsUpdate:(NSMenu *)menu;
@end

@implementation ZenvraMenuDelegate
- (void)menuItemClicked:(NSMenuItem*)sender {
    if (g_command_callback) {
        NSString* command_id = sender.representedObject;
        if (command_id && [command_id length] > 0) {
            g_command_callback([command_id UTF8String]);
        }
    }
}

- (void)menuNeedsUpdate:(NSMenu *)menu {
    if (!g_command_state_query_callback) {
        return;
    }
    for (NSMenuItem* item in [menu itemArray]) {
        if (item.submenu) {
            [item.submenu setDelegate:self];
        }
        NSString* command_id = item.representedObject;
        if (command_id && [command_id length] > 0) {
            auto state = g_command_state_query_callback([command_id UTF8String]);
            item.state = state.checked ? NSControlStateValueOn : NSControlStateValueOff;
            item.enabled = state.enabled ? YES : NO;
        }
    }
}

- (BOOL)validateMenuItem:(NSMenuItem *)menuItem {
    // macOS built-in application / window actions are always enabled
    if (menuItem.action == @selector(hide:) ||
        menuItem.action == @selector(hideOtherApplications:) ||
        menuItem.action == @selector(unhideAllApplications:) ||
        menuItem.action == @selector(terminate:) ||
        menuItem.action == @selector(performMiniaturize:) ||
        menuItem.action == @selector(performZoom:) ||
        menuItem.action == @selector(arrangeInFront:)) {
        return YES;
    }

    if (!g_command_state_query_callback) {
        return YES;
    }

    NSString* command_id = menuItem.representedObject;
    if (!command_id || [command_id length] == 0) {
        return YES;
    }

    auto state = g_command_state_query_callback([command_id UTF8String]);
    menuItem.state = state.checked ? NSControlStateValueOn : NSControlStateValueOff;
    return state.enabled ? YES : NO;
}
@end

namespace Zenvra::Platform::Cocoa::Runtime
{

namespace
{

static ZenvraMenuDelegate* g_shared_menu_delegate = nil;

NSMenu* create_menu(NSString* title) {
    NSMenu* menu = [[NSMenu alloc] initWithTitle:title];
    [menu setAutoenablesItems:NO];
    if (g_shared_menu_delegate) {
        [menu setDelegate:g_shared_menu_delegate];
    }
    return menu;
}

NSMenuItem* make_item(
    NSString* title,
    SEL action,
    id target,
    NSString* key_equiv = @"",
    NSEventModifierFlags mod_mask = NSEventModifierFlagCommand,
    std::string_view command_id = {})
{
    NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title
                                                  action:action
                                           keyEquivalent:(key_equiv ? key_equiv : @"")];
    item.target = target;
    [item setEnabled:YES];
    if (key_equiv && [key_equiv length] > 0) {
        item.keyEquivalentModifierMask = mod_mask;
    }
    if (!command_id.empty()) {
        item.representedObject = [NSString stringWithUTF8String:std::string(command_id).c_str()];
    }
    return item;
}

NSMenuItem* make_cmd_item(
    NSString* title,
    std::string_view command_id,
    id delegate,
    NSString* key_equiv = @"",
    NSEventModifierFlags mod_mask = NSEventModifierFlagCommand)
{
    return make_item(title, @selector(menuItemClicked:), delegate, key_equiv, mod_mask, command_id);
}

NSMenuItem* make_submenu(NSString* title, NSMenu* submenu) {
    NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title action:nil keyEquivalent:@""];
    [item setEnabled:YES];
    if (g_shared_menu_delegate && submenu) {
        [submenu setDelegate:g_shared_menu_delegate];
    }
    [item setSubmenu:submenu];
    return item;
}

NSString* key_f(unichar key) {
    return [NSString stringWithFormat:@"%C", key];
}

} // namespace

void CocoaMenuBridge::set_command_callback(CommandInvokedCallback callback) {
    g_command_callback = std::move(callback);
}

void CocoaMenuBridge::set_command_state_query_callback(CommandStateQueryCallback callback) {
    g_command_state_query_callback = std::move(callback);
}

void CocoaMenuBridge::build_native_menu_bar() {
    if (!g_shared_menu_delegate) {
        g_shared_menu_delegate = [[ZenvraMenuDelegate alloc] init];
    }
    ZenvraMenuDelegate* shared_delegate = g_shared_menu_delegate;

    NSMenu* main_menu = [[NSMenu alloc] init];
    [main_menu setAutoenablesItems:NO];
    [main_menu setDelegate:shared_delegate];

    NSString* app_name = [[NSProcessInfo processInfo] processName];
    if (!app_name || [app_name length] == 0) {
        app_name = @"ZDE";
    }

    // ==========================================
    // 1. Application Menu (macOS specific)
    // ==========================================
    NSMenu* app_menu = create_menu(app_name);

    [app_menu addItem:make_cmd_item([@"About " stringByAppendingString:app_name],
                                    Commands::CommandIds::help_about,
                                    shared_delegate)];
    [app_menu addItem:make_cmd_item(@"Check for Updates...",
                                    Commands::CommandIds::help_check_for_updates,
                                    shared_delegate)];
    [app_menu addItem:[NSMenuItem separatorItem]];

    // Preferences Submenu
    NSMenu* pref_menu = create_menu(@"Preferences");
    [pref_menu addItem:make_cmd_item(@"Settings",
                                     Commands::CommandIds::open_settings,
                                     shared_delegate,
                                     @",",
                                     NSEventModifierFlagCommand)];
    [pref_menu addItem:make_cmd_item(@"Online Services Settings",
                                     "zde.settings.onlineServices",
                                     shared_delegate)];
    [pref_menu addItem:make_cmd_item(@"Telemetry",
                                     "zde.settings.telemetry",
                                     shared_delegate)];
    [pref_menu addItem:[NSMenuItem separatorItem]];
    [pref_menu addItem:make_cmd_item(@"Extensions",
                                     Commands::CommandIds::open_plugins,
                                     shared_delegate,
                                     @"X",
                                     NSEventModifierFlagShift | NSEventModifierFlagCommand)];
    [pref_menu addItem:make_cmd_item(@"Keyboard Shortcuts",
                                     "zde.shortcuts.open",
                                     shared_delegate,
                                     @"k",
                                     NSEventModifierFlagCommand)];
    [pref_menu addItem:make_cmd_item(@"User Tasks",
                                     "zde.tasks.user",
                                     shared_delegate)];
    [pref_menu addItem:make_cmd_item(@"User Snippets",
                                     "zde.snippets.open",
                                     shared_delegate)];
    [pref_menu addItem:[NSMenuItem separatorItem]];
    [pref_menu addItem:make_cmd_item(@"Color Theme",
                                     Commands::CommandIds::open_themes,
                                     shared_delegate,
                                     @"t",
                                     NSEventModifierFlagCommand)];
    [pref_menu addItem:make_cmd_item(@"File Icon Theme",
                                     "zde.themes.fileIcon",
                                     shared_delegate)];
    [pref_menu addItem:make_cmd_item(@"Product Icon Theme",
                                     "zde.themes.productIcon",
                                     shared_delegate)];
    [pref_menu addItem:[NSMenuItem separatorItem]];
    [pref_menu addItem:make_cmd_item(@"Turn on Cloud Changes...",
                                     "zde.cloud.turnOn",
                                     shared_delegate)];
    [app_menu addItem:make_submenu(@"Preferences", pref_menu)];

    // Services Submenu
    NSMenu* services_menu = create_menu(@"Services");
    [app_menu addItem:make_submenu(@"Services", services_menu)];
    [NSApp setServicesMenu:services_menu];

    [app_menu addItem:[NSMenuItem separatorItem]];

    // Hide Actions
    [app_menu addItem:make_item([@"Hide " stringByAppendingString:app_name],
                                @selector(hide:),
                                NSApp,
                                @"h",
                                NSEventModifierFlagCommand)];
    [app_menu addItem:make_item(@"Hide Others",
                                @selector(hideOtherApplications:),
                                NSApp,
                                @"h",
                                NSEventModifierFlagOption | NSEventModifierFlagCommand)];
    [app_menu addItem:make_item(@"Show All",
                                @selector(unhideAllApplications:),
                                NSApp)];

    [app_menu addItem:[NSMenuItem separatorItem]];

    // Quit
    [app_menu addItem:make_item([@"Quit " stringByAppendingString:app_name],
                                @selector(terminate:),
                                NSApp,
                                @"q",
                                NSEventModifierFlagCommand)];

    [main_menu addItem:make_submenu(app_name, app_menu)];

    // ==========================================
    // 2. File Menu
    // ==========================================
    NSMenu* file_menu = create_menu(@"File");

    [file_menu addItem:make_cmd_item(@"New Text File",
                                     Commands::CommandIds::file_new,
                                     shared_delegate,
                                     @"n",
                                     NSEventModifierFlagCommand)];
    [file_menu addItem:make_cmd_item(@"New File...",
                                     Commands::CommandIds::file_new,
                                     shared_delegate,
                                     @"n",
                                     NSEventModifierFlagOption | NSEventModifierFlagCommand)];
    [file_menu addItem:make_cmd_item(@"New Window",
                                     Commands::CommandIds::window_new,
                                     shared_delegate,
                                     @"N",
                                     NSEventModifierFlagShift | NSEventModifierFlagCommand)];
    [file_menu addItem:[NSMenuItem separatorItem]];

    [file_menu addItem:make_cmd_item(@"Open File...",
                                     Commands::CommandIds::file_open,
                                     shared_delegate,
                                     @"o",
                                     NSEventModifierFlagCommand)];
    [file_menu addItem:make_cmd_item(@"Open Folder...",
                                     Commands::CommandIds::folder_open,
                                     shared_delegate,
                                     @"o",
                                     NSEventModifierFlagOption | NSEventModifierFlagCommand)];

    // Open Recent Submenu
    NSMenu* recent_menu = create_menu(@"Open Recent");
    [recent_menu addItem:make_cmd_item(@"Reopen Closed Editor",
                                       "zde.file.reopenClosed",
                                       shared_delegate,
                                       @"T",
                                       NSEventModifierFlagShift | NSEventModifierFlagCommand)];
    [recent_menu addItem:[NSMenuItem separatorItem]];
    [recent_menu addItem:make_cmd_item(@"More...",
                                       "zde.file.moreRecent",
                                       shared_delegate)];
    [recent_menu addItem:make_cmd_item(@"Clear Recently Opened",
                                       "zde.file.clearRecent",
                                       shared_delegate)];
    [file_menu addItem:make_submenu(@"Open Recent", recent_menu)];

    [file_menu addItem:[NSMenuItem separatorItem]];

    [file_menu addItem:make_cmd_item(@"Add Folder to Project...",
                                     Commands::CommandIds::project_add_folder,
                                     shared_delegate)];
    [file_menu addItem:make_cmd_item(@"Save Workspace As...",
                                     "zde.project.saveAs",
                                     shared_delegate)];
    [file_menu addItem:[NSMenuItem separatorItem]];

    [file_menu addItem:make_cmd_item(@"Save",
                                     Commands::CommandIds::file_save,
                                     shared_delegate,
                                     @"s",
                                     NSEventModifierFlagCommand)];
    [file_menu addItem:make_cmd_item(@"Save As...",
                                     Commands::CommandIds::file_save_as,
                                     shared_delegate,
                                     @"S",
                                     NSEventModifierFlagShift | NSEventModifierFlagCommand)];
    [file_menu addItem:make_cmd_item(@"Save All",
                                     Commands::CommandIds::file_save_all,
                                     shared_delegate,
                                     @"s",
                                     NSEventModifierFlagOption | NSEventModifierFlagCommand)];
    [file_menu addItem:[NSMenuItem separatorItem]];

    [file_menu addItem:make_cmd_item(@"Auto Save",
                                     "zde.file.autoSave",
                                     shared_delegate)];
    [file_menu addItem:[NSMenuItem separatorItem]];

    [file_menu addItem:make_cmd_item(@"Revert File",
                                     "zde.file.revert",
                                     shared_delegate)];
    [file_menu addItem:make_cmd_item(@"Close Editor",
                                     Commands::CommandIds::file_close,
                                     shared_delegate,
                                     @"w",
                                     NSEventModifierFlagCommand)];
    [file_menu addItem:make_cmd_item(@"Close Folder",
                                     Commands::CommandIds::project_close,
                                     shared_delegate,
                                     @"w",
                                     NSEventModifierFlagOption | NSEventModifierFlagCommand)];
    [file_menu addItem:make_cmd_item(@"Close Window",
                                     Commands::CommandIds::window_close,
                                     shared_delegate,
                                     @"W",
                                     NSEventModifierFlagShift | NSEventModifierFlagCommand)];

    [main_menu addItem:make_submenu(@"File", file_menu)];

    // ==========================================
    // 3. Edit Menu
    // ==========================================
    NSMenu* edit_menu = create_menu(@"Edit");

    [edit_menu addItem:make_cmd_item(@"Undo",
                                     Commands::CommandIds::edit_undo,
                                     shared_delegate,
                                     @"z",
                                     NSEventModifierFlagCommand)];
    [edit_menu addItem:make_cmd_item(@"Redo",
                                     Commands::CommandIds::edit_redo,
                                     shared_delegate,
                                     @"Z",
                                     NSEventModifierFlagShift | NSEventModifierFlagCommand)];
    [edit_menu addItem:[NSMenuItem separatorItem]];

    [edit_menu addItem:make_cmd_item(@"Cut",
                                     Commands::CommandIds::edit_cut,
                                     shared_delegate,
                                     @"x",
                                     NSEventModifierFlagCommand)];
    [edit_menu addItem:make_cmd_item(@"Copy",
                                     Commands::CommandIds::edit_copy,
                                     shared_delegate,
                                     @"c",
                                     NSEventModifierFlagCommand)];
    [edit_menu addItem:make_cmd_item(@"Paste",
                                     Commands::CommandIds::edit_paste,
                                     shared_delegate,
                                     @"v",
                                     NSEventModifierFlagCommand)];
    [edit_menu addItem:[NSMenuItem separatorItem]];

    [edit_menu addItem:make_cmd_item(@"Find",
                                     Commands::CommandIds::edit_find,
                                     shared_delegate,
                                     @"f",
                                     NSEventModifierFlagCommand)];
    [edit_menu addItem:make_cmd_item(@"Replace",
                                     "zde.edit.replace",
                                     shared_delegate,
                                     @"f",
                                     NSEventModifierFlagOption | NSEventModifierFlagCommand)];
    [edit_menu addItem:[NSMenuItem separatorItem]];

    [edit_menu addItem:make_cmd_item(@"Find in Files",
                                     Commands::CommandIds::edit_find_in_project,
                                     shared_delegate,
                                     @"F",
                                     NSEventModifierFlagShift | NSEventModifierFlagCommand)];
    [edit_menu addItem:make_cmd_item(@"Replace in Files",
                                     "zde.edit.replaceInFiles",
                                     shared_delegate,
                                     @"H",
                                     NSEventModifierFlagShift | NSEventModifierFlagCommand)];
    [edit_menu addItem:[NSMenuItem separatorItem]];

    [edit_menu addItem:make_cmd_item(@"Toggle Line Comment",
                                     Commands::CommandIds::edit_toggle_comment,
                                     shared_delegate,
                                     @"/",
                                     NSEventModifierFlagCommand)];
    [edit_menu addItem:make_cmd_item(@"Toggle Block Comment",
                                     "zde.edit.toggleBlockComment",
                                     shared_delegate,
                                     @"A",
                                     NSEventModifierFlagOption | NSEventModifierFlagShift)];
    [edit_menu addItem:make_cmd_item(@"Emmet: Expand Abbreviation",
                                     "zde.emmet.expand",
                                     shared_delegate,
                                     @"\t",
                                     0)];

    [main_menu addItem:make_submenu(@"Edit", edit_menu)];

    // ==========================================
    // 4. Selection Menu
    // ==========================================
    NSMenu* sel_menu = create_menu(@"Selection");

    [sel_menu addItem:make_cmd_item(@"Select All",
                                    Commands::CommandIds::selection_select_all,
                                    shared_delegate,
                                    @"a",
                                    NSEventModifierFlagCommand)];
    [sel_menu addItem:make_cmd_item(@"Expand Selection",
                                    Commands::CommandIds::selection_expand,
                                    shared_delegate,
                                    key_f(NSRightArrowFunctionKey),
                                    NSEventModifierFlagControl | NSEventModifierFlagShift | NSEventModifierFlagCommand)];
    [sel_menu addItem:make_cmd_item(@"Shrink Selection",
                                    Commands::CommandIds::selection_shrink,
                                    shared_delegate,
                                    key_f(NSLeftArrowFunctionKey),
                                    NSEventModifierFlagControl | NSEventModifierFlagShift | NSEventModifierFlagCommand)];
    [sel_menu addItem:[NSMenuItem separatorItem]];

    [sel_menu addItem:make_cmd_item(@"Copy Line Up",
                                    Commands::CommandIds::selection_copy_line_up,
                                    shared_delegate,
                                    key_f(NSUpArrowFunctionKey),
                                    NSEventModifierFlagOption | NSEventModifierFlagShift)];
    [sel_menu addItem:make_cmd_item(@"Copy Line Down",
                                    Commands::CommandIds::selection_copy_line_down,
                                    shared_delegate,
                                    key_f(NSDownArrowFunctionKey),
                                    NSEventModifierFlagOption | NSEventModifierFlagShift)];
    [sel_menu addItem:make_cmd_item(@"Move Line Up",
                                    Commands::CommandIds::selection_move_line_up,
                                    shared_delegate,
                                    key_f(NSUpArrowFunctionKey),
                                    NSEventModifierFlagOption)];
    [sel_menu addItem:make_cmd_item(@"Move Line Down",
                                    Commands::CommandIds::selection_move_line_down,
                                    shared_delegate,
                                    key_f(NSDownArrowFunctionKey),
                                    NSEventModifierFlagOption)];
    [sel_menu addItem:make_cmd_item(@"Duplicate Selection",
                                    Commands::CommandIds::selection_duplicate,
                                    shared_delegate)];
    [sel_menu addItem:[NSMenuItem separatorItem]];

    [sel_menu addItem:make_cmd_item(@"Add Cursor Above",
                                    Commands::CommandIds::selection_add_cursor_above,
                                    shared_delegate,
                                    key_f(NSUpArrowFunctionKey),
                                    NSEventModifierFlagOption | NSEventModifierFlagCommand)];
    [sel_menu addItem:make_cmd_item(@"Add Cursor Below",
                                    Commands::CommandIds::selection_add_cursor_below,
                                    shared_delegate,
                                    key_f(NSDownArrowFunctionKey),
                                    NSEventModifierFlagOption | NSEventModifierFlagCommand)];
    [sel_menu addItem:make_cmd_item(@"Add Cursors to Line Ends",
                                    Commands::CommandIds::selection_add_cursors_to_line_ends,
                                    shared_delegate,
                                    @"I",
                                    NSEventModifierFlagOption | NSEventModifierFlagShift)];
    [sel_menu addItem:make_cmd_item(@"Add Next Occurrence",
                                    Commands::CommandIds::selection_add_next_occurrence,
                                    shared_delegate,
                                    @"d",
                                    NSEventModifierFlagCommand)];
    [sel_menu addItem:make_cmd_item(@"Add Previous Occurrence",
                                    Commands::CommandIds::selection_add_previous_occurrence,
                                    shared_delegate)];
    [sel_menu addItem:make_cmd_item(@"Select All Occurrences",
                                    Commands::CommandIds::selection_select_all_occurrences,
                                    shared_delegate,
                                    @"L",
                                    NSEventModifierFlagShift | NSEventModifierFlagCommand)];
    [sel_menu addItem:[NSMenuItem separatorItem]];

    [sel_menu addItem:make_cmd_item(@"Switch to Ctrl+Click for Multi-Cursor",
                                    Commands::CommandIds::selection_switch_multi_cursor_modifier,
                                    shared_delegate)];
    [sel_menu addItem:make_cmd_item(@"Column Selection Mode",
                                    Commands::CommandIds::selection_column_selection_mode,
                                    shared_delegate)];

    [main_menu addItem:make_submenu(@"Selection", sel_menu)];

    // ==========================================
    // 5. View Menu
    // ==========================================
    NSMenu* view_menu = create_menu(@"View");

    [view_menu addItem:make_cmd_item(@"Command Palette...",
                                     Commands::CommandIds::help_show_all_commands,
                                     shared_delegate,
                                     @"P",
                                     NSEventModifierFlagShift | NSEventModifierFlagCommand)];
    [view_menu addItem:make_cmd_item(@"Open View...",
                                     "zde.view.openView",
                                     shared_delegate)];
    [view_menu addItem:[NSMenuItem separatorItem]];

    // Appearance Submenu
    NSMenu* apper_menu = create_menu(@"Appearance");
    [apper_menu addItem:make_cmd_item(@"Full Screen",
                                      Commands::CommandIds::window_toggle_fullscreen,
                                      shared_delegate,
                                      @"f",
                                      NSEventModifierFlagControl | NSEventModifierFlagCommand)];
    [apper_menu addItem:make_cmd_item(@"Zen Mode",
                                      "zde.view.zenMode",
                                      shared_delegate)];
    [apper_menu addItem:make_cmd_item(@"Centered Layout",
                                      "zde.view.centeredLayout",
                                      shared_delegate)];
    [apper_menu addItem:[NSMenuItem separatorItem]];
    [apper_menu addItem:make_cmd_item(@"Primary Side Bar",
                                      Commands::CommandIds::view_toggle_left_dock,
                                      shared_delegate,
                                      @"b",
                                      NSEventModifierFlagCommand)];
    [apper_menu addItem:make_cmd_item(@"Secondary Side Bar",
                                      Commands::CommandIds::view_toggle_right_dock,
                                      shared_delegate,
                                      @"b",
                                      NSEventModifierFlagOption | NSEventModifierFlagCommand)];
    [apper_menu addItem:make_cmd_item(@"Panel",
                                      Commands::CommandIds::view_toggle_bottom_dock,
                                      shared_delegate,
                                      @"j",
                                      NSEventModifierFlagCommand)];
    [apper_menu addItem:make_cmd_item(@"Status Bar",
                                      "zde.view.statusBar",
                                      shared_delegate)];
    [apper_menu addItem:make_cmd_item(@"Activity Bar",
                                      "zde.view.activityBar",
                                      shared_delegate)];
    [apper_menu addItem:[NSMenuItem separatorItem]];
    [apper_menu addItem:make_cmd_item(@"Zoom In",
                                      Commands::CommandIds::view_zoom_in,
                                      shared_delegate,
                                      @"=",
                                      NSEventModifierFlagCommand)];
    [apper_menu addItem:make_cmd_item(@"Zoom Out",
                                      Commands::CommandIds::view_zoom_out,
                                      shared_delegate,
                                      @"-",
                                      NSEventModifierFlagCommand)];
    [apper_menu addItem:make_cmd_item(@"Reset Zoom",
                                      Commands::CommandIds::view_reset_zoom,
                                      shared_delegate,
                                      @"0",
                                      NSEventModifierFlagCommand)];
    [view_menu addItem:make_submenu(@"Appearance", apper_menu)];

    // Editor Layout Submenu
    NSMenu* layout_menu = create_menu(@"Editor Layout");
    [layout_menu addItem:make_cmd_item(@"Split Up",
                                       Commands::CommandIds::view_split_up,
                                       shared_delegate)];
    [layout_menu addItem:make_cmd_item(@"Split Down",
                                       Commands::CommandIds::view_split_down,
                                       shared_delegate)];
    [layout_menu addItem:make_cmd_item(@"Split Left",
                                       Commands::CommandIds::view_split_left,
                                       shared_delegate)];
    [layout_menu addItem:make_cmd_item(@"Split Right",
                                       Commands::CommandIds::view_split_right,
                                       shared_delegate)];
    [layout_menu addItem:[NSMenuItem separatorItem]];
    [layout_menu addItem:make_cmd_item(@"Single", "zde.layout.single", shared_delegate)];
    [layout_menu addItem:make_cmd_item(@"Two Columns", "zde.layout.twoColumns", shared_delegate)];
    [layout_menu addItem:make_cmd_item(@"Three Columns", "zde.layout.threeColumns", shared_delegate)];
    [layout_menu addItem:make_cmd_item(@"Two Rows", "zde.layout.twoRows", shared_delegate)];
    [layout_menu addItem:make_cmd_item(@"Grid (2x2)", "zde.layout.grid", shared_delegate)];
    [view_menu addItem:make_submenu(@"Editor Layout", layout_menu)];

    [view_menu addItem:[NSMenuItem separatorItem]];

    [view_menu addItem:make_cmd_item(@"Explorer",
                                     Commands::CommandIds::view_explorer,
                                     shared_delegate,
                                     @"E",
                                     NSEventModifierFlagShift | NSEventModifierFlagCommand)];
    [view_menu addItem:make_cmd_item(@"Search",
                                     Commands::CommandIds::view_search,
                                     shared_delegate,
                                     @"F",
                                     NSEventModifierFlagShift | NSEventModifierFlagCommand)];
    [view_menu addItem:make_cmd_item(@"Source Control",
                                     Commands::CommandIds::view_git_panel,
                                     shared_delegate,
                                     @"G",
                                     NSEventModifierFlagControl | NSEventModifierFlagShift)];
    [view_menu addItem:make_cmd_item(@"Run and Debug",
                                     Commands::CommandIds::view_debugger_panel,
                                     shared_delegate,
                                     @"D",
                                     NSEventModifierFlagShift | NSEventModifierFlagCommand)];
    [view_menu addItem:make_cmd_item(@"Extensions",
                                     Commands::CommandIds::open_plugins,
                                     shared_delegate,
                                     @"X",
                                     NSEventModifierFlagShift | NSEventModifierFlagCommand)];
    [view_menu addItem:[NSMenuItem separatorItem]];

    [view_menu addItem:make_cmd_item(@"Problems",
                                     Commands::CommandIds::view_problems,
                                     shared_delegate,
                                     @"M",
                                     NSEventModifierFlagShift | NSEventModifierFlagCommand)];
    [view_menu addItem:make_cmd_item(@"Output",
                                     Commands::CommandIds::view_output,
                                     shared_delegate,
                                     @"U",
                                     NSEventModifierFlagShift | NSEventModifierFlagCommand)];
    [view_menu addItem:make_cmd_item(@"Debug Console",
                                     "zde.view.debugConsole",
                                     shared_delegate,
                                     @"Y",
                                     NSEventModifierFlagShift | NSEventModifierFlagCommand)];
    [view_menu addItem:make_cmd_item(@"Terminal",
                                     Commands::CommandIds::view_terminal_panel,
                                     shared_delegate,
                                     @"`",
                                     NSEventModifierFlagControl)];
    [view_menu addItem:[NSMenuItem separatorItem]];

    [view_menu addItem:make_cmd_item(@"Word Wrap",
                                     "zde.view.wordWrap",
                                     shared_delegate,
                                     @"z",
                                     NSEventModifierFlagOption)];

    [main_menu addItem:make_submenu(@"View", view_menu)];

    // ==========================================
    // 6. Go Menu
    // ==========================================
    NSMenu* go_menu = create_menu(@"Go");

    [go_menu addItem:make_cmd_item(@"Back",
                                   "zde.navigate.back",
                                   shared_delegate,
                                   @"-",
                                   NSEventModifierFlagControl)];
    [go_menu addItem:make_cmd_item(@"Forward",
                                   "zde.navigate.forward",
                                   shared_delegate,
                                   @"-",
                                   NSEventModifierFlagControl | NSEventModifierFlagShift)];
    [go_menu addItem:make_cmd_item(@"Last Edit Location",
                                   "zde.navigate.lastEditLocation",
                                   shared_delegate,
                                   @"q",
                                   NSEventModifierFlagCommand)];
    [go_menu addItem:[NSMenuItem separatorItem]];

    // Switch Editor Submenu
    NSMenu* sw_menu = create_menu(@"Switch Editor");
    [sw_menu addItem:make_cmd_item(@"Next Editor",
                                   "zde.navigate.nextEditor",
                                   shared_delegate,
                                   @"\t",
                                   NSEventModifierFlagControl)];
    [sw_menu addItem:make_cmd_item(@"Previous Editor",
                                   "zde.navigate.prevEditor",
                                   shared_delegate,
                                   @"\t",
                                   NSEventModifierFlagControl | NSEventModifierFlagShift)];
    [sw_menu addItem:make_cmd_item(@"Next Used Editor", "zde.navigate.nextUsedEditor", shared_delegate)];
    [sw_menu addItem:make_cmd_item(@"Previous Used Editor", "zde.navigate.prevUsedEditor", shared_delegate)];
    [go_menu addItem:make_submenu(@"Switch Editor", sw_menu)];

    [go_menu addItem:make_cmd_item(@"Switch Window...", "zde.window.switch", shared_delegate)];
    [go_menu addItem:[NSMenuItem separatorItem]];

    [go_menu addItem:make_cmd_item(@"Go to File...",
                                   Commands::CommandIds::file_open,
                                   shared_delegate,
                                   @"p",
                                   NSEventModifierFlagCommand)];
    [go_menu addItem:make_cmd_item(@"Go to Symbol in Workspace...",
                                   "zde.navigate.symbolWorkspace",
                                   shared_delegate,
                                   @"t",
                                   NSEventModifierFlagCommand)];
    [go_menu addItem:[NSMenuItem separatorItem]];

    [go_menu addItem:make_cmd_item(@"Go to Symbol in Editor...",
                                   "zde.navigate.symbolEditor",
                                   shared_delegate,
                                   @"O",
                                   NSEventModifierFlagShift | NSEventModifierFlagCommand)];
    [go_menu addItem:make_cmd_item(@"Go to Definition",
                                   "zde.navigate.definition",
                                   shared_delegate,
                                   key_f(NSF12FunctionKey),
                                   0)];
    [go_menu addItem:make_cmd_item(@"Go to Declaration",
                                   "zde.navigate.declaration",
                                   shared_delegate)];
    [go_menu addItem:make_cmd_item(@"Go to Type Definition",
                                   "zde.navigate.typeDefinition",
                                   shared_delegate)];
    [go_menu addItem:make_cmd_item(@"Go to Implementation",
                                   "zde.navigate.implementation",
                                   shared_delegate,
                                   key_f(NSF12FunctionKey),
                                   NSEventModifierFlagCommand)];
    [go_menu addItem:make_cmd_item(@"Go to References",
                                   "zde.navigate.references",
                                   shared_delegate,
                                   key_f(NSF12FunctionKey),
                                   NSEventModifierFlagShift)];
    [go_menu addItem:[NSMenuItem separatorItem]];

    [go_menu addItem:make_cmd_item(@"Go to Line/Column...",
                                   "zde.navigate.lineColumn",
                                   shared_delegate,
                                   @"g",
                                   NSEventModifierFlagControl)];
    [go_menu addItem:make_cmd_item(@"Go to Bracket",
                                   "zde.navigate.bracket",
                                   shared_delegate,
                                   @"\\",
                                   NSEventModifierFlagShift | NSEventModifierFlagCommand)];
    [go_menu addItem:[NSMenuItem separatorItem]];

    [go_menu addItem:make_cmd_item(@"Next Problem",
                                   "zde.navigate.nextProblem",
                                   shared_delegate,
                                   key_f(NSF8FunctionKey),
                                   0)];
    [go_menu addItem:make_cmd_item(@"Previous Problem",
                                   "zde.navigate.prevProblem",
                                   shared_delegate,
                                   key_f(NSF8FunctionKey),
                                   NSEventModifierFlagShift)];

    [main_menu addItem:make_submenu(@"Go", go_menu)];

    // ==========================================
    // 7. Run Menu
    // ==========================================
    NSMenu* run_menu = create_menu(@"Run");

    [run_menu addItem:make_cmd_item(@"Start Debugging",
                                    Commands::CommandIds::run_start,
                                    shared_delegate,
                                    key_f(NSF5FunctionKey),
                                    0)];
    [run_menu addItem:make_cmd_item(@"Run Without Debugging",
                                    Commands::CommandIds::run_start,
                                    shared_delegate,
                                    key_f(NSF5FunctionKey),
                                    NSEventModifierFlagControl)];
    [run_menu addItem:make_cmd_item(@"Stop Debugging",
                                    "zde.run.stop",
                                    shared_delegate,
                                    key_f(NSF5FunctionKey),
                                    NSEventModifierFlagShift)];
    [run_menu addItem:make_cmd_item(@"Restart Debugging",
                                    "zde.run.restart",
                                    shared_delegate,
                                    key_f(NSF5FunctionKey),
                                    NSEventModifierFlagShift | NSEventModifierFlagCommand)];
    [run_menu addItem:[NSMenuItem separatorItem]];

    [run_menu addItem:make_cmd_item(@"Open Configurations", "zde.run.openConfigs", shared_delegate)];
    [run_menu addItem:make_cmd_item(@"Add Configuration...", "zde.run.addConfig", shared_delegate)];
    [run_menu addItem:[NSMenuItem separatorItem]];

    [run_menu addItem:make_cmd_item(@"Step Over",
                                    "zde.run.stepOver",
                                    shared_delegate,
                                    key_f(NSF10FunctionKey),
                                    0)];
    [run_menu addItem:make_cmd_item(@"Step Into",
                                    "zde.run.stepInto",
                                    shared_delegate,
                                    key_f(NSF11FunctionKey),
                                    0)];
    [run_menu addItem:make_cmd_item(@"Step Out",
                                    "zde.run.stepOut",
                                    shared_delegate,
                                    key_f(NSF11FunctionKey),
                                    NSEventModifierFlagShift)];
    [run_menu addItem:make_cmd_item(@"Continue",
                                    "zde.run.continue",
                                    shared_delegate,
                                    key_f(NSF5FunctionKey),
                                    0)];
    [run_menu addItem:[NSMenuItem separatorItem]];

    [run_menu addItem:make_cmd_item(@"Toggle Breakpoint",
                                    "zde.run.toggleBreakpoint",
                                    shared_delegate,
                                    key_f(NSF9FunctionKey),
                                    0)];

    // New Breakpoint Submenu
    NSMenu* bp_menu = create_menu(@"New Breakpoint");
    [bp_menu addItem:make_cmd_item(@"Conditional Breakpoint...", "zde.run.condBreakpoint", shared_delegate)];
    [bp_menu addItem:make_cmd_item(@"Logpoint...", "zde.run.logpoint", shared_delegate)];
    [run_menu addItem:make_submenu(@"New Breakpoint", bp_menu)];

    [run_menu addItem:make_cmd_item(@"Enable All Breakpoints", "zde.run.enableAllBreakpoints", shared_delegate)];
    [run_menu addItem:make_cmd_item(@"Disable All Breakpoints", "zde.run.disableAllBreakpoints", shared_delegate)];
    [run_menu addItem:make_cmd_item(@"Remove All Breakpoints", "zde.run.removeAllBreakpoints", shared_delegate)];
    [run_menu addItem:[NSMenuItem separatorItem]];

    [run_menu addItem:make_cmd_item(@"Install Additional Debuggers...", "zde.run.installDebuggers", shared_delegate)];

    [main_menu addItem:make_submenu(@"Run", run_menu)];

    // ==========================================
    // 8. Terminal Menu
    // ==========================================
    NSMenu* term_menu = create_menu(@"Terminal");

    [term_menu addItem:make_cmd_item(@"New Terminal",
                                     Commands::CommandIds::view_terminal_panel,
                                     shared_delegate,
                                     @"`",
                                     NSEventModifierFlagControl | NSEventModifierFlagShift)];
    [term_menu addItem:make_cmd_item(@"Split Terminal",
                                     "zde.terminal.split",
                                     shared_delegate,
                                     @"\\",
                                     NSEventModifierFlagCommand)];
    [term_menu addItem:[NSMenuItem separatorItem]];

    [term_menu addItem:make_cmd_item(@"Run Task...", "zde.terminal.runTask", shared_delegate)];
    [term_menu addItem:make_cmd_item(@"Run Build Task...",
                                     Commands::CommandIds::build_build_project,
                                     shared_delegate,
                                     @"B",
                                     NSEventModifierFlagShift | NSEventModifierFlagCommand)];
    [term_menu addItem:make_cmd_item(@"Run Active File", "zde.terminal.runActiveFile", shared_delegate)];
    [term_menu addItem:make_cmd_item(@"Run Selected Text", "zde.terminal.runSelectedText", shared_delegate)];
    [term_menu addItem:[NSMenuItem separatorItem]];

    [term_menu addItem:make_cmd_item(@"Show Terminal",
                                     Commands::CommandIds::view_terminal_panel,
                                     shared_delegate,
                                     @"`",
                                     NSEventModifierFlagControl)];

    [main_menu addItem:make_submenu(@"Terminal", term_menu)];

    // ==========================================
    // 9. Window Menu (macOS Standard)
    // ==========================================
    NSMenu* win_menu = create_menu(@"Window");

    [win_menu addItem:make_item(@"Minimize",
                                @selector(performMiniaturize:),
                                nil,
                                @"m",
                                NSEventModifierFlagCommand)];
    [win_menu addItem:make_item(@"Zoom",
                                @selector(performZoom:),
                                nil)];
    [win_menu addItem:[NSMenuItem separatorItem]];

    [win_menu addItem:make_item(@"Bring All to Front",
                                @selector(arrangeInFront:),
                                NSApp)];

    [main_menu addItem:make_submenu(@"Window", win_menu)];
    [NSApp setWindowsMenu:win_menu];

    // ==========================================
    // 10. Help Menu
    // ==========================================
    NSMenu* help_menu = create_menu(@"Help");

    [help_menu addItem:make_cmd_item(@"Welcome",
                                     Commands::CommandIds::help_welcome,
                                     shared_delegate)];
    [help_menu addItem:make_cmd_item(@"Show All Commands",
                                     Commands::CommandIds::help_show_all_commands,
                                     shared_delegate,
                                     @"P",
                                     NSEventModifierFlagShift | NSEventModifierFlagCommand)];
    [help_menu addItem:make_cmd_item(@"Documentation", "zde.help.docs", shared_delegate)];
    [help_menu addItem:make_cmd_item(@"Editor Playground",
                                     Commands::CommandIds::help_editor_playground,
                                     shared_delegate)];
    [help_menu addItem:make_cmd_item(@"Show Release Notes", "zde.help.releaseNotes", shared_delegate)];
    [help_menu addItem:[NSMenuItem separatorItem]];

    [help_menu addItem:make_cmd_item(@"Keyboard Shortcuts Reference",
                                     "zde.help.keyboardShortcutsRef",
                                     shared_delegate,
                                     @"r",
                                     NSEventModifierFlagCommand)];
    [help_menu addItem:make_cmd_item(@"Video Tutorials", "zde.help.tutorials", shared_delegate)];
    [help_menu addItem:make_cmd_item(@"Tips and Tricks", "zde.help.tips", shared_delegate)];
    [help_menu addItem:[NSMenuItem separatorItem]];

    [help_menu addItem:make_cmd_item(@"Report Issue",
                                     Commands::CommandIds::help_provide_feedback,
                                     shared_delegate)];
    [help_menu addItem:[NSMenuItem separatorItem]];

    [help_menu addItem:make_cmd_item(@"View License",
                                     Commands::CommandIds::help_view_license,
                                     shared_delegate)];
    [help_menu addItem:make_cmd_item(@"Privacy Statement", "zde.help.privacy", shared_delegate)];
    [help_menu addItem:[NSMenuItem separatorItem]];

    [help_menu addItem:make_cmd_item(@"Toggle Developer Tools",
                                     Commands::CommandIds::help_toggle_developer_tools,
                                     shared_delegate,
                                     @"I",
                                     NSEventModifierFlagOption | NSEventModifierFlagCommand)];
    [help_menu addItem:make_cmd_item(@"Open Process Explorer",
                                     Commands::CommandIds::help_open_process_explorer,
                                     shared_delegate)];
    [help_menu addItem:[NSMenuItem separatorItem]];

    [help_menu addItem:make_cmd_item(@"Check for Updates...",
                                     Commands::CommandIds::help_check_for_updates,
                                     shared_delegate)];
    [help_menu addItem:make_cmd_item([@"About " stringByAppendingString:app_name],
                                     Commands::CommandIds::help_about,
                                     shared_delegate)];

    [main_menu addItem:make_submenu(@"Help", help_menu)];
    [NSApp setHelpMenu:help_menu];

    [NSApp setMainMenu:main_menu];
}

} // namespace Zenvra::Platform::Cocoa::Runtime
