#import "Platform/Cocoa/Components/ZenvraContentView.h"

#include "Platform/Cocoa/Components/CocoaChromeRenderer.h"
#include "Platform/Cocoa/Components/FileDropTarget.h"
#include "UI/Components/MenuModel.h"
#include "Commands/CommandIds.h"
#include "Language/LanguageServerManager.h"

#import <Cocoa/Cocoa.h>

using namespace Zenvra::Platform::Cocoa::Components;

@interface ZenvraMenuTracker : NSObject {
@public
    ZenvraContentView* _view;
    NSMenu* _menu;
    std::size_t _menuIndex;
    NSTimer* _timer;
}
- (id)initWithView:(ZenvraContentView*)view menu:(NSMenu*)menu index:(std::size_t)index;
- (void)start;
- (void)stop;
- (void)checkMouse:(NSTimer*)timer;
@end

@implementation ZenvraMenuTracker
- (id)initWithView:(ZenvraContentView*)view menu:(NSMenu*)menu index:(std::size_t)index {
    self = [super init];
    if (self) {
        _view = view;
        _menu = menu;
        _menuIndex = index;
        _timer = nil;
    }
    return self;
}

- (void)start {
    _timer = [NSTimer timerWithTimeInterval:0.008
                                     target:self
                                     selector:@selector(checkMouse:)
                                     userInfo:nil
                                     repeats:YES];
    [[NSRunLoop currentRunLoop] addTimer:_timer forMode:NSEventTrackingRunLoopMode];
    [[NSRunLoop currentRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes];
}

- (void)stop {
    if (_timer) {
        [_timer invalidate];
        _timer = nil;
    }
}

- (void)checkMouse:(NSTimer*)timer {
    (void)timer;
    if (!_view || !_menu) return;
    
    NSWindow* window = [_view window];
    if (!window) return;
    
    NSPoint screenLoc = [NSEvent mouseLocation];
    NSRect screenRect = NSMakeRect(screenLoc.x, screenLoc.y, 1, 1);
    NSRect windowRect = [window convertRectFromScreen:screenRect];
    NSPoint viewLoc = [_view convertPoint:windowRect.origin fromView:nil];
    
    [_view checkToolbarMenuHoverAtPointX:viewLoc.x pointY:viewLoc.y currentMenuIndex:_menuIndex menuToCancel:_menu];
}

- (void)dealloc {
    [self stop];
    [super dealloc];
}
@end

@implementation ZenvraContentView {
    CocoaChromeRenderer* _renderer;
    Zenvra::UI::Chrome::WindowChromeLayoutResult _chrome_layout;
    ChromeInteractionState _interaction_state;
    Zenvra::Platform::CommandStateQueryCallback _command_callback;
    Zenvra::Platform::CommandInvokedCallback _command_invoked_callback;
    
    NSTrackingArea* _trackingArea;
    bool _is_drag_active;
    
    float _scroll_remainder_x;
    float _scroll_remainder_y;

    NSMenu* _activeToolbarMenu;
    std::optional<std::size_t> _activeToolbarIndex;
    std::optional<std::size_t> _pendingMenuIndex;
    Zenvra::UI::Rect _pendingMenuBounds;
}

@synthesize renderer = _renderer;

- (instancetype)initWithFrame:(NSRect)frameRect {
    self = [super initWithFrame:frameRect];
    if (self) {
        // Register for drag and drop
        [self registerForDraggedTypes:@[NSPasteboardTypeFileURL]];
        _is_drag_active = false;
        _scroll_remainder_x = 0.0f;
        _scroll_remainder_y = 0.0f;
        _activeToolbarMenu = nil;
        _activeToolbarIndex = std::nullopt;
        _pendingMenuIndex = std::nullopt;
        
        // Accept first responder to get keyboard events
        [[self window] makeFirstResponder:self];
    }
    return self;
}

- (void)updateLayout:(const Zenvra::UI::Chrome::WindowChromeLayoutResult&)layout {
    _chrome_layout = layout;
    
    // Update tracking area for mouse hover
    if (_trackingArea) {
        [self removeTrackingArea:_trackingArea];
        [_trackingArea release];
    }
    
    NSTrackingAreaOptions options = (NSTrackingMouseEnteredAndExited | NSTrackingMouseMoved | NSTrackingActiveAlways);
    _trackingArea = [[NSTrackingArea alloc] initWithRect:[self bounds] options:options owner:self userInfo:nil];
    [self addTrackingArea:_trackingArea];
}

- (void)setCommandCallback:(Zenvra::Platform::CommandStateQueryCallback)callback {
    _command_callback = std::move(callback);
}

- (void)setCommandInvokedCallback:(Zenvra::Platform::CommandInvokedCallback)callback {
    _command_invoked_callback = std::move(callback);
}

- (void)dealloc {
    if (_trackingArea) {
        [self removeTrackingArea:_trackingArea];
        [_trackingArea release];
    }
    [super dealloc];
}

- (BOOL)isFlipped {
    return YES;
}

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    if (_trackingArea) {
        [self removeTrackingArea:_trackingArea];
    }
    
    NSTrackingAreaOptions options = NSTrackingMouseMoved | 
                                    NSTrackingMouseEnteredAndExited | 
                                    NSTrackingActiveInKeyWindow |
                                    NSTrackingInVisibleRect;
                                    
    _trackingArea = [[NSTrackingArea alloc] initWithRect:[self bounds]
                                                 options:options
                                                   owner:self
                                                userInfo:nil];
    [self addTrackingArea:_trackingArea];
}

- (void)setFrameSize:(NSSize)newSize {
    [super setFrameSize:newSize];
    [self setNeedsDisplay:YES];
}

- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    if (!_renderer) return;
    
    CGContextRef context = [[NSGraphicsContext currentContext] CGContext];
    if (!context) return;
    
    NSRect bounds = [self bounds];
    _renderer->render(
        context,
        bounds.size.width,
        bounds.size.height,
        _chrome_layout,
        _interaction_state,
        _command_callback ? _command_callback : [](std::string_view){ return Zenvra::Platform::CommandPresentationState{}; }
    );
}

// --- Toolbar Popups & Events ---

- (void)checkToolbarMenuHoverAtPointX:(float)px pointY:(float)py currentMenuIndex:(std::size_t)currentMenuIndex menuToCancel:(NSMenu*)menuToCancel {
    if (py <= _chrome_layout.titlebar_bounds.bottom() && py >= _chrome_layout.titlebar_bounds.y) {
        std::optional<std::pair<std::size_t, Zenvra::UI::Rect>> target;
        if (_chrome_layout.is_compiler_button(px, py)) {
            target = std::make_pair(std::size_t{10}, _chrome_layout.compiler_bounds);
        } else if (_chrome_layout.is_platform_button(px, py)) {
            target = std::make_pair(std::size_t{11}, _chrome_layout.platform_bounds);
        } else if (_chrome_layout.is_binary_button(px, py)) {
            target = std::make_pair(std::size_t{12}, _chrome_layout.binary_bounds);
        } else if (_chrome_layout.is_gear_button(px, py)) {
            target = std::make_pair(std::size_t{13}, _chrome_layout.gear_bounds);
        } else if (_chrome_layout.is_ellipsis_button(px, py)) {
            target = std::make_pair(std::size_t{14}, _chrome_layout.ellipsis_bounds);
        }
        
        if (target && target->first != currentMenuIndex) {
            _pendingMenuIndex = target->first;
            _pendingMenuBounds = target->second;
            [menuToCancel cancelTrackingWithoutAnimation];
        }
    }
}

