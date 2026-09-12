#pragma once

#ifdef __OBJC__
#import <Cocoa/Cocoa.h>
#include "UI/Chrome/WindowChromeLayout.h"
#include "Platform/IPlatformWindow.h"

namespace Zenvra::Platform::Cocoa::Components {
    class CocoaChromeRenderer;
}

@interface ZenvraContentView : NSView <NSDraggingDestination, NSTextInputClient>

@property (nonatomic, assign) Zenvra::Platform::Cocoa::Components::CocoaChromeRenderer* renderer;

- (void)updateLayout:(const Zenvra::UI::Chrome::WindowChromeLayoutResult&)layout;
- (void)setCommandCallback:(Zenvra::Platform::CommandStateQueryCallback)callback;
- (void)setCommandInvokedCallback:(Zenvra::Platform::CommandInvokedCallback)callback;
- (void)checkToolbarMenuHoverAtPointX:(float)px pointY:(float)py currentMenuIndex:(std::size_t)currentMenuIndex menuToCancel:(NSMenu*)menuToCancel;

@end
#else
typedef void ZenvraContentView;
#endif
