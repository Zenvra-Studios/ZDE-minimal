#include "Application/ViewModels/StudioViewModel.h"

#include "Commands/CommandIds.h"

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
        .execute = {},
        .is_enabled = [] { return false; },
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
    add_command(create_unavailable_command(
        Commands::CommandIds::file_open,
        "Open File",
        "Open a document from disk.",
        "File",
        Shortcut{KeyCode::O, true, false, false}));
    add_command(create_unavailable_command(
        Commands::CommandIds::file_save,
        "Save File",
        "Save the active document.",
        "File",
        Shortcut{KeyCode::S, true, false, false}));
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
    add_command(create_unavailable_command(
        Commands::CommandIds::window_reset_layout,
        "Reset Layout",
        "Restore the default Studio panel layout.",
        "Window"));
    add_command(create_unavailable_command(
        Commands::CommandIds::window_toggle_fullscreen,
        "Toggle Fullscreen",
        "Toggle the main window fullscreen state.",
        "Window"));
    add_command(create_unavailable_command(
        Commands::CommandIds::project_open,
        "Open Project",
        "Open a ZDE project.",
        "Project"));
    add_command(create_unavailable_command(
        Commands::CommandIds::project_close,
        "Close Project",
        "Close the active ZDE project.",
        "Project"));
    add_command(create_unavailable_command(
        Commands::CommandIds::build_build_project,
        "Build Project",
        "Build the active project.",
        "Build"));
    add_command(create_unavailable_command(
        Commands::CommandIds::run_start,
        "Start",
        "Run the active project.",
        "Run"));

    return registered;
}

} // namespace Zenvra::Application::ViewModels