- (void)showToolbarMenu:(std::size_t)menuIndex forBounds:(const Zenvra::UI::Rect&)bounds {
    std::size_t currentIdx = menuIndex;
    Zenvra::UI::Rect currentBounds = bounds;
    
    while (true) {
        const std::span<const Zenvra::UI::Components::Menu> menus = Zenvra::UI::Components::get_window_menus();
        if (currentIdx >= menus.size()) {
            break;
        }
        
        const auto& menu_model = menus[currentIdx];
        NSMenu* popup_menu = [[NSMenu alloc] initWithTitle:[NSString stringWithUTF8String:std::string(menu_model.label).c_str()]];
        [popup_menu setAutoenablesItems:NO];
        
        // Calculate a generous, spacious minimum width (persis seperti X11/Win32)
        float min_width = 230.0F;
        for (const auto& item_model : menu_model.items) {
            float item_w = static_cast<float>(item_model.label.size()) * 9.0F + 60.0F;
            if (!item_model.shortcut.empty()) {
                item_w += static_cast<float>(item_model.shortcut.size()) * 9.0F + 40.0F;
            }
            min_width = std::max(min_width, item_w);
        }
        min_width = std::min(min_width, 450.0F);
        [popup_menu setMinimumWidth:min_width];
        
        for (const auto& item_model : menu_model.items) {
            if (item_model.separator) {
                [popup_menu addItem:[NSMenuItem separatorItem]];
            } else {
                NSString* title = [NSString stringWithUTF8String:std::string(item_model.label).c_str()];
                NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title action:@selector(toolbarMenuItemClicked:) keyEquivalent:@""];
                item.target = self;
                
                bool enabled = true;
                bool checked = false;
                if (!item_model.command_id.empty()) {
                    item.representedObject = [NSString stringWithUTF8String:std::string(item_model.command_id).c_str()];
                    if (_command_callback) {
                        auto state = _command_callback(item_model.command_id);
                        enabled = state.enabled;
                        checked = state.checked;
                    }
                    if (_renderer) {
                        const auto& cfg = _renderer->get_run_config_state();
                        if (item_model.command_id == Zenvra::Commands::CommandIds::build_debug) {
                            checked = (cfg.active_mode == Zenvra::UI::Toolbar::BuildConfigurationMode::Debug);
                        } else if (item_model.command_id == Zenvra::Commands::CommandIds::build_release) {
                            checked = (cfg.active_mode == Zenvra::UI::Toolbar::BuildConfigurationMode::Release);
                        } else if (item_model.command_id == Zenvra::Commands::CommandIds::platform_arm64 ||
                                   item_model.command_id == Zenvra::Commands::CommandIds::platform_apple_arm) {
                            checked = (cfg.active_architecture == Zenvra::UI::Toolbar::TargetArchitecture::Arm64);
                        } else if (item_model.command_id == Zenvra::Commands::CommandIds::platform_x64) {
                            checked = (cfg.active_architecture == Zenvra::UI::Toolbar::TargetArchitecture::X86_64);
                        } else if (item_model.command_id == Zenvra::Commands::CommandIds::run_zde) {
                            checked = (cfg.active_target_name == "ZDE");
                        } else if (item_model.command_id == Zenvra::Commands::CommandIds::run_tests) {
                            checked = (cfg.active_target_name == "ZDEUnitTests" || cfg.active_target_name == "Tests");
                        }
                    }
                }
                [item setEnabled:enabled ? YES : NO];
                [item setState:checked ? NSControlStateValueOn : NSControlStateValueOff];
                [popup_menu addItem:item];
            }
        }
        
        _activeToolbarMenu = popup_menu;
        _activeToolbarIndex = currentIdx;
        _pendingMenuIndex = std::nullopt;
        _interaction_state.open_menu_index = currentIdx;
        [self setNeedsDisplay:YES];
        
        ZenvraMenuTracker* tracker = [[ZenvraMenuTracker alloc] initWithView:self menu:popup_menu index:currentIdx];
        [tracker start];
        
        // Position the native macOS popup menu directly below the toolbar button
        NSPoint menu_origin = NSMakePoint(currentBounds.x, currentBounds.y + currentBounds.height + 2.0F);
        [popup_menu popUpMenuPositioningItem:nil atLocation:menu_origin inView:self];
        
        [tracker stop];
        [tracker release];
        [popup_menu release];
        
        _activeToolbarMenu = nil;
        _activeToolbarIndex.reset();
        
        if (_pendingMenuIndex) {
            currentIdx = *_pendingMenuIndex;
            currentBounds = _pendingMenuBounds;
            _pendingMenuIndex.reset();
        } else {
            _interaction_state.open_menu_index.reset();
            [self setNeedsDisplay:YES];
            break;
        }
    }
}

- (void)toolbarMenuItemClicked:(NSMenuItem*)sender {
    NSString* command_id = sender.representedObject;
    if (command_id && [command_id length] > 0) {
        std::string cmd = [command_id UTF8String];
        if (_renderer) {
            auto& ws = _renderer->get_workspace_renderer();
            auto& editor = ws.get_text_editor();

            if (cmd == Zenvra::Commands::CommandIds::build_debug) {
                _renderer->set_active_mode(Zenvra::UI::Toolbar::BuildConfigurationMode::Debug);
            } else if (cmd == Zenvra::Commands::CommandIds::build_release) {
                _renderer->set_active_mode(Zenvra::UI::Toolbar::BuildConfigurationMode::Release);
            } else if (cmd == Zenvra::Commands::CommandIds::platform_arm64 ||
                       cmd == Zenvra::Commands::CommandIds::platform_apple_arm) {
                _renderer->set_active_architecture(Zenvra::UI::Toolbar::TargetArchitecture::Arm64);
            } else if (cmd == Zenvra::Commands::CommandIds::platform_x64) {
                _renderer->set_active_architecture(Zenvra::UI::Toolbar::TargetArchitecture::X86_64);
            } else if (cmd == Zenvra::Commands::CommandIds::run_zde) {
                _renderer->set_active_target("ZDE");
            } else if (cmd == Zenvra::Commands::CommandIds::run_tests) {
                _renderer->set_active_target("ZDEUnitTests");
            } else if (cmd == Zenvra::Commands::CommandIds::project_close || cmd == "zde.project.close" || cmd == "zde.folder.close") {
                _renderer->close_project();
                Zenvra::Language::LanguageServerManager::instance().set_workspace_root({});
                Zenvra::Language::LanguageServerManager::instance().shutdown_all();
                [self setNeedsDisplay:YES];
                if (_command_invoked_callback) {
                    _command_invoked_callback(cmd);
                }
                return;
            } else if (cmd == Zenvra::Commands::CommandIds::folder_open || cmd == Zenvra::Commands::CommandIds::project_open) {
                NSOpenPanel* panel = [NSOpenPanel openPanel];
                [panel setTitle:@"Open Project Folder"];
                [panel setPrompt:@"Open"];
                [panel setCanChooseFiles:NO];
                [panel setCanChooseDirectories:YES];
                [panel setAllowsMultipleSelection:NO];
                if ([panel runModal] == NSModalResponseOK) {
                    NSURL* url = [[panel URLs] firstObject];
                    if (url && [url isFileURL]) {
                        std::filesystem::path root = [[url path] UTF8String];
                        Zenvra::Language::LanguageServerManager::instance().set_workspace_root(root);
                        _renderer->set_workspace_root(root);
                        [self setNeedsDisplay:YES];
                    }
                }
                return;
            } else if (cmd == Zenvra::Commands::CommandIds::file_open) {
                NSOpenPanel* panel = [NSOpenPanel openPanel];
                [panel setTitle:@"Open File"];
                [panel setPrompt:@"Open"];
                [panel setCanChooseFiles:YES];
                [panel setCanChooseDirectories:NO];
                [panel setAllowsMultipleSelection:YES];
                if ([panel runModal] == NSModalResponseOK) {
                    for (NSURL* url in [panel URLs]) {
                        if (url && [url isFileURL]) {
                            std::filesystem::path filePath = [[url path] UTF8String];
                            editor.open_file(filePath);
                        }
                    }
                    [self setNeedsDisplay:YES];
                }
                return;
            } else if (cmd == Zenvra::Commands::CommandIds::file_new) {
                editor.create_buffer();
                [self setNeedsDisplay:YES];
                return;
            } else if (cmd == "zde.terminal.new" || cmd == Zenvra::Commands::CommandIds::view_terminal_panel) {
                ws.create_terminal();
                [self setNeedsDisplay:YES];
                return;
            } else if (cmd == Zenvra::Commands::CommandIds::file_close_all) {
                editor.close_all_documents();
                [self setNeedsDisplay:YES];
                return;
            }
        }
        if (_command_invoked_callback) {
            _command_invoked_callback(cmd);
        }
        [self setNeedsDisplay:YES];
    }
}

