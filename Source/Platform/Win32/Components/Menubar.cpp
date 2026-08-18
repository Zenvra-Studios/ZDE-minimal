#include "Platform/Win32/Components/Menubar.h"

#include "Commands/CommandIds.h"
#include "Platform/Win32/Config/resource.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

namespace Zenvra::Platform::Win32::Components
{

Menubar::~Menubar()
{
    if (m_menu_handle != nullptr && IsMenu(m_menu_handle) != FALSE)
    {
        DestroyMenu(m_menu_handle);
    }
}

bool Menubar::load(HINSTANCE instance_handle)
{
    m_menu_handle = LoadMenuW(instance_handle, MAKEINTRESOURCEW(IDR_MAINMENU));
    return m_menu_handle != nullptr;
}

bool Menubar::attach(HWND window_handle)
{
    m_window_handle = window_handle;
    return m_window_handle != nullptr && SetMenu(m_window_handle, m_menu_handle) != FALSE;
}

bool Menubar::detach()
{
    if (m_window_handle == nullptr)
    {
        return false;
    }

    const bool detached = SetMenu(m_window_handle, nullptr) != FALSE;
    DrawMenuBar(m_window_handle);
    return detached;
}

bool Menubar::show_popup(std::size_t menu_index, int screen_x, int screen_y) const
{
    if (m_menu_handle == nullptr || m_window_handle == nullptr)
    {
        return false;
    }

    HMENU popup_menu = GetSubMenu(m_menu_handle, static_cast<int>(menu_index));
    if (popup_menu == nullptr)
    {
        return false;
    }

    refresh_menu_state(popup_menu);

    const UINT selected_command = TrackPopupMenuEx(
        popup_menu,
        TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_LEFTBUTTON,
        screen_x,
        screen_y,
        m_window_handle,
        nullptr);
    if (selected_command != 0)
    {
        PostMessageW(m_window_handle, WM_COMMAND, MAKEWPARAM(selected_command, 0), 0);
    }
    return true;
}

bool Menubar::show_overflow_popup(
    std::size_t first_menu_index,
    int screen_x,
    int screen_y) const
{
    if (m_menu_handle == nullptr || m_window_handle == nullptr)
    {
        return false;
    }

    const int menu_count = GetMenuItemCount(m_menu_handle);
    if (first_menu_index >= static_cast<std::size_t>(menu_count))
    {
        return false;
    }

    HMENU overflow_menu = CreatePopupMenu();
    if (overflow_menu == nullptr)
    {
        return false;
    }

    bool populated = false;
    for (std::size_t menu_index = first_menu_index;
         menu_index < static_cast<std::size_t>(menu_count);
         ++menu_index)
    {
        HMENU source_popup = GetSubMenu(m_menu_handle, static_cast<int>(menu_index));
        if (source_popup == nullptr)
        {
            continue;
        }

        refresh_menu_state(source_popup);
        HMENU popup_copy = clone_menu(source_popup);
        if (popup_copy == nullptr)
        {
            continue;
        }

        const std::wstring label = get_menu_label(m_menu_handle, static_cast<int>(menu_index));
        if (AppendMenuW(
                overflow_menu,
                MF_POPUP | MF_STRING,
                reinterpret_cast<UINT_PTR>(popup_copy),
                label.c_str()) == FALSE)
        {
            DestroyMenu(popup_copy);
            continue;
        }
        populated = true;
    }

    if (!populated)
    {
        DestroyMenu(overflow_menu);
        return false;
    }

    const UINT selected_command = TrackPopupMenuEx(
        overflow_menu,
        TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_LEFTBUTTON,
        screen_x,
        screen_y,
        m_window_handle,
        nullptr);
    DestroyMenu(overflow_menu);
    if (selected_command != 0)
    {
        PostMessageW(m_window_handle, WM_COMMAND, MAKEWPARAM(selected_command, 0), 0);
    }
    return true;
}

bool Menubar::handle_command(int native_command_id) const
{
    const std::string_view command_id = get_command_id(native_command_id);
    if (command_id.empty())
    {
        return false;
    }

    if (m_command_invoked_callback)
    {
        m_command_invoked_callback(command_id);
    }
    return true;
}

void Menubar::set_command_invoked_callback(CommandInvokedCallback callback)
{
    m_command_invoked_callback = std::move(callback);
}

void Menubar::set_command_state_query_callback(CommandStateQueryCallback callback)
{
    m_command_state_query_callback = std::move(callback);
}

void Menubar::refresh_menu_state(HMENU menu_handle) const
{
    const int item_count = GetMenuItemCount(menu_handle);
    for (int item_index = 0; item_index < item_count; ++item_index)
    {
        HMENU child_menu = GetSubMenu(menu_handle, item_index);
        if (child_menu != nullptr)
        {
            refresh_menu_state(child_menu);
            continue;
        }

        const UINT native_command_id = GetMenuItemID(menu_handle, item_index);
        if (native_command_id == 0 || native_command_id == static_cast<UINT>(-1))
        {
            continue;
        }

        const std::string_view command_id = get_command_id(static_cast<int>(native_command_id));
        if (command_id.empty())
        {
            continue;
        }

        const CommandPresentationState state = m_command_state_query_callback
            ? m_command_state_query_callback(command_id)
            : CommandPresentationState{true, false};
        EnableMenuItem(
            menu_handle,
            native_command_id,
            MF_BYCOMMAND | (state.enabled ? MF_ENABLED : MF_GRAYED));
        CheckMenuItem(
            menu_handle,
            native_command_id,
            MF_BYCOMMAND | (state.checked ? MF_CHECKED : MF_UNCHECKED));
    }
}

HMENU Menubar::clone_menu(HMENU source_menu)
{
    HMENU menu_copy = CreatePopupMenu();
    if (menu_copy == nullptr)
    {
        return nullptr;
    }

    const int item_count = GetMenuItemCount(source_menu);
    for (int item_index = 0; item_index < item_count; ++item_index)
    {
        const UINT item_state = GetMenuState(source_menu, item_index, MF_BYPOSITION);
        if (item_state == static_cast<UINT>(-1))
        {
            continue;
        }
        if ((item_state & MF_SEPARATOR) != 0)
        {
            AppendMenuW(menu_copy, MF_SEPARATOR, 0, nullptr);
            continue;
        }

        const std::wstring label = get_menu_label(source_menu, item_index);
        const UINT state_flags = item_state &
            (MF_DISABLED | MF_GRAYED | MF_CHECKED | MF_DEFAULT | MF_MENUBREAK | MF_MENUBARBREAK);
        HMENU source_child = GetSubMenu(source_menu, item_index);
        if (source_child != nullptr)
        {
            HMENU child_copy = clone_menu(source_child);
            if (child_copy == nullptr || AppendMenuW(
                    menu_copy,
                    MF_POPUP | MF_STRING | state_flags,
                    reinterpret_cast<UINT_PTR>(child_copy),
                    label.c_str()) == FALSE)
            {
                if (child_copy != nullptr)
                {
                    DestroyMenu(child_copy);
                }
                DestroyMenu(menu_copy);
                return nullptr;
            }
            continue;
        }

        const UINT command_id = GetMenuItemID(source_menu, item_index);
        if (AppendMenuW(
                menu_copy,
                MF_STRING | state_flags,
                command_id,
                label.c_str()) == FALSE)
        {
            DestroyMenu(menu_copy);
            return nullptr;
        }
    }
    return menu_copy;
}

std::wstring Menubar::get_menu_label(HMENU menu_handle, int item_index)
{
    const int label_length = GetMenuStringW(menu_handle, item_index, nullptr, 0, MF_BYPOSITION);
    if (label_length <= 0)
    {
        return {};
    }

    std::wstring label(static_cast<std::size_t>(label_length) + 1, L'\0');
    const int copied_length = GetMenuStringW(
        menu_handle,
        item_index,
        label.data(),
        label_length + 1,
        MF_BYPOSITION);
    label.resize(static_cast<std::size_t>(std::max(copied_length, 0)));
    return label;
}

std::string_view Menubar::get_command_id(int native_command_id) noexcept
{
    std::string_view command_id;

    switch (native_command_id)
    {
    case ID_FILE_NEW:
        command_id = Commands::CommandIds::file_new;
        break;
    case ID_FILE_OPEN:
        command_id = Commands::CommandIds::file_open;
        break;
    case ID_FILE_SAVE:
        command_id = Commands::CommandIds::file_save;
        break;
    case ID_FILE_CLOSE:
        command_id = Commands::CommandIds::file_close;
        break;
    case ID_FILE_DELETE:
        command_id = Commands::CommandIds::file_delete;
        break;
    case ID_FILE_EXIT:
        command_id = Commands::CommandIds::file_exit;
        break;
    case ID_EDIT_UNDO:
        command_id = Commands::CommandIds::edit_undo;
        break;
    case ID_EDIT_REDO:
        command_id = Commands::CommandIds::edit_redo;
        break;
    case ID_EDIT_CUT:
        command_id = Commands::CommandIds::edit_cut;
        break;
    case ID_EDIT_COPY:
        command_id = Commands::CommandIds::edit_copy;
        break;
    case ID_EDIT_PASTE:
        command_id = Commands::CommandIds::edit_paste;
        break;
    case ID_SELECTION_SELECT_ALL:
        command_id = Commands::CommandIds::selection_select_all;
        break;
    case ID_SELECTION_MOVE_LINE_UP:
        command_id = Commands::CommandIds::selection_move_line_up;
        break;
    case ID_SELECTION_MOVE_LINE_DOWN:
        command_id = Commands::CommandIds::selection_move_line_down;
        break;
    case ID_SELECTION_ADD_CURSOR_ABOVE:
        command_id = Commands::CommandIds::selection_add_cursor_above;
        break;
    case ID_SELECTION_ADD_CURSOR_BELOW:
        command_id = Commands::CommandIds::selection_add_cursor_below;
        break;
    case ID_VIEW_EXPLORER:
        command_id = Commands::CommandIds::view_explorer;
        break;
    case ID_VIEW_SEARCH:
        command_id = Commands::CommandIds::view_search;
        break;
    case ID_VIEW_OUTPUT:
        command_id = Commands::CommandIds::view_output;
        break;
    case ID_VIEW_PROBLEMS:
        command_id = Commands::CommandIds::view_problems;
        break;
    case ID_WINDOW_CLOSE:
        command_id = Commands::CommandIds::window_close;
        break;
    case ID_WINDOW_RESET_LAYOUT:
        command_id = Commands::CommandIds::window_reset_layout;
        break;
    case ID_WINDOW_TOGGLE_FULLSCREEN:
        command_id = Commands::CommandIds::window_toggle_fullscreen;
        break;
    case ID_WINDOW_MINIMIZE:
        command_id = Commands::CommandIds::window_minimize;
        break;
    case ID_WINDOW_MAXIMIZE:
        command_id = Commands::CommandIds::window_maximize;
        break;
    case ID_WINDOW_SPLIT_RIGHT:
        command_id = Commands::CommandIds::view_split_right;
        break;
    case ID_WINDOW_SPLIT_LEFT:
        command_id = Commands::CommandIds::view_split_left;
        break;
    case ID_WINDOW_SPLIT_UP:
        command_id = Commands::CommandIds::view_split_up;
        break;
    case ID_WINDOW_SPLIT_DOWN:
        command_id = Commands::CommandIds::view_split_down;
        break;
    case ID_WINDOW_NEXT_TAB:
        command_id = Commands::CommandIds::window_next_tab;
        break;
    case ID_WINDOW_PREV_TAB:
        command_id = Commands::CommandIds::window_prev_tab;
        break;
    case ID_FILE_CLOSE_ALL:
        command_id = Commands::CommandIds::file_close_all;
        break;
    case ID_PROJECT_OPEN:
        command_id = Commands::CommandIds::project_open;
        break;
    case ID_PROJECT_CLOSE:
        command_id = Commands::CommandIds::project_close;
        break;
    case ID_BUILD_PROJECT:
        command_id = Commands::CommandIds::build_build_project;
        break;
    case ID_RUN_START:
        command_id = Commands::CommandIds::run_start;
        break;
    case ID_HELP_ABOUT:
        command_id = Commands::CommandIds::help_about;
        break;
    default:
        return {};
    }

    return command_id;
}

} // namespace Zenvra::Platform::Win32::Components
