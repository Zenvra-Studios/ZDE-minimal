#pragma once

#include "Platform/IPlatformWindow.h"

namespace Zenvra::Platform::Cocoa::Runtime
{
class CocoaMenuBridge
{
public:
    static void build_native_menu_bar();
    static void set_command_callback(CommandInvokedCallback callback);
    static void set_command_state_query_callback(CommandStateQueryCallback callback);
};
} // namespace Zenvra::Platform::Cocoa::Runtime