// --- Mouse Events ---

- (void)mouseDown:(NSEvent *)event {
    if (!_renderer) return;
    
    NSPoint location = [self convertPoint:[event locationInWindow] fromView:nil];
    float px = location.x;
    float py = location.y;
    int cw = [self bounds].size.width;
    int ch = [self bounds].size.height;
    
    // Handle toolbar actions and dropdown popups if click is inside titlebar
    if (py <= _chrome_layout.titlebar_bounds.bottom()) {
        if (_chrome_layout.is_build_button(px, py)) {
            if (_command_invoked_callback) {
                _command_invoked_callback(Zenvra::Commands::CommandIds::build_build_project);
            }
            [self setNeedsDisplay:YES];
            return;
        }
        if (_chrome_layout.is_run_button(px, py)) {
            if (_command_invoked_callback) {
                _command_invoked_callback(Zenvra::Commands::CommandIds::run_start);
            }
            [self setNeedsDisplay:YES];
            return;
        }
        if (_chrome_layout.is_debug_button(px, py)) {
            if (_command_invoked_callback) {
                _command_invoked_callback(Zenvra::Commands::CommandIds::view_problems);
            }
            [self setNeedsDisplay:YES];
            return;
        }
        if (_chrome_layout.is_compiler_button(px, py)) {
            [self showToolbarMenu:10 forBounds:_chrome_layout.compiler_bounds];
            [self setNeedsDisplay:YES];
            return;
        }
        if (_chrome_layout.is_platform_button(px, py)) {
            [self showToolbarMenu:11 forBounds:_chrome_layout.platform_bounds];
            [self setNeedsDisplay:YES];
            return;
        }
        if (_chrome_layout.is_binary_button(px, py)) {
            [self showToolbarMenu:12 forBounds:_chrome_layout.binary_bounds];
            [self setNeedsDisplay:YES];
            return;
        }
        if (_chrome_layout.is_gear_button(px, py)) {
            [self showToolbarMenu:13 forBounds:_chrome_layout.gear_bounds];
            [self setNeedsDisplay:YES];
            return;
        }
        if (_chrome_layout.is_ellipsis_button(px, py)) {
            [self showToolbarMenu:14 forBounds:_chrome_layout.ellipsis_bounds];
            [self setNeedsDisplay:YES];
            return;
        }
    }
    
    std::string command;
    bool extend = ([event modifierFlags] & NSEventModifierFlagShift) != 0;
    
    [[self window] makeFirstResponder:self];
    
    _is_drag_active = _renderer->handle_workspace_pointer_press(
        px, py, cw, ch, _chrome_layout.titlebar_bounds.bottom(),
        extend, [event clickCount], [event timestamp], command);
        
    if (!command.empty() && _command_invoked_callback) {
        _command_invoked_callback(command);
    } else if (!command.empty() && _command_callback) {
        _command_callback(command);
    }
    
    [self setNeedsDisplay:YES];
    
    if (_is_drag_active) {
        return;
    }
    
    if (_chrome_layout.is_drag_region(px, py)) {
        if ([event clickCount] == 2) {
            [[self window] performZoom:nil];
        } else {
            [[self window] performWindowDragWithEvent:event];
        }
        return;
    }
}

- (void)mouseMoved:(NSEvent *)event {
    if (!_renderer) return;
    
    NSPoint location = [self convertPoint:[event locationInWindow] fromView:nil];
    float px = location.x;
    float py = location.y;
    int cw = [self bounds].size.width;
    int ch = [self bounds].size.height;
    
    bool needs_display = false;
    
    if (_renderer->update_chrome_hover_state(px, py, _chrome_layout, _interaction_state)) {
        needs_display = true;
    }
    
    if (!_interaction_state.open_menu_index) {
        if (_renderer->handle_workspace_pointer_move(px, py, cw, ch, _chrome_layout.titlebar_bounds.bottom())) {
            needs_display = true;
        }
    }

    // Dynamic cursor feedback for resizers
    const float content_top = _chrome_layout.titlebar_bounds.bottom();
    auto& ws = _renderer->get_workspace_renderer();
    if (ws.is_editor_split_resize_handle(px, py, cw, ch, content_top) ||
        ws.is_editor_split_resizing() ||
        ws.is_sidebar_resize_handle_point(px, py, cw, ch, content_top) ||
        ws.is_sidebar_resizing()) {
        [[NSCursor resizeLeftRightCursor] set];
    } else if (ws.is_terminal_resize_handle_point(px, py, cw, ch, content_top) ||
               ws.is_terminal_resizing()) {
        [[NSCursor resizeUpDownCursor] set];
    } else {
        [[NSCursor arrowCursor] set];
    }
    
    if (needs_display) {
        [self setNeedsDisplay:YES];
    }
}

- (void)mouseExited:(NSEvent *)event {
    if (!_renderer) return;
    
    if (!_interaction_state.open_menu_index) {
        if (_renderer->update_chrome_hover_state(-1.0F, -1.0F, _chrome_layout, _interaction_state)) {
            [self setNeedsDisplay:YES];
        }
    }
}

- (void)mouseDragged:(NSEvent *)event {
    if (!_renderer || !_is_drag_active) return;
    
    NSPoint location = [self convertPoint:[event locationInWindow] fromView:nil];
    float px = location.x;
    float py = location.y;
    int cw = [self bounds].size.width;
    int ch = [self bounds].size.height;
    
    if (_renderer->handle_workspace_pointer_drag(px, py, cw, ch, _chrome_layout.titlebar_bounds.bottom())) {
        [self setNeedsDisplay:YES];
    }

    const float content_top = _chrome_layout.titlebar_bounds.bottom();
    auto& ws = _renderer->get_workspace_renderer();
    if (ws.is_editor_split_resizing() || ws.is_sidebar_resizing()) {
        [[NSCursor resizeLeftRightCursor] set];
    } else if (ws.is_terminal_resizing()) {
        [[NSCursor resizeUpDownCursor] set];
    }
}

