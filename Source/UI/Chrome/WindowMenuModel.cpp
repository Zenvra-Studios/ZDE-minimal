#include "UI/Chrome/WindowMenuModel.h"

#include "Commands/CommandIds.h"

#include <array>

namespace Zenvra::UI::Chrome
{

namespace
{

constexpr WindowMenuItem separator{{}, {}, true};

constexpr std::array file_items{
    WindowMenuItem{"New", Commands::CommandIds::file_new, false, "Ctrl+N"},
    WindowMenuItem{"New Window", Commands::CommandIds::window_new, false, "Ctrl+Shift+N"},
    separator,
    WindowMenuItem{"Open File...", Commands::CommandIds::file_open, false, "Ctrl+O"},
    WindowMenuItem{"Open Folder...", Commands::CommandIds::folder_open, false, "Ctrl+K Ctrl+O"},
    WindowMenuItem{"Open Recent...", Commands::CommandIds::file_open_recent, false, "Ctrl+R"},
    WindowMenuItem{"Open Remote...", Commands::CommandIds::file_open_remote, false, "Ctrl+Alt+O"},
    separator,
    WindowMenuItem{"Add Folder to Project...", Commands::CommandIds::project_add_folder, false, ""},
    separator,
    WindowMenuItem{"Save", Commands::CommandIds::file_save, false, "Ctrl+S"},
    WindowMenuItem{"Save As...", Commands::CommandIds::file_save_as, false, "Ctrl+Shift+S"},
    WindowMenuItem{"Save All", Commands::CommandIds::file_save_all, false, "Ctrl+K S"},
    separator,
    WindowMenuItem{"Close Editor", Commands::CommandIds::file_close, false, "Ctrl+W"},
    WindowMenuItem{"Close Project", Commands::CommandIds::project_close, false, "Ctrl+K F"},
    WindowMenuItem{"Close Window", Commands::CommandIds::window_close, false, "Ctrl+Shift+W"},
    separator,
    WindowMenuItem{"Exit", Commands::CommandIds::file_exit, false, "Alt+F4"},
};

constexpr std::array edit_items{
    WindowMenuItem{"Undo", Commands::CommandIds::edit_undo, false, "Ctrl+Z"},
    WindowMenuItem{"Redo", Commands::CommandIds::edit_redo, false, "Ctrl+Y"},
    separator,
    WindowMenuItem{"Cut", Commands::CommandIds::edit_cut, false, "Ctrl+X"},
    WindowMenuItem{"Copy", Commands::CommandIds::edit_copy, false, "Ctrl+C"},
    WindowMenuItem{"Paste", Commands::CommandIds::edit_paste, false, "Ctrl+V"},
};

constexpr std::array selection_items{
    WindowMenuItem{"Select All", Commands::CommandIds::selection_select_all, false, "Ctrl+A"},
    separator,
    WindowMenuItem{"Move Line Up", Commands::CommandIds::selection_move_line_up, false, "Alt+Up"},
    WindowMenuItem{"Move Line Down", Commands::CommandIds::selection_move_line_down, false, "Alt+Down"},
    separator,
    WindowMenuItem{"Add Cursor Above", Commands::CommandIds::selection_add_cursor_above, false, "Ctrl+Alt+Up"},
    WindowMenuItem{"Add Cursor Below", Commands::CommandIds::selection_add_cursor_below, false, "Ctrl+Alt+Down"},
};

constexpr std::array view_items{
    WindowMenuItem{"Explorer", Commands::CommandIds::view_explorer, false, "Ctrl+Shift+E"},
    WindowMenuItem{"Search", Commands::CommandIds::view_search, false, "Ctrl+Shift+F"},
    WindowMenuItem{"Output", Commands::CommandIds::view_output, false, "Ctrl+`"},
    WindowMenuItem{"Problems", Commands::CommandIds::view_problems, false, "Ctrl+Shift+M"},
};

constexpr std::array navigate_items{
    WindowMenuItem{"Navigation commands are not available yet", {}},
};

constexpr std::array project_items{
    WindowMenuItem{"Open Project...", Commands::CommandIds::project_open, false, "Ctrl+K Ctrl+O"},
    WindowMenuItem{"Close Project", Commands::CommandIds::project_close, false, "Ctrl+K F"},
};

constexpr std::array build_items{
    WindowMenuItem{"Build Project", Commands::CommandIds::build_build_project, false, "Ctrl+Shift+B"},
};

constexpr std::array run_items{
    WindowMenuItem{"Start", Commands::CommandIds::run_start, false, "F5"},
};

constexpr std::array window_items{
    WindowMenuItem{"Close Window", Commands::CommandIds::window_close, false, "Ctrl+Shift+W"},
    separator,
    WindowMenuItem{"Split Editor Right", Commands::CommandIds::view_split_right, false, "Ctrl+\\"},
    WindowMenuItem{"Split Editor Left", Commands::CommandIds::view_split_left, false, "Ctrl+K Left"},
    WindowMenuItem{"Split Editor Up", Commands::CommandIds::view_split_up, false, "Ctrl+K Up"},
    WindowMenuItem{"Split Editor Down", Commands::CommandIds::view_split_down, false, "Ctrl+K Down"},
    separator,
    WindowMenuItem{"Toggle Fullscreen", Commands::CommandIds::window_toggle_fullscreen, false, "F11"},
    WindowMenuItem{"Minimize", Commands::CommandIds::window_minimize, false, ""},
    WindowMenuItem{"Maximize / Restore", Commands::CommandIds::window_maximize, false, ""},
    separator,
    WindowMenuItem{"Reset Layout", Commands::CommandIds::window_reset_layout, false, ""},
    separator,
    WindowMenuItem{"Next Editor Tab", Commands::CommandIds::window_next_tab, false, "Ctrl+Tab"},
    WindowMenuItem{"Previous Editor Tab", Commands::CommandIds::window_prev_tab, false, "Ctrl+Shift+Tab"},
    WindowMenuItem{"Close All Editors", Commands::CommandIds::file_close_all, false, "Ctrl+K W"},
};

constexpr std::array help_items{
    WindowMenuItem{"About ZDE", Commands::CommandIds::help_about, false, ""},
};

constexpr std::array menus{
    WindowMenu{"File", file_items},
    WindowMenu{"Edit", edit_items},
    WindowMenu{"Selection", selection_items},
    WindowMenu{"View", view_items},
    WindowMenu{"Navigate", navigate_items},
    WindowMenu{"Project", project_items},
    WindowMenu{"Build", build_items},
    WindowMenu{"Run", run_items},
    WindowMenu{"Window", window_items},
    WindowMenu{"Help", help_items},
};

} // namespace

std::span<const WindowMenu> get_window_menu_model() noexcept
{
    return menus;
}

} // namespace Zenvra::UI::Chrome
