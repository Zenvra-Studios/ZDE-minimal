#import "Platform/Cocoa/Components/ZenvraContentView.h"

#include "Platform/Cocoa/Components/CocoaChromeRenderer.h"
#include "Platform/Cocoa/Components/FileDropTarget.h"

#import <Cocoa/Cocoa.h>

using namespace Zenvra::Platform::Cocoa::Components;

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
    return YES; // Top-left origin
}

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (void)drawRect:(NSRect)dirtyRect {
    if (!_renderer) return;

    CGContextRef context = [[NSGraphicsContext currentContext] CGContext];
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

// --- Mouse Events ---

- (void)mouseDown:(NSEvent *)event {
    if (!_renderer) return;
    
    NSPoint location = [self convertPoint:[event locationInWindow] fromView:nil];
    float px = location.x;
    float py = location.y;
    int cw = [self bounds].size.width;
    int ch = [self bounds].size.height;
    
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
    
    if (_renderer->handle_workspace_pointer_move(px, py, cw, ch, _chrome_layout.titlebar_bounds.bottom())) {
        needs_display = true;
    }
    
    if (needs_display) {
        [self setNeedsDisplay:YES];
    }
}

- (void)mouseExited:(NSEvent *)event {
    if (!_renderer) return;
    
    if (_renderer->update_chrome_hover_state(-1.0F, -1.0F, _chrome_layout, _interaction_state)) {
        [self setNeedsDisplay:YES];
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
    
    // Reset scroll accumulators when a new gesture begins or the previous one ends.
    // This prevents stale remainder from one gesture bleeding into the next.
    if ([event phase] == NSEventPhaseBegan || [event phase] == NSEventPhaseCancelled) {
        _scroll_remainder_y = 0.0f;
        _scroll_remainder_x = 0.0f;
    }
    
    // Convert scrolling delta
    float raw_dy = [event scrollingDeltaY];
    float raw_dx = [event scrollingDeltaX];
    
    if ([event hasPreciseScrollingDeltas]) {
        // Trackpad/Magic Mouse: deltas are in pixels.
        // Convert to line equivalents using a responsive divisor.
        constexpr float pixels_per_line = 14.0f;
        _scroll_remainder_y += raw_dy / pixels_per_line;
        _scroll_remainder_x += raw_dx / pixels_per_line;
        
        // Use directional rounding: extract whole lines in the direction of scrolling
        // so that even small, slow movements eventually fire a line scroll.
        float whole_y = (_scroll_remainder_y >= 0.0f)
            ? std::floor(_scroll_remainder_y)
            : std::ceil(_scroll_remainder_y);
        float whole_x = (_scroll_remainder_x >= 0.0f)
            ? std::floor(_scroll_remainder_x)
            : std::ceil(_scroll_remainder_x);
        
        _scroll_remainder_y -= whole_y;
        _scroll_remainder_x -= whole_x;
        
        raw_dy = whole_y;
        raw_dx = whole_x;
    } else {
        // Mouse wheel: scrollingDeltaY is typically ±1 per notch.
        // Multiply by 3 to match Win32/X11 behaviour (3 lines per tick).
        raw_dy *= 3.0f;
        raw_dx *= 3.0f;
    }
    
    // macOS scrollingDeltaY is positive for a swipe UP (with Natural Scrolling).
    // Win32/X11 use negative deltas for scrolling UP and positive for DOWN
    // (see Win32Window.cpp WM_MOUSEWHEEL and X11Window Button4/5), so negate
    // here to keep the wheel direction identical across platforms.
    std::ptrdiff_t delta_y = -static_cast<std::ptrdiff_t>(raw_dy);
    std::ptrdiff_t delta_x = -static_cast<std::ptrdiff_t>(raw_dx);
    
    std::string command;
    bool needs_update = false;
    
    if (delta_y != 0) {
        bool scrolled = _renderer->handle_workspace_scroll(
            px, py, command, delta_y, false, cw, ch, _chrome_layout.titlebar_bounds.bottom());
        needs_update |= scrolled;
        // If the scroll was rejected (at boundary), drain the remainder in that
        // direction so it cannot accumulate and cause a flicker/jump when the
        // user reverses.
        if (!scrolled && [event hasPreciseScrollingDeltas]) {
            _scroll_remainder_y = 0.0f;
        }
    }
    if (delta_x != 0) {
        bool scrolled = _renderer->handle_workspace_scroll(
            px, py, command, delta_x, true, cw, ch, _chrome_layout.titlebar_bounds.bottom());
        needs_update |= scrolled;
        if (!scrolled && [event hasPreciseScrollingDeltas]) {
            _scroll_remainder_x = 0.0f;
        }
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
                _renderer->open_dropped_paths(paths);
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
        _renderer->handle_text_input([text UTF8String]);
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