- (void)mouseUp:(NSEvent *)event {
    if (!_renderer) return;
    if (_renderer->handle_workspace_pointer_release()) {
        [self setNeedsDisplay:YES];
    }
    _is_drag_active = false;
}

- (void)rightMouseDown:(NSEvent *)event {
    if (!_renderer) return;

    const NSPoint location = [self convertPoint:[event locationInWindow] fromView:nil];
    const float px = static_cast<float>(location.x);
    const float py = static_cast<float>(location.y);
    const int cw = static_cast<int>(self.bounds.size.width);
    const int ch = static_cast<int>(self.bounds.size.height);
    const float content_top = _chrome_layout.titlebar_bounds.bottom();

    auto& ws = _renderer->get_workspace_renderer();
    auto& editor = ws.get_text_editor();

    if (ws.is_tool_sidebar_point(px, py, cw, ch, content_top)) {
        const auto layout = ws.calculate_layout(cw, ch, content_top);
        std::filesystem::path target_path = ws.get_tool_sidebar().get_model().get_workspace_root();
        if (auto row = ws.get_tool_sidebar().row_at_point(layout, py)) {
            const auto& items = ws.get_tool_sidebar().get_model().get_project_items();
            if (*row < items.size()) {
                target_path = items[*row].path;
                static_cast<void>(ws.get_tool_sidebar().get_model().activate_project_item(*row));
            }
        }
        [self showNativeExplorerMenuForPath:target_path withEvent:event];
        [self setNeedsDisplay:YES];
        return;
    }

    if (ws.is_editor_point(px, py, cw, ch, content_top)) {
        const auto layout = ws.calculate_layout(cw, ch, content_top);
        std::string cmd_out;

        if (editor.get_document() != nullptr) {
            editor.handle_pointer_press(ws, layout, px, py, false, 1, cmd_out);
            [self showNativeEditorContextMenuWithEvent:event];
        } else {
            [self showNativeEmptyStateContextMenuWithEvent:event];
        }
        [self setNeedsDisplay:YES];
        return;
    }
}

- (void)showNativeEditorContextMenuWithEvent:(NSEvent*)event {
    NSMenu* menu = [[NSMenu alloc] initWithTitle:@"Editor Context"];
    [menu setAutoenablesItems:NO];

    auto add_item = [&](NSString* title, NSString* key_equiv, NSEventModifierFlags modifiers, std::string_view cmd) {
        NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title
                                                      action:@selector(onEditorContextMenuItemClicked:)
                                               keyEquivalent:key_equiv ? key_equiv : @""];
        item.target = self;
        if (key_equiv && [key_equiv length] > 0) {
            item.keyEquivalentModifierMask = modifiers;
        }
        item.representedObject = [NSString stringWithUTF8String:std::string(cmd).c_str()];
        [menu addItem:item];
        [item release];
    };

    auto add_separator = [&]() {
        [menu addItem:[NSMenuItem separatorItem]];
    };

    NSString* f12Str = [NSString stringWithFormat:@"%C", (unichar)NSF12FunctionKey];
    NSString* f2Str = [NSString stringWithFormat:@"%C", (unichar)NSF2FunctionKey];

    add_item(@"Go to Definition", f12Str, 0, "zde.editor.goToDefinition");
    add_item(@"Go to Declaration", @"", 0, "zde.editor.goToDeclaration");
    add_item(@"Go to Implementations", f12Str, NSEventModifierFlagCommand, "zde.editor.goToImplementation");
    add_item(@"Go to References", f12Str, NSEventModifierFlagShift, "zde.editor.goToReferences");
    add_separator();
    add_item(@"Find All References", f12Str, NSEventModifierFlagShift | NSEventModifierFlagOption, "zde.editor.findAllReferences");
    add_item(@"Find All Implementations", @"", 0, "zde.editor.findAllImplementations");
    add_item(@"Show Call Hierarchy", @"h", NSEventModifierFlagShift | NSEventModifierFlagOption, "zde.editor.showCallHierarchy");
    add_item(@"Switch Between Source/Header", @"o", NSEventModifierFlagCommand | NSEventModifierFlagOption, "zde.editor.switchHeaderSource");
    add_separator();
    add_item(@"Rename Symbol", f2Str, 0, "zde.editor.renameSymbol");
    add_item(@"Change All Occurrences", f2Str, NSEventModifierFlagCommand, "zde.selection.selectAllOccurrences");
    add_item(@"Format Document", @"f", NSEventModifierFlagShift | NSEventModifierFlagOption, "zde.editor.formatDocument");
    add_item(@"Format Document With...", @"", 0, "zde.editor.formatDocumentWith");
    add_item(@"Refactor...", @"r", NSEventModifierFlagControl | NSEventModifierFlagShift, "zde.editor.refactor");
    add_separator();
    add_item(@"Cut", @"x", NSEventModifierFlagCommand, "zde.edit.cut");
    add_item(@"Copy", @"c", NSEventModifierFlagCommand, "zde.edit.copy");
    add_item(@"Paste", @"v", NSEventModifierFlagCommand, "zde.edit.paste");
    add_separator();
    add_item(@"Command Palette...", @"p", NSEventModifierFlagShift | NSEventModifierFlagCommand, "zde.help.showAllCommands");

    [NSMenu popUpContextMenu:menu withEvent:event forView:self];
    [menu release];
}

- (void)showNativeEmptyStateContextMenuWithEvent:(NSEvent*)event {
    NSMenu* menu = [[NSMenu alloc] initWithTitle:@"Empty State Context"];
    [menu setAutoenablesItems:NO];

    auto add_item = [&](NSString* title, NSString* key_equiv, NSEventModifierFlags modifiers, std::string_view cmd) {
        NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title
                                                      action:@selector(onEditorContextMenuItemClicked:)
                                               keyEquivalent:key_equiv ? key_equiv : @""];
        item.target = self;
        if (key_equiv && [key_equiv length] > 0) {
            item.keyEquivalentModifierMask = modifiers;
        }
        item.representedObject = [NSString stringWithUTF8String:std::string(cmd).c_str()];
        [menu addItem:item];
        [item release];
    };

    auto add_separator = [&]() {
        [menu addItem:[NSMenuItem separatorItem]];
    };

    add_item(@"New Text File", @"n", NSEventModifierFlagCommand, "zde.file.new");
    add_item(@"Open File...", @"o", NSEventModifierFlagCommand, "zde.file.open");
    add_separator();
    add_item(@"New Terminal", @"`", NSEventModifierFlagControl, "zde.view.terminalPanel");
    add_separator();
    add_item(@"Split Up", @"\\", NSEventModifierFlagCommand, "zde.view.splitUp");
    add_item(@"Split Down", @"\\", NSEventModifierFlagCommand, "zde.view.splitDown");
    add_item(@"Split Left", @"\\", NSEventModifierFlagCommand, "zde.view.splitLeft");
    add_item(@"Split Right", @"\\", NSEventModifierFlagCommand, "zde.view.splitRight");
    add_separator();
    add_item(@"New Window", @"n", NSEventModifierFlagCommand | NSEventModifierFlagShift, "zde.window.new");

    [NSMenu popUpContextMenu:menu withEvent:event forView:self];
    [menu release];
}

