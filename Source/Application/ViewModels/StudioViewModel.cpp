#include "Application/ViewModels/StudioViewModel.h"

#include "Commands/CommandIds.h"

#include <iostream>
#include <string>
#include <utility>

namespace Zenvra::Application::ViewModels
{

namespace
{

Commands::Command create_unavailable_command(
    std::string_view command_id,
    std::string name,
    std::string description,
    std::string category,
    Commands::Shortcut shortcut = {})
{
    return Commands::Command{
        .id = std::string(command_id),
        .name = std::move(name),
        .description = std::move(description),
        .category = std::move(category),
        .shortcut_binding = shortcut,
        .execute = [id = std::string(command_id)] {
            std::clog << "[ZDE] " << id << " executed\n";
        },
        .is_enabled = [] { return true; },
        .is_checked = {},
    };
}

} // namespace

StudioViewModel::StudioViewModel(StudioActions actions)
    : m_actions(std::move(actions))
{
}

bool StudioViewModel::initialize()
{
    if (m_initialized)
    {
        return true;
    }

    m_initialized = register_available_commands() && register_future_commands();
    if (!m_initialized)
    {
        m_command_registry = Commands::CommandRegistry{};
    }
    return m_initialized;
}

Commands::CommandExecutionResult StudioViewModel::execute_command(std::string_view command_id) const
{
    return m_command_registry.execute_command(command_id);
}

const Commands::CommandRegistry& StudioViewModel::get_command_registry() const noexcept
{
    return m_command_registry;
}

bool StudioViewModel::register_available_commands()
{
    bool registered = true;

    registered = m_command_registry.register_command(Commands::Command{
                     .id = std::string(Commands::CommandIds::file_exit),
                     .name = "Exit",
                     .description = "Close Zenvra Development Studio.",
                     .category = "File",
                     .shortcut_binding = {},
                     .execute = m_actions.request_close,
                     .is_enabled = [this] { return static_cast<bool>(m_actions.request_close); },
                     .is_checked = {},
                 }) &&
        registered;

    registered = m_command_registry.register_command(Commands::Command{
                     .id = std::string(Commands::CommandIds::help_about),
                     .name = "About ZDE",
                     .description = "Show product and version information.",
                     .category = "Help",
                     .shortcut_binding = {},
                     .execute = m_actions.show_about,
                     .is_enabled = [this] { return static_cast<bool>(m_actions.show_about); },
                     .is_checked = {},
                 }) &&
        registered;

    registered = m_command_registry.register_command(Commands::Command{
                     .id = std::string(Commands::CommandIds::project_open),
                     .name = "Open Project",
                     .description = "Open a folder as the ZDE workspace.",
                     .category = "Project",
                     .shortcut_binding = {},
                     .execute = m_actions.request_open_project,
                     .is_enabled = [this] {
                         return static_cast<bool>(m_actions.request_open_project);
                     },
                     .is_checked = {},
                 }) &&
        registered;

    registered = m_command_registry.register_command(Commands::Command{
                     .id = std::string(Commands::CommandIds::project_close),
                     .name = "Close Project",
                     .description = "Close the active ZDE project and workspace.",
                     .category = "Project",
                     .shortcut_binding = {},
                     .execute = m_actions.request_close_project,
                     .is_enabled = [this] {
                         return static_cast<bool>(m_actions.request_close_project);
                     },
                     .is_checked = {},
                 }) &&
        registered;

    registered = m_command_registry.register_command(Commands::Command{
                     .id = std::string(Commands::CommandIds::build_build_project),
                     .name = "Build Project",
                     .description = "Build the active project with CMake.",
                     .category = "Build",
                     .shortcut_binding = {},
                     .execute = m_actions.request_build,
                     .is_enabled = [this] { return static_cast<bool>(m_actions.request_build); },
                     .is_checked = {},
                 }) &&
        registered;

    registered = m_command_registry.register_command(Commands::Command{
                     .id = std::string(Commands::CommandIds::run_start),
                     .name = "Start",
                     .description = "Run the active target executable.",
                     .category = "Run",
                     .shortcut_binding = {},
                     .execute = m_actions.request_run,
                     .is_enabled = [this] { return static_cast<bool>(m_actions.request_run); },
                     .is_checked = {},
                 }) &&
        registered;

    // Active Build Profiles
    registered = m_command_registry.register_command(Commands::Command{
                     .id = std::string(Commands::CommandIds::build_debug),
                     .name = "Debug Profile",
                     .description = "Select Debug build profile.",
                     .category = "Build",
                     .shortcut_binding = {},
                     .execute = [this] { m_active_mode = "Debug"; m_active_preset = "macos-debug"; },
                     .is_enabled = [] { return true; },
                     .is_checked = [this] { return m_active_mode == "Debug"; },
                 }) &&
        registered;

    registered = m_command_registry.register_command(Commands::Command{
                     .id = std::string(Commands::CommandIds::build_release),
                     .name = "Release Profile",
                     .description = "Select Release build profile.",
                     .category = "Build",
                     .shortcut_binding = {},
                     .execute = [this] { m_active_mode = "Release"; m_active_preset = "macos-release"; },
                     .is_enabled = [] { return true; },
                     .is_checked = [this] { return m_active_mode == "Release"; },
                 }) &&
        registered;

    // Active Platforms / Architectures
    registered = m_command_registry.register_command(Commands::Command{
                     .id = std::string(Commands::CommandIds::platform_x64),
                     .name = "x86_64",
                     .description = "Target x86_64 (64-bit) architecture.",
                     .category = "Architecture",
                     .shortcut_binding = {},
                     .execute = [this] { m_active_arch = "x86_64"; },
                     .is_enabled = [] { return true; },
                     .is_checked = [this] { return m_active_arch == "x86_64" || m_active_arch == "x64"; },
                 }) &&
        registered;

    registered = m_command_registry.register_command(Commands::Command{
                     .id = std::string(Commands::CommandIds::platform_x86),
                     .name = "x86",
                     .description = "Target x86 (32-bit) architecture.",
                     .category = "Architecture",
                     .shortcut_binding = {},
                     .execute = [this] { m_active_arch = "x86"; },
                     .is_enabled = [] { return true; },
                     .is_checked = [this] { return m_active_arch == "x86"; },
                 }) &&
        registered;

    registered = m_command_registry.register_command(Commands::Command{
                     .id = std::string(Commands::CommandIds::platform_arm64),
                     .name = "ARM64",
                     .description = "Target ARM64 (AArch64) architecture.",
                     .category = "Architecture",
                     .shortcut_binding = {},
                     .execute = [this] { m_active_arch = "arm64"; },
                     .is_enabled = [] { return true; },
                     .is_checked = [this] { return m_active_arch == "arm64"; },
                 }) &&
        registered;

    registered = m_command_registry.register_command(Commands::Command{
                     .id = std::string(Commands::CommandIds::platform_arm32),
                     .name = "ARM32",
                     .description = "Target ARM32 architecture.",
                     .category = "Architecture",
                     .shortcut_binding = {},
                     .execute = [this] { m_active_arch = "arm32"; },
                     .is_enabled = [] { return true; },
                     .is_checked = [this] { return m_active_arch == "arm32"; },
                 }) &&
        registered;

    registered = m_command_registry.register_command(Commands::Command{
                     .id = std::string(Commands::CommandIds::platform_apple_arm),
                     .name = "Apple ARM",
                     .description = "Target Apple ARM platform.",
                     .category = "Architecture",
                     .shortcut_binding = {},
                     .execute = [this] { m_active_arch = "arm64"; },
                     .is_enabled = [] { return true; },
                     .is_checked = [this] { return m_active_arch == "arm64"; },
                 }) &&
        registered;

    registered = m_command_registry.register_command(Commands::Command{
                     .id = std::string(Commands::CommandIds::platform_aarch64),
                     .name = "AArch64",
                     .description = "Target AArch64 platform.",
                     .category = "Architecture",
                     .shortcut_binding = {},
                     .execute = [this] { m_active_arch = "arm64"; },
                     .is_enabled = [] { return true; },
                     .is_checked = [this] { return m_active_arch == "arm64"; },
                 }) &&
        registered;

    registered = m_command_registry.register_command(Commands::Command{
                     .id = std::string(Commands::CommandIds::platform_win32),
                     .name = "Win32",
                     .description = "Target Win32 platform.",
                     .category = "Architecture",
                     .shortcut_binding = {},
                     .execute = [this] { m_active_arch = "x86"; },
                     .is_enabled = [] { return true; },
                     .is_checked = [this] { return m_active_arch == "x86"; },
                 }) &&
        registered;

    // Active Binary Targets
    registered = m_command_registry.register_command(Commands::Command{
                     .id = std::string(Commands::CommandIds::run_zde),
                     .name = "Run ZDE",
                     .description = "Launch ZDE target.",
                     .category = "Run",
                     .shortcut_binding = {},
                     .execute = [this] { m_active_target = "ZDE"; },
                     .is_enabled = [] { return true; },
                     .is_checked = [this] { return m_active_target == "ZDE"; },
                 }) &&
        registered;

    registered = m_command_registry.register_command(Commands::Command{
                     .id = std::string(Commands::CommandIds::run_tests),
                     .name = "Run Tests",
                     .description = "Launch test target.",
                     .category = "Run",
                     .shortcut_binding = {},
                     .execute = [this] { m_active_target = "ZDEUnitTests"; },
                     .is_enabled = [] { return true; },
                     .is_checked = [this] { return m_active_target == "ZDEUnitTests"; },
                 }) &&
        registered;

    return registered;
}

bool StudioViewModel::register_future_commands()
{
    using Commands::KeyCode;
    using Commands::Shortcut;

    bool registered = true;
    const auto add_command = [this, &registered](Commands::Command command) {
        registered = m_command_registry.register_command(std::move(command)) && registered;
    };

    add_command(create_unavailable_command(
        Commands::CommandIds::file_new,
        "New File",
        "Create a new document.",
        "File",
        Shortcut{KeyCode::N, true, false, false}));

    add_command(Commands::Command{
        .id = std::string(Commands::CommandIds::window_new),
        .name = "New Window",
        .description = "Open a new ZDE window.",
        .category = "File",
        .shortcut_binding = Shortcut{KeyCode::N, true, true, false},
        .execute = [this] {
            if (m_actions.request_new_window) {
                m_actions.request_new_window();
            }
        },
        .is_enabled = [this] { return static_cast<bool>(m_actions.request_new_window); },
        .is_checked = {},
    });

    add_command(create_unavailable_command(
        Commands::CommandIds::file_open,
        "Open File",
        "Open a document from disk.",
        "File",
        Shortcut{KeyCode::O, true, false, false}));

    add_command(Commands::Command{
        .id = std::string(Commands::CommandIds::folder_open),
        .name = "Open Folder",
        .description = "Open a folder in the explorer.",
        .category = "File",
        .shortcut_binding = {},
        .execute = [this] {
            if (m_actions.request_open_folder) {
                m_actions.request_open_folder();
            }
        },
        .is_enabled = [this] { return static_cast<bool>(m_actions.request_open_folder); },
        .is_checked = {},
    });

    add_command(Commands::Command{
        .id = std::string(Commands::CommandIds::file_open_recent),
        .name = "Open Recent",
        .description = "Open a recently opened file.",
        .category = "File",
        .shortcut_binding = {},
        .execute = [this] {
            if (m_actions.request_open_recent) {
                m_actions.request_open_recent();
            }
        },
        .is_enabled = [this] { return static_cast<bool>(m_actions.request_open_recent); },
        .is_checked = {},
    });

    add_command(Commands::Command{
        .id = std::string(Commands::CommandIds::file_open_remote),
        .name = "Open Remote",
        .description = "Connect to a remote development environment.",
        .category = "File",
        .shortcut_binding = {},
        .execute = [this] {
            if (m_actions.request_open_remote) {
                m_actions.request_open_remote();
            }
        },
        .is_enabled = [this] { return static_cast<bool>(m_actions.request_open_remote); },
        .is_checked = {},
    });

    add_command(Commands::Command{
        .id = std::string(Commands::CommandIds::project_add_folder),
        .name = "Add Folder to Project",
        .description = "Add an additional folder to the workspace.",
        .category = "Project",
        .shortcut_binding = {},
        .execute = [this] {
            if (m_actions.request_add_folder_to_project) {
                m_actions.request_add_folder_to_project();
            }
        },
        .is_enabled = [this] { return static_cast<bool>(m_actions.request_add_folder_to_project); },
        .is_checked = {},
    });

    add_command(create_unavailable_command(
        Commands::CommandIds::file_save,
        "Save File",
        "Save the active document.",
        "File",
        Shortcut{KeyCode::S, true, false, false}));

    add_command(Commands::Command{
        .id = std::string(Commands::CommandIds::file_save_as),
        .name = "Save As",
        .description = "Save the active document to a new location.",
        .category = "File",
        .shortcut_binding = Shortcut{KeyCode::S, true, true, false},
        .execute = [this] {
            if (m_actions.request_save_as) {
                m_actions.request_save_as();
            }
        },
        .is_enabled = [this] { return static_cast<bool>(m_actions.request_save_as); },
        .is_checked = {},
    });

    add_command(Commands::Command{
        .id = std::string(Commands::CommandIds::file_save_all),
        .name = "Save All",
        .description = "Save all open documents.",
        .category = "File",
        .shortcut_binding = {},
        .execute = [this] {
            if (m_actions.request_save_all) {
                m_actions.request_save_all();
            }
        },
        .is_enabled = [this] { return static_cast<bool>(m_actions.request_save_all); },
        .is_checked = {},
    });

    add_command(create_unavailable_command(
        Commands::CommandIds::file_close,
        "Close File",
        "Close the active document.",
        "File",
        Shortcut{KeyCode::W, true, false, false}));
    add_command(create_unavailable_command(
        Commands::CommandIds::file_delete,
        "Delete File",
        "Delete the active document from disk.",
        "File",
        Shortcut{KeyCode::Delete, true, true, false}));

    add_command(Commands::Command{
        .id = std::string(Commands::CommandIds::window_close),
        .name = "Close Window",
        .description = "Close the current window.",
        .category = "File",
        .shortcut_binding = Shortcut{KeyCode::W, true, true, false},
        .execute = [this] {
            if (m_actions.request_close_window) {
                m_actions.request_close_window();
            }
        },
        .is_enabled = [this] { return static_cast<bool>(m_actions.request_close_window); },
        .is_checked = {},
    });

    add_command(Commands::Command{
        .id = std::string(Commands::CommandIds::window_toggle_fullscreen),
        .name = "Toggle Fullscreen",
        .description = "Toggle the main window fullscreen state.",
        .category = "Window",
        .shortcut_binding = Shortcut{KeyCode::F11, false, false, false},
        .execute = [this] {
            if (m_actions.request_toggle_fullscreen) {
                m_actions.request_toggle_fullscreen();
            }
        },
        .is_enabled = [this] { return static_cast<bool>(m_actions.request_toggle_fullscreen); },
        .is_checked = {},
    });

    add_command(Commands::Command{
        .id = std::string(Commands::CommandIds::window_reset_layout),
        .name = "Reset Layout",
        .description = "Restore default Studio panel and editor layout.",
        .category = "Window",
        .shortcut_binding = {},
        .execute = [this] {
            if (m_actions.request_reset_layout) {
                m_actions.request_reset_layout();
            }
        },
        .is_enabled = [this] { return static_cast<bool>(m_actions.request_reset_layout); },
        .is_checked = {},
    });

    add_command(Commands::Command{
        .id = std::string(Commands::CommandIds::window_minimize),
        .name = "Minimize",
        .description = "Minimize the current window.",
        .category = "Window",
        .shortcut_binding = {},
        .execute = [this] {
            if (m_actions.request_minimize_window) {
                m_actions.request_minimize_window();
            }
        },
        .is_enabled = [this] { return static_cast<bool>(m_actions.request_minimize_window); },
        .is_checked = {},
    });

    add_command(Commands::Command{
        .id = std::string(Commands::CommandIds::window_maximize),
        .name = "Maximize / Restore",
        .description = "Maximize or restore the current window.",
        .category = "Window",
        .shortcut_binding = {},
        .execute = [this] {
            if (m_actions.request_maximize_window) {
                m_actions.request_maximize_window();
            }
        },
        .is_enabled = [this] { return static_cast<bool>(m_actions.request_maximize_window); },
        .is_checked = {},
    });

    add_command(Commands::Command{
        .id = std::string(Commands::CommandIds::view_shader_panel),
        .name = "Shader Sandbox",
        .description = "Toggle the Shader Sandbox panel.",
        .category = "View",
        .shortcut_binding = {},
        .execute = [this] {
            if (m_actions.request_toggle_shader) {
                m_actions.request_toggle_shader();
            }
        },
        .is_enabled = [this] { return static_cast<bool>(m_actions.request_toggle_shader); },
        .is_checked = {},
    });

    add_command(create_unavailable_command(
        Commands::CommandIds::edit_undo,
        "Undo",
        "Undo the last editor operation.",
        "Edit",
        Shortcut{KeyCode::Z, true, false, false}));
    add_command(create_unavailable_command(
        Commands::CommandIds::edit_redo,
        "Redo",
        "Redo the last editor operation.",
        "Edit"));
    add_command(create_unavailable_command(
        Commands::CommandIds::edit_cut,
        "Cut",
        "Cut the current editor selection.",
        "Edit",
        Shortcut{KeyCode::X, true, false, false}));
    add_command(create_unavailable_command(
        Commands::CommandIds::edit_copy,
        "Copy",
        "Copy the current editor selection.",
        "Edit",
        Shortcut{KeyCode::C, true, false, false}));
    add_command(create_unavailable_command(
        Commands::CommandIds::edit_paste,
        "Paste",
        "Paste the editor clipboard.",
        "Edit",
        Shortcut{KeyCode::V, true, false, false}));
    add_command(create_unavailable_command(
        Commands::CommandIds::selection_select_all,
        "Select All",
        "Select all text in the active document.",
        "Selection",
        Shortcut{KeyCode::A, true, false, false}));
    add_command(Commands::Command{
        .id = std::string(Commands::CommandIds::view_terminal_panel),
        .name = "Terminal Panel",
        .description = "Show or hide the integrated terminal.",
        .category = "View",
        .shortcut_binding = {},
        .execute = [this] {
            if (m_actions.request_toggle_terminal) {
                m_actions.request_toggle_terminal();
            }
        },
        .is_enabled = [this] { return static_cast<bool>(m_actions.request_toggle_terminal); },
        .is_checked = {},
    });

    add_command(create_unavailable_command(
        Commands::CommandIds::view_explorer,
        "Show Explorer",
        "Show or focus the Explorer panel.",
        "View"));
    add_command(create_unavailable_command(
        Commands::CommandIds::view_search,
        "Show Search",
        "Show or focus the Search panel.",
        "View"));
    add_command(create_unavailable_command(
        Commands::CommandIds::view_output,
        "Show Output",
        "Show or focus the Output panel.",
        "View"));
    add_command(create_unavailable_command(
        Commands::CommandIds::view_problems,
        "Show Problems",
        "Show or focus the Problems panel.",
        "View"));

    // Additional Editor & Navigation Commands
    add_command(create_unavailable_command(
        Commands::CommandIds::selection_expand, "Expand Selection", "Expand selection to surrounding scope.", "Selection"));
    add_command(create_unavailable_command(
        Commands::CommandIds::selection_shrink, "Shrink Selection", "Shrink selection.", "Selection"));
    add_command(create_unavailable_command(
        Commands::CommandIds::selection_copy_line_up, "Copy Line Up", "Copy current line up.", "Selection"));
    add_command(create_unavailable_command(
        Commands::CommandIds::selection_copy_line_down, "Copy Line Down", "Copy current line down.", "Selection"));
    add_command(create_unavailable_command(
        Commands::CommandIds::selection_move_line_up, "Move Line Up", "Move current line up.", "Selection",
        Shortcut{KeyCode::Up, false, false, true}));
    add_command(create_unavailable_command(
        Commands::CommandIds::selection_move_line_down, "Move Line Down", "Move current line down.", "Selection",
        Shortcut{KeyCode::Down, false, false, true}));
    add_command(create_unavailable_command(
        Commands::CommandIds::selection_duplicate, "Duplicate Selection", "Duplicate selection.", "Selection"));
    add_command(create_unavailable_command(
        Commands::CommandIds::selection_add_cursor_above, "Add Cursor Above", "Add secondary cursor above.", "Selection",
        Shortcut{KeyCode::Up, true, true, false}));
    add_command(create_unavailable_command(
        Commands::CommandIds::selection_add_cursor_below, "Add Cursor Below", "Add secondary cursor below.", "Selection",
        Shortcut{KeyCode::Down, true, true, false}));
    add_command(create_unavailable_command(
        Commands::CommandIds::selection_add_cursors_to_line_ends, "Add Cursors to Line Ends", "Add cursors to line ends.", "Selection"));
    add_command(create_unavailable_command(
        Commands::CommandIds::selection_add_next_occurrence, "Add Next Occurrence", "Select next match.", "Selection"));
    add_command(create_unavailable_command(
        Commands::CommandIds::selection_add_previous_occurrence, "Add Previous Occurrence", "Select previous match.", "Selection"));
    add_command(create_unavailable_command(
        Commands::CommandIds::selection_select_all_occurrences, "Select All Occurrences", "Select all matching occurrences.", "Selection"));
    add_command(create_unavailable_command(
        Commands::CommandIds::selection_switch_multi_cursor_modifier, "Switch Multi-Cursor Modifier", "Switch multi-cursor modifier.", "Selection"));
    add_command(create_unavailable_command(
        Commands::CommandIds::selection_column_selection_mode, "Column Selection Mode", "Toggle column selection mode.", "Selection"));

    add_command(create_unavailable_command(
        Commands::CommandIds::edit_find, "Find", "Find in editor.", "Edit"));
    add_command(create_unavailable_command(
        Commands::CommandIds::edit_find_in_project, "Find in Files", "Find in project files.", "Edit"));
    add_command(create_unavailable_command(
        Commands::CommandIds::edit_toggle_comment, "Toggle Line Comment", "Toggle line comment.", "Edit"));

    add_command(create_unavailable_command(
        Commands::CommandIds::view_zoom_in, "Zoom In", "Zoom in.", "View"));
    add_command(create_unavailable_command(
        Commands::CommandIds::view_zoom_out, "Zoom Out", "Zoom out.", "View"));
    add_command(create_unavailable_command(
        Commands::CommandIds::view_reset_zoom, "Reset Zoom", "Reset zoom.", "View"));
    add_command(create_unavailable_command(
        Commands::CommandIds::view_toggle_left_dock, "Primary Side Bar", "Toggle primary sidebar.", "View"));
    add_command(create_unavailable_command(
        Commands::CommandIds::view_toggle_right_dock, "Secondary Side Bar", "Toggle secondary sidebar.", "View"));
    add_command(create_unavailable_command(
        Commands::CommandIds::view_toggle_bottom_dock, "Toggle Panel", "Toggle bottom panel.", "View"));
    add_command(create_unavailable_command(
        Commands::CommandIds::view_split_up, "Split Up", "Split editor up.", "View"));
    add_command(create_unavailable_command(
        Commands::CommandIds::view_split_down, "Split Down", "Split editor down.", "View"));
    add_command(create_unavailable_command(
        Commands::CommandIds::view_split_left, "Split Left", "Split editor left.", "View"));
    add_command(create_unavailable_command(
        Commands::CommandIds::view_split_right, "Split Right", "Split editor right.", "View"));
    add_command(create_unavailable_command(
        Commands::CommandIds::view_git_panel, "Source Control", "Show Source Control panel.", "View"));
    add_command(create_unavailable_command(
        Commands::CommandIds::view_debugger_panel, "Run and Debug", "Show Debugger panel.", "View"));

    add_command(create_unavailable_command(
        Commands::CommandIds::help_welcome, "Welcome", "Open Welcome page.", "Help"));
    add_command(create_unavailable_command(
        Commands::CommandIds::help_show_all_commands, "Command Palette", "Show all commands.", "Help"));
    add_command(create_unavailable_command(
        Commands::CommandIds::help_editor_playground, "Editor Playground", "Open interactive playground.", "Help"));
    add_command(create_unavailable_command(
        Commands::CommandIds::help_open_walkthrough, "Open Walkthrough", "Open walkthrough.", "Help"));
    add_command(create_unavailable_command(
        Commands::CommandIds::help_provide_feedback, "Report Issue", "Report an issue.", "Help"));
    add_command(create_unavailable_command(
        Commands::CommandIds::help_view_license, "View License", "View license.", "Help"));
    add_command(create_unavailable_command(
        Commands::CommandIds::help_toggle_developer_tools, "Toggle Developer Tools", "Toggle dev tools.", "Help"));
    add_command(create_unavailable_command(
        Commands::CommandIds::help_open_process_explorer, "Process Explorer", "Open process explorer.", "Help"));
    add_command(create_unavailable_command(
        Commands::CommandIds::help_check_for_updates, "Check for Updates", "Check for updates.", "Help"));

    add_command(create_unavailable_command(
        Commands::CommandIds::open_settings, "Settings", "Open settings.", "Preferences"));
    add_command(create_unavailable_command(
        Commands::CommandIds::open_themes, "Color Theme", "Select color theme.", "Preferences"));
    add_command(create_unavailable_command(
        Commands::CommandIds::open_plugins, "Extensions", "Manage extensions.", "Preferences"));
    add_command(create_unavailable_command(
        Commands::CommandIds::edit_profiles, "Configuration Manager", "Edit build profiles.", "Build"));
    add_command(create_unavailable_command(
        Commands::CommandIds::more_tools, "More Tools", "Open more developer tools.", "Tools"));

    // Tab and Window Navigation
    add_command(create_unavailable_command(
        Commands::CommandIds::window_next_tab, "Next Editor Tab", "Switch to next editor tab.", "Window",
        Shortcut{KeyCode::Tab, true, false, false}));
    add_command(create_unavailable_command(
        Commands::CommandIds::window_prev_tab, "Previous Editor Tab", "Switch to previous editor tab.", "Window",
        Shortcut{KeyCode::Tab, true, true, false}));
    add_command(create_unavailable_command(
        Commands::CommandIds::file_close_all, "Close All Editors", "Close all open editor tabs.", "File",
        Shortcut{KeyCode::W, true, false, false}));

    // Additional unused command definitions
    add_command(create_unavailable_command(
        Commands::CommandIds::edit_copy_trim, "Copy Trimmed", "Copy trimmed selection.", "Edit"));
    add_command(create_unavailable_command(
        Commands::CommandIds::view_reset_all_zoom, "Reset All Zoom", "Reset all zoom levels.", "View"));
    add_command(create_unavailable_command(
        Commands::CommandIds::view_toggle_all_docks, "Toggle All Docks", "Toggle all dock panels.", "View"));
    add_command(create_unavailable_command(
        Commands::CommandIds::view_project_panel, "Project Panel", "Show Project panel.", "View"));
    add_command(create_unavailable_command(
        Commands::CommandIds::view_outline_panel, "Outline Panel", "Show Outline panel.", "View"));
    add_command(create_unavailable_command(
        Commands::CommandIds::view_collab_panel, "Collaboration Panel", "Show Collaboration panel.", "View"));
    add_command(create_unavailable_command(
        Commands::CommandIds::view_agent_panel, "Agent Panel", "Show Agent panel.", "View"));
    add_command(create_unavailable_command(
        Commands::CommandIds::view_diagnostics, "Diagnostics", "Show Diagnostics.", "View"));
    add_command(create_unavailable_command(
        Commands::CommandIds::help_download_diagnostics, "Download Diagnostics", "Download diagnostics report.", "Help"));

    return registered;
}

} // namespace Zenvra::Application::ViewModels
