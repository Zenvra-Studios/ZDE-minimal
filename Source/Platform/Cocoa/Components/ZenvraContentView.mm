#import "Platform/Cocoa/Components/ZenvraContentView.h"

#include "Platform/Cocoa/Components/CocoaChromeRenderer.h"
#include "Platform/Cocoa/Components/FileDropTarget.h"
#include "UI/Components/MenuModel.h"
#include "Commands/CommandIds.h"

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
        if (_command_invoked_callback) {
            _command_invoked_callback([command_id UTF8String]);
        }
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
    
    _is_drag_active = _renderer->handle_workspace_pointer_press(
        px, py, cw, ch, _chrome_layout.titlebar_bounds.bottom(),
        extend, [event clickCount], [event timestamp], command);
        
    if (!command.empty() && _command_invoked_callback) {
        _command_invoked_callback(command);
    } else if (!command.empty() && _command_callback) {
        _command_callback(command);
    }
    
    if (_is_drag_active) {
        [self setNeedsDisplay:YES];
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
    
    [self setNeedsDisplay:YES];
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
}

- (void)mouseUp:(NSEvent *)event {
    if (!_renderer) return;
    if (_renderer->handle_workspace_pointer_release()) {
        [self setNeedsDisplay:YES];
    }
    _is_drag_active = false;
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
        NSString* chars = [event charactersIgnoringModifiers];
        const NSEventModifierFlags modifiers = [event modifierFlags];
        const bool has_cmd = (modifiers & NSEventModifierFlagCommand) != 0;
        const bool has_ctrl = (modifiers & NSEventModifierFlagControl) != 0;
        const bool has_alt = (modifiers & NSEventModifierFlagOption) != 0;
        
        // Ctrl+letter → control character (e.g. Ctrl+C).
        if (has_ctrl && !has_cmd && !has_alt && [chars length] == 1) {
            const unichar character = [chars characterAtIndex:0];
            if (character >= 'a' && character <= 'z') {
                if (_renderer->handle_terminal_control((char)character)) {
                    [self setNeedsDisplay:YES];
                }
                return;
            }
        }
        
        if ([chars length] == 1) {
            const unichar character = [chars characterAtIndex:0];
            std::optional<Zenvra::Terminal::TerminalInputKey> terminal_key;
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
                case NSUpArrowFunctionKey:
                    terminal_key = Zenvra::Terminal::TerminalInputKey::ArrowUp;
                    break;
                case NSDownArrowFunctionKey:
                    terminal_key = Zenvra::Terminal::TerminalInputKey::ArrowDown;
                    break;
                case NSLeftArrowFunctionKey:
                    terminal_key = Zenvra::Terminal::TerminalInputKey::ArrowLeft;
                    break;
                case NSRightArrowFunctionKey:
                    terminal_key = Zenvra::Terminal::TerminalInputKey::ArrowRight;
                    break;
                case NSHomeFunctionKey:
                    terminal_key = Zenvra::Terminal::TerminalInputKey::Home;
                    break;
                case NSEndFunctionKey:
                    terminal_key = Zenvra::Terminal::TerminalInputKey::End;
                    break;
                case NSDeleteFunctionKey:
                    terminal_key = Zenvra::Terminal::TerminalInputKey::DeleteForward;
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
        
        // Printable text → the active terminal session.
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

        NSString* chars = [event charactersIgnoringModifiers];
        if ([chars length] == 1) {
            const unichar character = [chars characterAtIndex:0];
            if (character == 0x1B) {
                if (_renderer->handle_editor_input(
                        Zenvra::UI::Editor::EditorInputCommand::Escape, false)) {
                    [self setNeedsDisplay:YES];
                    return;
                }
            }

            if ((has_ctrl && has_shift) || (has_ctrl && has_alt) || (has_cmd && has_alt) || (has_alt && has_shift)) {
                if (character == NSUpArrowFunctionKey || [event keyCode] == 126) {
                    if (_renderer->handle_editor_input(
                            Zenvra::UI::Editor::EditorInputCommand::AddCursorAbove, false)) {
                        [self setNeedsDisplay:YES];
                    }
                    return;
                }
                if (character == NSDownArrowFunctionKey || [event keyCode] == 125) {
                    if (_renderer->handle_editor_input(
                            Zenvra::UI::Editor::EditorInputCommand::AddCursorBelow, false)) {
                        [self setNeedsDisplay:YES];
                    }
                    return;
                }
            }

            if (has_alt && !has_cmd && !has_ctrl && !has_shift) {
                if (character == NSUpArrowFunctionKey || [event keyCode] == 126) {
                    if (_renderer->handle_editor_input(
                            Zenvra::UI::Editor::EditorInputCommand::MoveLineUp, false)) {
                        [self setNeedsDisplay:YES];
                    }
                    return;
                }
                if (character == NSDownArrowFunctionKey || [event keyCode] == 125) {
                    if (_renderer->handle_editor_input(
                            Zenvra::UI::Editor::EditorInputCommand::MoveLineDown, false)) {
                        [self setNeedsDisplay:YES];
                    }
                    return;
                }
            }
        }
    }

    // Editor path: try to pass to NSTextInputClient for proper IME handling
    if (![[self inputContext] handleEvent:event]) {
        // If not handled by IME, it's a raw key (or modifier combo)
        // ... translate and send to terminal or editor ...
    }
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
// Minimal stubs to satisfy the compiler for now.
// Real implementation needed for proper text input (IME).

- (void)insertText:(id)string replacementRange:(NSRange)replacementRange {
    if (!_renderer) return;
    NSString *text = nil;
    if ([string isKindOfClass:[NSString class]]) {
        text = string;
    } else if ([string isKindOfClass:[NSAttributedString class]]) {
        text = [(NSAttributedString *)string string];
    }
    if (text) {
        (void)_renderer->handle_text_input([text UTF8String]);
        [self setNeedsDisplay:YES];
    }
}
- (void)doCommandBySelector:(SEL)selector {}
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