- (void)onEditorContextMenuItemClicked:(NSMenuItem*)sender {
    NSString* command_id = sender.representedObject;
    if (!command_id || [command_id length] == 0) return;
    std::string cmd = [command_id UTF8String];

    if (_renderer) {
        auto& ws = _renderer->get_workspace_renderer();
        auto& editor = ws.get_text_editor();

        if (cmd == Zenvra::Commands::CommandIds::view_split_up ||
            cmd == Zenvra::Commands::CommandIds::view_split_down ||
            cmd == Zenvra::Commands::CommandIds::view_split_left ||
            cmd == Zenvra::Commands::CommandIds::view_split_right) {
            editor.split_editor();
            [self setNeedsDisplay:YES];
            return;
        }

        if (cmd == Zenvra::Commands::CommandIds::file_new) {
            editor.create_buffer();
            [self setNeedsDisplay:YES];
            return;
        }

        if (cmd == Zenvra::Commands::CommandIds::file_open) {
            NSOpenPanel* panel = [NSOpenPanel openPanel];
            [panel setTitle:@"Open File"];
            [panel setPrompt:@"Open"];
            [panel setCanChooseFiles:YES];
            [panel setCanChooseDirectories:NO];
            [panel setAllowsMultipleSelection:YES];
            if ([panel runModal] == NSModalResponseOK) {
                for (NSURL* url in [panel URLs]) {
                    if (url && [url isFileURL]) {
                        std::filesystem::path filePath = [[url path] UTF8String];
                        editor.open_file(filePath);
                    }
                }
                [self setNeedsDisplay:YES];
            }
            return;
        }

        if (cmd == "zde.terminal.new" || cmd == Zenvra::Commands::CommandIds::view_terminal_panel) {
            ws.create_terminal();
            [self setNeedsDisplay:YES];
            return;
        }

        if (cmd == Zenvra::Commands::CommandIds::file_close_all) {
            editor.close_all_documents();
            [self setNeedsDisplay:YES];
            return;
        }

        if (cmd == Zenvra::Commands::CommandIds::project_close || cmd == "zde.project.close" || cmd == "zde.folder.close") {
            _renderer->close_project();
            Zenvra::Language::LanguageServerManager::instance().set_workspace_root({});
            Zenvra::Language::LanguageServerManager::instance().shutdown_all();
            [self setNeedsDisplay:YES];
            return;
        }

        auto res = ws.handle_editor_command(cmd);
        if (res.has_value() && *res) {
            [self setNeedsDisplay:YES];
            return;
        }

        auto ed_res = editor.handle_command(cmd);
        if (ed_res.has_value() && *ed_res) {
            [self setNeedsDisplay:YES];
            return;
        }
    }

    if (_command_invoked_callback) {
        _command_invoked_callback(cmd);
    }
    [self setNeedsDisplay:YES];
}

- (void)showNativeExplorerMenuForPath:(const std::filesystem::path&)target_path withEvent:(NSEvent*)event {
    NSMenu* menu = [[NSMenu alloc] initWithTitle:@"Explorer"];
    [menu setAutoenablesItems:NO];

    auto add_item = [&](NSString* title, NSString* key_equiv, NSEventModifierFlags modifiers, std::string_view cmd) {
        NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title
                                                      action:@selector(onExplorerMenuItemClicked:)
                                               keyEquivalent:key_equiv ? key_equiv : @""];
        item.target = self;
        if (key_equiv && [key_equiv length] > 0) {
            item.keyEquivalentModifierMask = modifiers;
        }
        item.representedObject = [NSDictionary dictionaryWithObjectsAndKeys:
            [NSString stringWithUTF8String:std::string(cmd).c_str()], @"command",
            [NSString stringWithUTF8String:target_path.string().c_str()], @"path",
            nil];
        [menu addItem:item];
        [item release];
    };

    auto add_separator = [&]() {
        [menu addItem:[NSMenuItem separatorItem]];
    };

    add_item(@"New File...", @"", 0, "zde.explorer.newFile");
    add_item(@"New Folder...", @"", 0, "zde.explorer.newFolder");
    add_separator();
    add_item(@"Open to the Side", @"\r", NSEventModifierFlagCommand, "zde.explorer.openToSide");
    add_item(@"Reveal in Finder", @"r", NSEventModifierFlagShift | NSEventModifierFlagOption, "zde.explorer.reveal");
    add_item(@"Open in Integrated Terminal", @"", 0, "zde.explorer.openTerminal");
    add_separator();
    add_item(@"Cut", @"x", NSEventModifierFlagCommand, "zde.explorer.cut");
    add_item(@"Copy", @"c", NSEventModifierFlagCommand, "zde.explorer.copy");
    add_item(@"Paste", @"v", NSEventModifierFlagCommand, "zde.explorer.paste");
    add_separator();
    add_item(@"Copy Path", @"c", NSEventModifierFlagShift | NSEventModifierFlagOption, "zde.explorer.copyPath");
    add_item(@"Copy Relative Path", @"", 0, "zde.explorer.copyRelativePath");
    add_separator();
    add_item(@"Rename...", @"", 0, "zde.explorer.rename");
    add_item(@"Delete", [NSString stringWithFormat:@"%C", (unichar)NSBackspaceCharacter], NSEventModifierFlagCommand, "zde.explorer.delete");

    [NSMenu popUpContextMenu:menu withEvent:event forView:self];
    [menu release];
}

- (void)onExplorerMenuItemClicked:(NSMenuItem*)sender {
    NSDictionary* dict = sender.representedObject;
    if (!dict || !_renderer) return;

    NSString* cmd = [dict objectForKey:@"command"];
    NSString* pathStr = [dict objectForKey:@"path"];
    if (cmd && pathStr) {
        const std::string command = [cmd UTF8String];
        const std::filesystem::path target_path = [pathStr UTF8String];
        _renderer->get_workspace_renderer().execute_explorer_command(command, target_path);
        [self setNeedsDisplay:YES];
    }
}


