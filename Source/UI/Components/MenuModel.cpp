#include "UI/Components/MenuModel.h"
#include "Commands/CommandIds.h"
#include <array>

namespace Zenvra::UI::Components
{

namespace
{

constexpr MenuItem separator{{}, {}, true};

constexpr std::array file_items{
    MenuItem{"New File", Commands::CommandIds::file_new},
    MenuItem{"Open File...", Commands::CommandIds::file_open},
    MenuItem{"Save File", Commands::CommandIds::file_save},
    MenuItem{"Close File", Commands::CommandIds::file_close},
    MenuItem{"Delete File", Commands::CommandIds::file_delete},
    separator,
    MenuItem{"Exit", Commands::CommandIds::file_exit},
};

constexpr std::array edit_items{
    MenuItem{"Undo", Commands::CommandIds::edit_undo},
    MenuItem{"Redo", Commands::CommandIds::edit_redo},
    separator,
    MenuItem{"Cut", Commands::CommandIds::edit_cut},
    MenuItem{"Copy", Commands::CommandIds::edit_copy},
    MenuItem{"Paste", Commands::CommandIds::edit_paste},
};

constexpr std::array selection_items{
    MenuItem{"Select All", Commands::CommandIds::selection_select_all},
};

constexpr std::array view_items{
    MenuItem{"Explorer", Commands::CommandIds::view_explorer},
    MenuItem{"Search", Commands::CommandIds::view_search},
    MenuItem{"Output", Commands::CommandIds::view_output},
    MenuItem{"Problems", Commands::CommandIds::view_problems},
};

constexpr std::array navigate_items{
    MenuItem{"Navigation commands are not available yet", {}},
};

constexpr std::array project_items{
    MenuItem{"Open Project...", Commands::CommandIds::project_open},
    MenuItem{"Close Project", Commands::CommandIds::project_close},
};

constexpr std::array build_items{
    MenuItem{"Build Project", Commands::CommandIds::build_build_project},
};

constexpr std::array run_items{
    MenuItem{"Start", Commands::CommandIds::run_start},
};

constexpr std::array window_items{
    MenuItem{"Reset Layout", Commands::CommandIds::window_reset_layout},
    MenuItem{"Toggle Fullscreen", Commands::CommandIds::window_toggle_fullscreen},
};

constexpr std::array help_items{
    MenuItem{"About ZDE", Commands::CommandIds::help_about},
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
    MenuItem{"Settings...", Commands::CommandIds::open_settings},
    MenuItem{"Themes...", Commands::CommandIds::open_themes},
    separator,
    MenuItem{"Plugins...", Commands::CommandIds::open_plugins},
};

constexpr std::array ellipsis_items{
    MenuItem{"Search...", Commands::CommandIds::view_search},
    MenuItem{"Terminal", Commands::CommandIds::view_output},
    separator,
    MenuItem{"More Tools...", Commands::CommandIds::more_tools},
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
