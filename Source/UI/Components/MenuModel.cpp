#include "UI/Components/MenuModel.h"
#include "Commands/CommandIds.h"
#include <array>

namespace Zenvra::UI::Components
{

namespace
{

constexpr MenuItem separator{{}, {}, true};

constexpr std::array file_items{
    MenuItem{"New", Commands::CommandIds::file_new, false, "Ctrl+N"},
    MenuItem{"New Window", Commands::CommandIds::window_new, false, "Ctrl+Shift+N"},
    separator,
    MenuItem{"Open File...", Commands::CommandIds::file_open, false, "Ctrl+O"},
    MenuItem{"Open Folder...", Commands::CommandIds::folder_open, false, "Ctrl+K Ctrl+O"},
    MenuItem{"Open Recent...", Commands::CommandIds::file_open_recent, false, "Ctrl+R"},
    MenuItem{"Open Remote...", Commands::CommandIds::file_open_remote, false, "Ctrl+Alt+O"},
    separator,
    MenuItem{"Add Folder to Project...", Commands::CommandIds::project_add_folder, false, ""},
    separator,
    MenuItem{"Save", Commands::CommandIds::file_save, false, "Ctrl+S"},
    MenuItem{"Save As...", Commands::CommandIds::file_save_as, false, "Ctrl+Shift+S"},
    MenuItem{"Save All", Commands::CommandIds::file_save_all, false, "Ctrl+K S"},
    separator,
    MenuItem{"Close Editor", Commands::CommandIds::file_close, false, "Ctrl+W"},
    MenuItem{"Close Project", Commands::CommandIds::project_close, false, "Ctrl+K F"},
    MenuItem{"Close Window", Commands::CommandIds::window_close, false, "Ctrl+Shift+W"},
};

constexpr std::array edit_items{
    MenuItem{"Undo", Commands::CommandIds::edit_undo, false, "Ctrl+Z"},
    MenuItem{"Redo", Commands::CommandIds::edit_redo, false, "Ctrl+Y"},
    separator,
    MenuItem{"Cut", Commands::CommandIds::edit_cut, false, "Ctrl+X"},
    MenuItem{"Copy", Commands::CommandIds::edit_copy, false, "Ctrl+C"},
    MenuItem{"Copy and Trim", Commands::CommandIds::edit_copy_trim, false, "Ctrl+Shift+C"},
    MenuItem{"Paste", Commands::CommandIds::edit_paste, false, "Ctrl+V"},
    separator,
    MenuItem{"Find", Commands::CommandIds::edit_find, false, "Ctrl+F"},
    MenuItem{"Find in Project", Commands::CommandIds::edit_find_in_project, false, "Ctrl+Shift+F"},
    separator,
    MenuItem{"Toggle Line Comment", Commands::CommandIds::edit_toggle_comment, false, "Ctrl+/"},
};

constexpr std::array selection_items{
    MenuItem{"Select All", Commands::CommandIds::selection_select_all, false, "Ctrl+A"},
    MenuItem{"Expand Selection", Commands::CommandIds::selection_expand, false, "Shift+Alt+Right"},
    MenuItem{"Shrink Selection", Commands::CommandIds::selection_shrink, false, "Shift+Alt+Left"},
    separator,
    MenuItem{"Copy Line Up", Commands::CommandIds::selection_copy_line_up, false, "Shift+Alt+Up"},
    MenuItem{"Copy Line Down", Commands::CommandIds::selection_copy_line_down, false, "Shift+Alt+Down"},
    MenuItem{"Move Line Up", Commands::CommandIds::selection_move_line_up, false, "Alt+Up"},
    MenuItem{"Move Line Down", Commands::CommandIds::selection_move_line_down, false, "Alt+Down"},
    MenuItem{"Duplicate Selection", Commands::CommandIds::selection_duplicate, false, "Ctrl+D"},
    separator,
    MenuItem{"Add Cursor Above", Commands::CommandIds::selection_add_cursor_above, false, "Ctrl+Alt+Up"},
    MenuItem{"Add Cursor Below", Commands::CommandIds::selection_add_cursor_below, false, "Ctrl+Alt+Down"},
    MenuItem{"Add Cursors to Line Ends", Commands::CommandIds::selection_add_cursors_to_line_ends, false, "Shift+Alt+I"},
    MenuItem{"Add Next Occurrence", Commands::CommandIds::selection_add_next_occurrence, false, "Ctrl+D"},
    MenuItem{"Add Previous Occurrence", Commands::CommandIds::selection_add_previous_occurrence, false, "Ctrl+K Ctrl+D"},
    MenuItem{"Select All Occurrences", Commands::CommandIds::selection_select_all_occurrences, false, "Ctrl+Shift+L"},
    separator,
    MenuItem{"Switch to Ctrl+Click for Multi-Cursor", Commands::CommandIds::selection_switch_multi_cursor_modifier, false, ""},
    MenuItem{"Column Selection Mode", Commands::CommandIds::selection_column_selection_mode, false, ""},
};

constexpr std::array view_items{
    MenuItem{"Zoom In", Commands::CommandIds::view_zoom_in, false, "Ctrl+="},
    MenuItem{"Zoom Out", Commands::CommandIds::view_zoom_out, false, "Ctrl+-"},
    MenuItem{"Reset Zoom", Commands::CommandIds::view_reset_zoom, false, "Ctrl+Numpad0"},
    MenuItem{"Reset All Zoom", Commands::CommandIds::view_reset_all_zoom, false, "Ctrl+0"},
    separator,
    MenuItem{"Toggle Left Dock", Commands::CommandIds::view_toggle_left_dock, false, "Ctrl+B"},
    MenuItem{"Toggle Right Dock", Commands::CommandIds::view_toggle_right_dock, false, "Ctrl+Alt+B"},
    MenuItem{"Toggle Bottom Dock", Commands::CommandIds::view_toggle_bottom_dock, false, "Ctrl+J"},
    MenuItem{"Toggle All Docks", Commands::CommandIds::view_toggle_all_docks, false, "Ctrl+Alt+Y"},
    MenuItem{"Split Up", Commands::CommandIds::view_split_up, false, "Ctrl+K Up"},
    MenuItem{"Split Down", Commands::CommandIds::view_split_down, false, "Ctrl+K Down"},
    MenuItem{"Split Left", Commands::CommandIds::view_split_left, false, "Ctrl+K Left"},
    MenuItem{"Split Right", Commands::CommandIds::view_split_right, false, "Ctrl+\\"},
    separator,
    MenuItem{"Project Panel", Commands::CommandIds::view_project_panel, false, "Ctrl+Shift+E"},
    MenuItem{"Outline Panel", Commands::CommandIds::view_outline_panel, false, "Ctrl+Shift+O"},
    MenuItem{"Collab Panel", Commands::CommandIds::view_collab_panel, false, "Ctrl+Shift+C"},
    MenuItem{"Terminal Panel", Commands::CommandIds::view_terminal_panel, false, "Ctrl+`"},
    MenuItem{"Debugger Panel", Commands::CommandIds::view_debugger_panel, false, "Ctrl+Shift+D"},
    MenuItem{"Agent Panel", Commands::CommandIds::view_agent_panel, false, "Ctrl+?"},
    MenuItem{"Git Panel", Commands::CommandIds::view_git_panel, false, "Ctrl+Shift+G"},
    separator,
    MenuItem{"Diagnostics", Commands::CommandIds::view_diagnostics, false, "Ctrl+Shift+M"},
};

constexpr std::array navigate_items{
    MenuItem{"Navigation commands are not available yet", {}},
};

constexpr std::array project_items{
    MenuItem{"Open Project...", Commands::CommandIds::project_open, false, "Ctrl+K Ctrl+O"},
    MenuItem{"Close Project", Commands::CommandIds::project_close, false, "Ctrl+K F"},
};

constexpr std::array build_items{
    MenuItem{"Build Project", Commands::CommandIds::build_build_project, false, "Ctrl+Shift+B"},
};

constexpr std::array run_items{
    MenuItem{"Start", Commands::CommandIds::run_start, false, "F5"},
};

constexpr std::array window_items{
    MenuItem{"Reset Layout", Commands::CommandIds::window_reset_layout, false, ""},
    MenuItem{"Toggle Fullscreen", Commands::CommandIds::window_toggle_fullscreen, false, "F11"},
};

constexpr std::array help_items{
    MenuItem{"Welcome", Commands::CommandIds::help_welcome, false, ""},
    MenuItem{"Show All Commands", Commands::CommandIds::help_show_all_commands, false, "Ctrl+Shift+P"},
    MenuItem{"Editor Playground", Commands::CommandIds::help_editor_playground, false, ""},
    MenuItem{"Open Walkthrough...", Commands::CommandIds::help_open_walkthrough, false, ""},
    MenuItem{"Provide Feedback", Commands::CommandIds::help_provide_feedback, false, ""},
    MenuItem{"Download Diagnostics", Commands::CommandIds::help_download_diagnostics, false, ""},
    separator,
    MenuItem{"View License", Commands::CommandIds::help_view_license, false, ""},
    separator,
    MenuItem{"Toggle Developer Tools", Commands::CommandIds::help_toggle_developer_tools, false, "Ctrl+Shift+I"},
    MenuItem{"Open Process Explorer", Commands::CommandIds::help_open_process_explorer, false, "Shift+Esc"},
    separator,
    MenuItem{"Check for Updates...", Commands::CommandIds::help_check_for_updates, false, ""},
    separator,
    MenuItem{"About", Commands::CommandIds::help_about, false, ""},
};

constexpr std::array compiler_items{
    MenuItem{"Debug Profile", Commands::CommandIds::build_debug},
    MenuItem{"Release Profile", Commands::CommandIds::build_release},
    separator,
    MenuItem{"Configuration Manager...", Commands::CommandIds::edit_profiles},
};

constexpr std::array platform_items{
    MenuItem{"x64", Commands::CommandIds::platform_x64},
    MenuItem{"x86", Commands::CommandIds::platform_x86},
    MenuItem{"Win32", Commands::CommandIds::platform_win32},
    MenuItem{"ARM64", Commands::CommandIds::platform_arm64},
    MenuItem{"AArch64", Commands::CommandIds::platform_aarch64},
    MenuItem{"Apple ARM", Commands::CommandIds::platform_apple_arm},
};

constexpr std::array binary_items{
    MenuItem{"ZDE", Commands::CommandIds::run_zde},
    MenuItem{"Tests", Commands::CommandIds::run_tests},
};

constexpr std::array gear_items{
    MenuItem{"Settings...", Commands::CommandIds::open_settings, false, "Ctrl+,"},
    MenuItem{"Themes...", Commands::CommandIds::open_themes, false, "Ctrl+K Ctrl+T"},
    separator,
    MenuItem{"Plugins...", Commands::CommandIds::open_plugins, false, "Ctrl+Shift+X"},
};

constexpr std::array ellipsis_items{
    MenuItem{"Search...", Commands::CommandIds::view_search, false, "Ctrl+Shift+F"},
    MenuItem{"Terminal", Commands::CommandIds::view_output, false, "Ctrl+`"},
    separator,
    MenuItem{"More Tools...", Commands::CommandIds::more_tools, false, ""},
};

constexpr std::array menus{
    Menu{"File", file_items},
    Menu{"Edit", edit_items},
    Menu{"Selection", selection_items},
    Menu{"View", view_items},
    Menu{"Navigate", navigate_items},
    Menu{"Project", project_items},
    Menu{"Build", build_items},
    Menu{"Run", run_items},
    Menu{"Window", window_items},
    Menu{"Help", help_items},
    // Overlay menus
    Menu{"Compiler", compiler_items},
    Menu{"Platform", platform_items},
    Menu{"Binary", binary_items},
    Menu{"Gear", gear_items},
    Menu{"Ellipsis", ellipsis_items},
};

} // namespace

std::span<const Menu> get_window_menus() noexcept
{
    return menus;
}

std::span<const MenuItem> get_compiler_menu() noexcept
{
    return compiler_items;
}

std::span<const MenuItem> get_platform_menu() noexcept
{
    return platform_items;
}

std::span<const MenuItem> get_binary_menu() noexcept
{
    return binary_items;
}

std::span<const MenuItem> get_gear_menu() noexcept
{
    return gear_items;
}

std::span<const MenuItem> get_ellipsis_menu() noexcept
{
    return ellipsis_items;
}

} // namespace Zenvra::UI::Components