- (void)scrollWheel:(NSEvent *)event {
    if (!_renderer) return;
    
    NSPoint location = [self convertPoint:[event locationInWindow] fromView:nil];
    float px = location.x;
    float py = location.y;
    int cw = [self bounds].size.width;
    int ch = [self bounds].size.height;
    
    NSEventPhase phase = [event phase];
    NSEventPhase momentumPhase = [event momentumPhase];
    
    // Ignore momentum / inertia drift animation so scrolling is immediate and stops directly
    if (momentumPhase != NSEventPhaseNone && momentumPhase != NSEventPhaseEnded && momentumPhase != NSEventPhaseCancelled) {
        _scroll_remainder_x = 0.0f;
        _scroll_remainder_y = 0.0f;
        return;
    }
    
    // Reset accumulators when gesture begins or ends
    if (phase == NSEventPhaseBegan || phase == NSEventPhaseEnded || phase == NSEventPhaseCancelled) {
        _scroll_remainder_x = 0.0f;
        _scroll_remainder_y = 0.0f;
    }
    
    float raw_dy = [event scrollingDeltaY];
    float raw_dx = [event scrollingDeltaX];
    
    if (raw_dy == 0.0f && raw_dx == 0.0f) {
        return;
    }
    
    // Immediate direction reversal: clear opposite remainder so there is no wind-up or stuck resistance at edges
    if ((raw_dy > 0.0f && _scroll_remainder_y < 0.0f) || (raw_dy < 0.0f && _scroll_remainder_y > 0.0f)) {
        _scroll_remainder_y = 0.0f;
    }
    if ((raw_dx > 0.0f && _scroll_remainder_x < 0.0f) || (raw_dx < 0.0f && _scroll_remainder_x > 0.0f)) {
        _scroll_remainder_x = 0.0f;
    }
    
    // Dominant axis separation: prevent diagonal cross-talk fighting on tab bar / editor
    const bool is_horizontal = (std::abs(raw_dx) > std::abs(raw_dy));
    
    std::ptrdiff_t delta_y = 0;
    std::ptrdiff_t delta_x = 0;
    
    if ([event hasPreciseScrollingDeltas]) {
        // Trackpad / Magic Mouse precise point deltas
        constexpr float points_per_line_y = 16.0f;
        constexpr float points_per_col_x = 12.0f;
        
        if (is_horizontal) {
            _scroll_remainder_x += raw_dx;
            if (std::abs(_scroll_remainder_x) >= points_per_col_x) {
                float cols_x = std::trunc(_scroll_remainder_x / points_per_col_x);
                delta_x = -static_cast<std::ptrdiff_t>(cols_x);
                _scroll_remainder_x -= cols_x * points_per_col_x;
            }
            _scroll_remainder_x = std::clamp(_scroll_remainder_x, -points_per_col_x, points_per_col_x);
        } else {
            _scroll_remainder_y += raw_dy;
            if (std::abs(_scroll_remainder_y) >= points_per_line_y) {
                float lines_y = std::trunc(_scroll_remainder_y / points_per_line_y);
                delta_y = -static_cast<std::ptrdiff_t>(lines_y);
                _scroll_remainder_y -= lines_y * points_per_line_y;
            }
            _scroll_remainder_y = std::clamp(_scroll_remainder_y, -points_per_line_y, points_per_line_y);
        }
    } else {
        // Standard discrete mouse wheel (identical to X11 -3 / +3 line steps)
        if (is_horizontal) {
            constexpr float lines_per_notch = 3.0f;
            _scroll_remainder_x += raw_dx * lines_per_notch;
            if (std::abs(_scroll_remainder_x) >= 1.0f) {
                float cols_x = std::trunc(_scroll_remainder_x);
                delta_x = -static_cast<std::ptrdiff_t>(cols_x);
                _scroll_remainder_x -= cols_x;
            }
            _scroll_remainder_x = std::clamp(_scroll_remainder_x, -1.0f, 1.0f);
        } else {
            constexpr float lines_per_notch = 3.0f;
            _scroll_remainder_y += raw_dy * lines_per_notch;
            if (std::abs(_scroll_remainder_y) >= 1.0f) {
                float lines_y = std::trunc(_scroll_remainder_y);
                delta_y = -static_cast<std::ptrdiff_t>(lines_y);
                _scroll_remainder_y -= lines_y;
            }
            _scroll_remainder_y = std::clamp(_scroll_remainder_y, -1.0f, 1.0f);
        }
    }
    
    std::string command;
    bool needs_update = false;
    
    if (delta_y != 0) {
        bool scrolled = _renderer->handle_workspace_scroll(
            px, py, command, delta_y, false, cw, ch, _chrome_layout.titlebar_bounds.bottom());
        if (!scrolled) {
            _scroll_remainder_y = 0.0f;
        }
        needs_update |= scrolled;
    }
    if (delta_x != 0) {
        bool scrolled = _renderer->handle_workspace_scroll(
            px, py, command, delta_x, true, cw, ch, _chrome_layout.titlebar_bounds.bottom());
        if (!scrolled) {
            _scroll_remainder_x = 0.0f;
        }
        needs_update |= scrolled;
    }
    
    if (needs_update) {
        [self setNeedsDisplay:YES];
    }
}

// --- Keyboard Events ---

