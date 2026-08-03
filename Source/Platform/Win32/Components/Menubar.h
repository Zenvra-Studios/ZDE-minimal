#pragma once

#include "Platform/IPlatformWindow.h"

#include <windows.h>

#include <cstddef>
#include <string_view>

namespace Zenvra::Platform::Win32::Components
{

class Menubar
{
public:
    Menubar() = default;
    ~Menubar();

    Menubar(const Menubar&) = delete;
    Menubar& operator=(const Menubar&) = delete;

    [[nodiscard]] bool load(HINSTANCE instance_handle);
    [[nodiscard]] bool attach(HWND window_handle);
    [[nodiscard]] bool detach();
    [[nodiscard]] bool show_popup(std::size_t menu_index, int screen_x, int screen_y) const;
    [[nodiscard]] bool show_overflow_popup(
        std::size_t first_menu_index,
        int screen_x,
        int screen_y) const;
    [[nodiscard]] bool handle_command(int native_command_id) const;
    void set_command_invoked_callback(CommandInvokedCallback callback);
    void set_command_state_query_callback(CommandStateQueryCallback callback);

private:
    void refresh_menu_state(HMENU menu_handle) const;
    [[nodiscard]] static HMENU clone_menu(HMENU source_menu);
    [[nodiscard]] static std::wstring get_menu_label(HMENU menu_handle, int item_index);
    [[nodiscard]] static std::string_view get_command_id(int native_command_id) noexcept;

    HMENU m_menu_handle = nullptr;
    HWND m_window_handle = nullptr;
    CommandInvokedCallback m_command_invoked_callback;
    CommandStateQueryCallback m_command_state_query_callback;
};

} // namespace Zenvra::Platform::Win32::Components
