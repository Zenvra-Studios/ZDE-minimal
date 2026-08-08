#include "UI/Chrome/WindowMenuModel.h"

#include "Commands/CommandIds.h"

#include <array>

namespace Zenvra::UI::Chrome
{

namespace
{

constexpr WindowMenuItem separator{{}, {}, true};

constexpr std::array file_items{
    WindowMenuItem{"New", Commands::CommandIds::file_new},
    WindowMenuItem{"New Window", Commands::CommandIds::window_new},
    separator,
    WindowMenuItem{"Open File...", Commands::CommandIds::file_open},
    WindowMenuItem{"Open Folder...", Commands::CommandIds::folder_open},
    WindowMenuItem{"Open Recent...", Commands::CommandIds::file_open_recent},
    WindowMenuItem{"Open Remote...", Commands::CommandIds::file_open_remote},
    separator,
    WindowMenuItem{"Add Folder to Project...", Commands::CommandIds::project_add_folder},
    separator,
    WindowMenuItem{"Save", Commands::CommandIds::file_save},
    WindowMenuItem{"Save As...", Commands::CommandIds::file_save_as},
    WindowMenuItem{"Save All", Commands::CommandIds::file_save_all},
    separator,
    WindowMenuItem{"Close Editor", Commands::CommandIds::file_close},
    WindowMenuItem{"Close Project", Commands::CommandIds::project_close},
    WindowMenuItem{"Close Window", Commands::CommandIds::window_close},
};

constexpr std::array edit_items{
    WindowMenuItem{"Undo", Commands::CommandIds::edit_undo},
    WindowMenuItem{"Redo", Commands::CommandIds::edit_redo},
    separator,
    WindowMenuItem{"Cut", Commands::CommandIds::edit_cut},
    WindowMenuItem{"Copy", Commands::CommandIds::edit_copy},
    WindowMenuItem{"Paste", Commands::CommandIds::edit_paste},
};

constexpr std::array selection_items{
    WindowMenuItem{"Select All", Commands::CommandIds::selection_select_all},
};

constexpr std::array view_items{
    WindowMenuItem{"Explorer", Commands::CommandIds::view_explorer},
    WindowMenuItem{"Search", Commands::CommandIds::view_search},
    WindowMenuItem{"Output", Commands::CommandIds::view_output},
    WindowMenuItem{"Problems", Commands::CommandIds::view_problems},
};

constexpr std::array navigate_items{
    WindowMenuItem{"Navigation commands are not available yet", {}},
};

constexpr std::array project_items{
    WindowMenuItem{"Open Project...", Commands::CommandIds::project_open},
    WindowMenuItem{"Close Project", Commands::CommandIds::project_close},
};

constexpr std::array build_items{
    WindowMenuItem{"Build Project", Commands::CommandIds::build_build_project},
};

constexpr std::array run_items{
    WindowMenuItem{"Start", Commands::CommandIds::run_start},
};

constexpr std::array window_items{
    WindowMenuItem{"Reset Layout", Commands::CommandIds::window_reset_layout},
    WindowMenuItem{"Toggle Fullscreen", Commands::CommandIds::window_toggle_fullscreen},
};

constexpr std::array help_items{
    WindowMenuItem{"About ZDE", Commands::CommandIds::help_about},
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