- (void)keyDown:(NSEvent *)event {
    if (!_renderer) return;
    
    // When the integrated terminal has focus, route keys straight to it.
    if (_renderer->is_terminal_focused()) {
        const NSEventModifierFlags modifiers = [event modifierFlags];
        const bool has_cmd = (modifiers & NSEventModifierFlagCommand) != 0;
        const bool has_ctrl = (modifiers & NSEventModifierFlagControl) != 0;
        const bool has_alt = (modifiers & NSEventModifierFlagOption) != 0;
        const unsigned short keyCode = [event keyCode];
        NSString* chars = [event charactersIgnoringModifiers];
        
        // Ctrl+letter → control character (e.g. Ctrl+C, Ctrl+D, Ctrl+L).
        if (has_ctrl && !has_cmd && !has_alt && [chars length] == 1) {
            const unichar character = [chars characterAtIndex:0];
            if (character >= 'a' && character <= 'z') {
                if (_renderer->handle_terminal_control((char)character)) {
                    [self setNeedsDisplay:YES];
                }
                return;
            }
            if (character >= 'A' && character <= 'Z') {
                if (_renderer->handle_terminal_control((char)(character + 32))) {
                    [self setNeedsDisplay:YES];
                }
                return;
            }
        }

        // Direct macOS KeyCode routing for editing & navigation keys
        std::optional<Zenvra::Terminal::TerminalInputKey> terminal_key;
        switch (keyCode) {
            case 0x24: // Return / Enter
            case 0x4C: // Keypad Enter
                terminal_key = Zenvra::Terminal::TerminalInputKey::Enter;
                break;
            case 0x33: // Backspace (Delete)
                terminal_key = Zenvra::Terminal::TerminalInputKey::Backspace;
                break;
            case 0x75: // Forward Delete
                terminal_key = Zenvra::Terminal::TerminalInputKey::DeleteForward;
                break;
            case 0x30: // Tab
                terminal_key = Zenvra::Terminal::TerminalInputKey::Tab;
                break;
            case 0x35: // Escape
                terminal_key = Zenvra::Terminal::TerminalInputKey::Escape;
                break;
            case 0x7E: // Up Arrow
                terminal_key = Zenvra::Terminal::TerminalInputKey::ArrowUp;
                break;
            case 0x7D: // Down Arrow
                terminal_key = Zenvra::Terminal::TerminalInputKey::ArrowDown;
                break;
            case 0x7B: // Left Arrow
                terminal_key = Zenvra::Terminal::TerminalInputKey::ArrowLeft;
                break;
            case 0x7C: // Right Arrow
                terminal_key = Zenvra::Terminal::TerminalInputKey::ArrowRight;
                break;
            case 0x73: // Home
                terminal_key = Zenvra::Terminal::TerminalInputKey::Home;
                break;
            case 0x77: // End
                terminal_key = Zenvra::Terminal::TerminalInputKey::End;
                break;
            default:
                break;
        }

        if (terminal_key) {
            if (_renderer->handle_terminal_key(*terminal_key)) {
                [self setNeedsDisplay:YES];
            }
            return;
        }

        // Secondary fallback via unichar
        if ([chars length] == 1) {
            const unichar character = [chars characterAtIndex:0];
            switch (character) {
                case NSEnterCharacter:
                case NSNewlineCharacter:
                case NSCarriageReturnCharacter:
                    terminal_key = Zenvra::Terminal::TerminalInputKey::Enter;
                    break;
                case NSBackspaceCharacter:
                case NSDeleteCharacter:
                    terminal_key = Zenvra::Terminal::TerminalInputKey::Backspace;
                    break;
                case 0x1B: // Escape
                    terminal_key = Zenvra::Terminal::TerminalInputKey::Escape;
                    break;
                case 0x09: // Tab
                    terminal_key = Zenvra::Terminal::TerminalInputKey::Tab;
                    break;
                default:
                    break;
            }
            if (terminal_key) {
                if (_renderer->handle_terminal_key(*terminal_key)) {
                    [self setNeedsDisplay:YES];
                }
                return;
            }
        }
        
        // Printable text → active terminal session.
        NSString* text = [event characters];
        if (text && [text length] > 0) {
            if (_renderer->handle_text_input([text UTF8String])) {
                [self setNeedsDisplay:YES];
            }
        }
        return;
    }
    
    if (_renderer->is_editor_focused()) {
        const NSEventModifierFlags modifiers = [event modifierFlags];
        const bool has_cmd = (modifiers & NSEventModifierFlagCommand) != 0;
        const bool has_ctrl = (modifiers & NSEventModifierFlagControl) != 0;
        const bool has_alt = (modifiers & NSEventModifierFlagOption) != 0;
        const bool has_shift = (modifiers & NSEventModifierFlagShift) != 0;
        const unsigned short keyCode = [event keyCode];

        NSString* chars = [event charactersIgnoringModifiers];
        const unichar character = [chars length] > 0 ? [chars characterAtIndex:0] : 0;

        // 1. Escape
        if (character == 0x1B || keyCode == 53) {
            if (_renderer->handle_editor_input(
                    Zenvra::UI::Editor::EditorInputCommand::Escape, false)) {
                [self setNeedsDisplay:YES];
                return;
            }
        }

        // 2. Backspace
        if (character == NSBackspaceCharacter || character == NSDeleteCharacter || character == 0x7F || keyCode == 51) {
            if (_renderer->handle_editor_input(
                    Zenvra::UI::Editor::EditorInputCommand::DeleteBackward, false)) {
                [self setNeedsDisplay:YES];
                return;
            }
        }

        // 3. Forward Delete (fn + backspace or full keyboard delete)
        if (character == NSDeleteFunctionKey || keyCode == 117) {
            if (_renderer->handle_editor_input(
                    Zenvra::UI::Editor::EditorInputCommand::DeleteForward, false)) {
                [self setNeedsDisplay:YES];
                return;
            }
        }

        // 4. Return / Enter
        if (character == NSEnterCharacter || character == NSNewlineCharacter || character == NSCarriageReturnCharacter || keyCode == 36 || keyCode == 76) {
            if (_renderer->handle_editor_input(
                    Zenvra::UI::Editor::EditorInputCommand::InsertNewLine, false)) {
                [self setNeedsDisplay:YES];
                return;
            }
        }

        // 5. Tab
        if (character == 0x09 || keyCode == 48) {
            if (_renderer->handle_editor_input(
                    Zenvra::UI::Editor::EditorInputCommand::InsertTab, false)) {
                [self setNeedsDisplay:YES];
                return;
            }
        }

        // 6. Multi-cursor and line move shortcuts
        if ((has_ctrl && has_shift) || (has_ctrl && has_alt) || (has_cmd && has_alt) || (has_alt && has_shift)) {
            if (character == NSUpArrowFunctionKey || keyCode == 126) {
                if (_renderer->handle_editor_input(
                        Zenvra::UI::Editor::EditorInputCommand::AddCursorAbove, false)) {
                    [self setNeedsDisplay:YES];
                }
                return;
            }
            if (character == NSDownArrowFunctionKey || keyCode == 125) {
                if (_renderer->handle_editor_input(
                        Zenvra::UI::Editor::EditorInputCommand::AddCursorBelow, false)) {
                    [self setNeedsDisplay:YES];
                }
                return;
            }
        }

        if (has_alt && !has_cmd && !has_ctrl && !has_shift) {
            if (character == NSUpArrowFunctionKey || keyCode == 126) {
                if (_renderer->handle_editor_input(
                        Zenvra::UI::Editor::EditorInputCommand::MoveLineUp, false)) {
                    [self setNeedsDisplay:YES];
                }
                return;
            }
            if (character == NSDownArrowFunctionKey || keyCode == 125) {
                if (_renderer->handle_editor_input(
                        Zenvra::UI::Editor::EditorInputCommand::MoveLineDown, false)) {
                    [self setNeedsDisplay:YES];
                }
                return;
            }
        }

        // 7. Navigation arrow keys
        if (character == NSLeftArrowFunctionKey || keyCode == 123) {
            auto cmd = has_cmd ? Zenvra::UI::Editor::EditorInputCommand::MoveHome
                               : Zenvra::UI::Editor::EditorInputCommand::MoveLeft;
            if (_renderer->handle_editor_input(cmd, has_shift)) {
                [self setNeedsDisplay:YES];
            }
            return;
        }
        if (character == NSRightArrowFunctionKey || keyCode == 124) {
            auto cmd = has_cmd ? Zenvra::UI::Editor::EditorInputCommand::MoveEnd
                               : Zenvra::UI::Editor::EditorInputCommand::MoveRight;
            if (_renderer->handle_editor_input(cmd, has_shift)) {
                [self setNeedsDisplay:YES];
            }
            return;
        }
        if (character == NSUpArrowFunctionKey || keyCode == 126) {
            auto cmd = has_cmd ? Zenvra::UI::Editor::EditorInputCommand::MoveHome
                               : Zenvra::UI::Editor::EditorInputCommand::MoveUp;
            if (_renderer->handle_editor_input(cmd, has_shift)) {
                [self setNeedsDisplay:YES];
            }
            return;
        }
        if (character == NSDownArrowFunctionKey || keyCode == 125) {
            auto cmd = has_cmd ? Zenvra::UI::Editor::EditorInputCommand::MoveEnd
                               : Zenvra::UI::Editor::EditorInputCommand::MoveDown;
            if (_renderer->handle_editor_input(cmd, has_shift)) {
                [self setNeedsDisplay:YES];
            }
            return;
        }
        if (character == NSHomeFunctionKey || keyCode == 115) {
            if (_renderer->handle_editor_input(
                    Zenvra::UI::Editor::EditorInputCommand::MoveHome, has_shift)) {
                [self setNeedsDisplay:YES];
            }
            return;
        }
        if (character == NSEndFunctionKey || keyCode == 119) {
            if (_renderer->handle_editor_input(
                    Zenvra::UI::Editor::EditorInputCommand::MoveEnd, has_shift)) {
                [self setNeedsDisplay:YES];
            }
            return;
        }

        // 8. Command shortcuts (Cmd+A, Cmd+C, Cmd+V, Cmd+X, Cmd+S, Cmd+W, Cmd+N, Cmd+/)
        if (has_cmd || has_ctrl) {
            std::optional<Zenvra::UI::Editor::EditorAction> action;
            if (character == 'a' || character == 'A' || keyCode == 0) {
                action = Zenvra::UI::Editor::EditorAction::SelectAll;
            } else if (character == 'c' || character == 'C' || keyCode == 8) {
                action = Zenvra::UI::Editor::EditorAction::Copy;
            } else if (character == 'x' || character == 'X' || keyCode == 7) {
                action = Zenvra::UI::Editor::EditorAction::Cut;
            } else if (character == 'v' || character == 'V' || keyCode == 9) {
                action = Zenvra::UI::Editor::EditorAction::Paste;
            } else if (character == 's' || character == 'S' || keyCode == 1) {
                action = Zenvra::UI::Editor::EditorAction::SaveDocument;
            } else if (character == 'w' || character == 'W' || keyCode == 13) {
                action = Zenvra::UI::Editor::EditorAction::CloseDocument;
            } else if (character == 'n' || character == 'N' || keyCode == 45) {
                action = Zenvra::UI::Editor::EditorAction::CreateDocument;
            } else if (character == '/' || keyCode == 44) {
                action = Zenvra::UI::Editor::EditorAction::ToggleComment;
            }

            if (action) {
                if (_renderer->handle_editor_action(*action)) {
                    [self setNeedsDisplay:YES];
                }
                return;
            }
        }

        // 9. Standard Text Input (no Cmd modifier, not control key)
        if (!has_cmd) {
            NSString* text = [event characters];
            if (text && [text length] > 0) {
                unichar first_char = [text characterAtIndex:0];
                if (first_char >= 0x20 && (first_char < 0xF700 || first_char > 0xF8FF) && first_char != 0x7F) {
                    if (_renderer->handle_text_input([text UTF8String])) {
                        [self setNeedsDisplay:YES];
                    }
                    return;
                }
            }
        }
    }

    // Pass remaining events to input context (e.g. for IME composition)
    [self interpretKeyEvents:@[event]];
    [self setNeedsDisplay:YES];
}

// --- NSDraggingDestination ---

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {
    return NSDragOperationCopy;
}

- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
    NSPasteboard *pboard = [sender draggingPasteboard];
    if ([[pboard types] containsObject:NSPasteboardTypeFileURL]) {
        NSArray *urls = [pboard readObjectsForClasses:@[[NSURL class]] options:nil];
        if (urls && [urls count] > 0) {
            std::vector<std::filesystem::path> paths;
            for (NSURL *url in urls) {
                if ([url isFileURL]) {
                    paths.push_back([[url path] UTF8String]);
                }
            }
            if (_renderer) {
                (void)_renderer->open_dropped_paths(paths);
                [self setNeedsDisplay:YES];
            }
            return YES;
        }
    }
    return NO;
}

// --- NSTextInputClient ---

- (void)insertText:(id)string replacementRange:(NSRange)replacementRange {
    (void)replacementRange;
    if (!_renderer) return;
    NSString *text = nil;
    if ([string isKindOfClass:[NSString class]]) {
        text = string;
    } else if ([string isKindOfClass:[NSAttributedString class]]) {
        text = [(NSAttributedString *)string string];
    }
    if (text && [text length] > 0) {
        (void)_renderer->handle_text_input([text UTF8String]);
        [self setNeedsDisplay:YES];
    }
}

- (void)doCommandBySelector:(SEL)selector {
    if (!_renderer || (!_renderer->is_editor_focused() && !_renderer->is_search_focused() && !_renderer->is_source_control_focused())) return;
    
    if (selector == @selector(deleteBackward:)) {
        (void)_renderer->handle_editor_input(Zenvra::UI::Editor::EditorInputCommand::DeleteBackward, false);
    } else if (selector == @selector(deleteForward:)) {
        (void)_renderer->handle_editor_input(Zenvra::UI::Editor::EditorInputCommand::DeleteForward, false);
    } else if (selector == @selector(insertNewline:)) {
        (void)_renderer->handle_editor_input(Zenvra::UI::Editor::EditorInputCommand::InsertNewLine, false);
    } else if (selector == @selector(insertTab:)) {
        (void)_renderer->handle_editor_input(Zenvra::UI::Editor::EditorInputCommand::InsertTab, false);
    } else if (selector == @selector(moveLeft:)) {
        (void)_renderer->handle_editor_input(Zenvra::UI::Editor::EditorInputCommand::MoveLeft, false);
    } else if (selector == @selector(moveRight:)) {
        (void)_renderer->handle_editor_input(Zenvra::UI::Editor::EditorInputCommand::MoveRight, false);
    } else if (selector == @selector(moveUp:)) {
        (void)_renderer->handle_editor_input(Zenvra::UI::Editor::EditorInputCommand::MoveUp, false);
    } else if (selector == @selector(moveDown:)) {
        (void)_renderer->handle_editor_input(Zenvra::UI::Editor::EditorInputCommand::MoveDown, false);
    } else if (selector == @selector(moveLeftAndModifySelection:)) {
        (void)_renderer->handle_editor_input(Zenvra::UI::Editor::EditorInputCommand::MoveLeft, true);
    } else if (selector == @selector(moveRightAndModifySelection:)) {
        (void)_renderer->handle_editor_input(Zenvra::UI::Editor::EditorInputCommand::MoveRight, true);
    } else if (selector == @selector(moveUpAndModifySelection:)) {
        (void)_renderer->handle_editor_input(Zenvra::UI::Editor::EditorInputCommand::MoveUp, true);
    } else if (selector == @selector(moveDownAndModifySelection:)) {
        (void)_renderer->handle_editor_input(Zenvra::UI::Editor::EditorInputCommand::MoveDown, true);
    } else if (selector == @selector(moveToBeginningOfLine:) ||
               selector == @selector(moveToBeginningOfParagraph:) ||
               selector == @selector(moveToLeftEndOfLine:)) {
        (void)_renderer->handle_editor_input(Zenvra::UI::Editor::EditorInputCommand::MoveHome, false);
    } else if (selector == @selector(moveToEndOfLine:) ||
               selector == @selector(moveToEndOfParagraph:) ||
               selector == @selector(moveToRightEndOfLine:)) {
        (void)_renderer->handle_editor_input(Zenvra::UI::Editor::EditorInputCommand::MoveEnd, false);
    } else if (selector == @selector(moveToBeginningOfLineAndModifySelection:) ||
               selector == @selector(moveToBeginningOfParagraphAndModifySelection:)) {
        (void)_renderer->handle_editor_input(Zenvra::UI::Editor::EditorInputCommand::MoveHome, true);
    } else if (selector == @selector(moveToEndOfLineAndModifySelection:) ||
               selector == @selector(moveToEndOfParagraphAndModifySelection:)) {
        (void)_renderer->handle_editor_input(Zenvra::UI::Editor::EditorInputCommand::MoveEnd, true);
    } else if (selector == @selector(cancelOperation:)) {
        (void)_renderer->handle_editor_input(Zenvra::UI::Editor::EditorInputCommand::Escape, false);
    } else if (selector == @selector(selectAll:)) {
        (void)_renderer->handle_editor_action(Zenvra::UI::Editor::EditorAction::SelectAll);
    }
    [self setNeedsDisplay:YES];
}
- (void)setMarkedText:(id)string selectedRange:(NSRange)selectedRange replacementRange:(NSRange)replacementRange {}
- (void)unmarkText {}
- (NSRange)selectedRange { return NSMakeRange(NSNotFound, 0); }
- (NSRange)markedRange { return NSMakeRange(NSNotFound, 0); }
- (BOOL)hasMarkedText { return NO; }
- (NSAttributedString *)attributedSubstringForProposedRange:(NSRange)range actualRange:(NSRangePointer)actualRange { return nil; }
- (NSArray<NSAttributedStringKey> *)validAttributesForMarkedText { return @[]; }
- (NSRect)firstRectForCharacterRange:(NSRange)range actualRange:(NSRangePointer)actualRange { return NSZeroRect; }
- (NSUInteger)characterIndexForPoint:(NSPoint)point { return NSNotFound; }

@end
