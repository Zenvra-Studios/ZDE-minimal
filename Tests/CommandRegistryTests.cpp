#include "Application/ViewModels/StudioViewModel.h"
#include "Commands/CommandIds.h"
#include "Commands/CommandRegistry.h"
#include "UI/Chrome/WindowChromeLayout.h"
#include "UI/Chrome/WindowMenuModel.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace
{

int failure_count = 0;

void expect(bool condition, const char* message)
{
    if (!condition)
    {
        ++failure_count;
        std::cerr << "FAILED: " << message << '\n';
    }
}

void test_command_registration_and_execution()
{
    using namespace Zenvra::Commands;

    CommandRegistry registry;
    int execution_count = 0;

    const bool first_registration = registry.register_command(Command{
        .id = "zde.test.execute",
        .name = "Execute Test",
        .description = "Test command execution.",
        .category = "Test",
        .shortcut_binding = {},
        .execute = [&execution_count] { ++execution_count; },
        .is_enabled = {},
        .is_checked = {},
    });
    const bool duplicate_registration = registry.register_command(Command{
        .id = "zde.test.execute",
        .name = "Duplicate",
        .description = {},
        .category = "Test",
        .shortcut_binding = {},
        .execute = {},
        .is_enabled = {},
        .is_checked = {},
    });

    expect(first_registration, "a valid command must be registered");
    expect(!duplicate_registration, "duplicate command IDs must be rejected");
    expect(registry.size() == 1, "a duplicate must not change registry size");
    expect(
        registry.execute_command("zde.test.execute") == CommandExecutionResult::Executed,
        "an enabled command must execute");
    expect(execution_count == 1, "the command callback must run exactly once");
    expect(
        registry.execute_command("zde.test.missing") == CommandExecutionResult::NotFound,
        "an unknown command must report NotFound");
}

void test_disabled_command()
{
    using namespace Zenvra::Commands;

    CommandRegistry registry;
    bool executed = false;
    const bool registered = registry.register_command(Command{
        .id = "zde.test.disabled",
        .name = "Disabled Test",
        .description = {},
        .category = "Test",
        .shortcut_binding = {},
        .execute = [&executed] { executed = true; },
        .is_enabled = [] { return false; },
        .is_checked = {},
    });

    expect(registered, "a disabled command definition must still register");
    expect(
        registry.execute_command("zde.test.disabled") == CommandExecutionResult::Disabled,
        "a disabled command must not execute");
    expect(!executed, "a disabled command callback must not run");
}

void test_studio_view_model_routes_actions()
{
    using Zenvra::Application::ViewModels::StudioActions;
    using Zenvra::Application::ViewModels::StudioViewModel;
    using Zenvra::Commands::CommandExecutionResult;

    bool close_requested = false;
    bool about_requested = false;
    StudioViewModel view_model(StudioActions{
        .request_close = [&close_requested] { close_requested = true; },
        .show_about = [&about_requested] { about_requested = true; },
    });

    expect(view_model.initialize(), "the Studio ViewModel must initialize its command model");
    expect(view_model.initialize(), "initialization must be idempotent");
    expect(view_model.get_command_registry().size() == 17, "the initial command contract must contain 17 commands");
    expect(
        view_model.execute_command(Zenvra::Commands::CommandIds::file_exit) == CommandExecutionResult::Executed,
        "the exit command must execute through the ViewModel");
    expect(close_requested, "the exit command must route to the injected close action");
    expect(
        view_model.execute_command(Zenvra::Commands::CommandIds::help_about) == CommandExecutionResult::Executed,
        "the about command must execute through the ViewModel");
    expect(about_requested, "the about command must route to the injected about action");
    expect(
        view_model.execute_command(Zenvra::Commands::CommandIds::file_open) == CommandExecutionResult::Disabled,
        "future commands must be visible but disabled until their feature exists");
}

void test_window_chrome_layout_excludes_interactive_regions()
{
    using namespace Zenvra::UI::Chrome;

    const WindowChromeLayout layout_engine;
    const WindowChromeLayoutResult layout = layout_engine.calculate(1600.0F, 1.0F);

    expect(layout.titlebar_bounds.height == 35.0F, "the default titlebar must use the 35px design token");
    expect(layout.visible_menu_count == window_menu_count, "all main menus must fit at the default width");
    expect(!layout.command_center_bounds.is_empty(), "the command center must be visible at the default width");
    expect(
        layout.get_window_control(1580.0F, 17.0F) == WindowControl::Close,
        "the close control must remain at the far-right edge");
    expect(
        !layout.is_drag_region(layout.menu_regions[0].bounds.x + 2.0F, 17.0F),
        "menu items must be excluded from the drag region");
    expect(
        !layout.is_drag_region(
            layout.command_center_bounds.x + 2.0F,
            layout.command_center_bounds.y + 2.0F),
        "the command center must be excluded from the drag region");
    expect(
        layout.is_drag_region(
            layout.command_center_bounds.right() + 4.0F,
            layout.titlebar_bounds.height * 0.5F),
        "unused titlebar space must remain draggable");
}

void test_window_chrome_layout_is_responsive_and_dpi_aware()
{
    using namespace Zenvra::UI::Chrome;

    const WindowChromeLayout layout_engine;
    const WindowChromeLayoutResult narrow_layout = layout_engine.calculate(600.0F, 1.0F);
    const WindowChromeLayoutResult compact_layout = layout_engine.calculate(720.0F, 1.0F);
    const WindowChromeLayoutResult scaled_layout = layout_engine.calculate(2400.0F, 2.0F);

    expect(
        narrow_layout.command_center_bounds.is_empty(),
        "the command center must hide before overlapping menus or window controls");
    expect(
        narrow_layout.close_bounds.right() == 600.0F,
        "window controls must remain visible at narrow widths");
    expect(
        narrow_layout.visible_menu_count < window_menu_count,
        "secondary menu content must collapse at narrow widths");
    expect(narrow_layout.has_overflow_menu(), "collapsed menus must remain available through overflow");
    expect(
        compact_layout.visible_menu_count == 3,
        "the minimum supported width must retain File, Edit, and Selection");
    expect(compact_layout.has_overflow_menu(), "the minimum supported width must expose an ellipsis menu");
    expect(
        compact_layout.first_overflow_menu_index == compact_layout.visible_menu_count,
        "overflow must begin immediately after the last visible menu");
    expect(
        !compact_layout.command_center_bounds.is_empty(),
        "the command center must shrink to its responsive minimum before hiding");
    expect(
        !compact_layout.is_drag_region(
            compact_layout.overflow_menu_bounds.x + 1.0F,
            compact_layout.overflow_menu_bounds.y + 1.0F),
        "the ellipsis button must be excluded from the drag region");
    expect(scaled_layout.titlebar_bounds.height == 70.0F, "titlebar metrics must scale with DPI");
    expect(scaled_layout.close_bounds.width == 92.0F, "window controls must scale with DPI");
}

void test_window_menu_model_matches_chrome_contract()
{
    const std::span<const Zenvra::UI::Chrome::WindowMenu> menus =
        Zenvra::UI::Chrome::get_window_menu_model();

    expect(
        menus.size() == Zenvra::UI::Chrome::window_menu_count,
        "the portable menu model must match the chrome menu region count");
    expect(menus.front().label == "File", "the first top-level menu must be File");
    expect(menus.back().label == "Help", "the last top-level menu must be Help");
    const auto& layout_labels = Zenvra::UI::Chrome::WindowChromeLayout::get_menu_labels();
    for (std::size_t menu_index = 0; menu_index < menus.size(); ++menu_index)
    {
        expect(
            layout_labels[menu_index] == menus[menu_index].label,
            "the chrome layout and portable menu model must use the same labels");
    }
    expect(
        menus.front().items.back().command_id == Zenvra::Commands::CommandIds::file_exit,
        "the File menu must route Exit through the stable command ID");
    expect(
        menus.back().items.front().command_id == Zenvra::Commands::CommandIds::help_about,
        "the Help menu must route About through the stable command ID");

    Zenvra::Application::ViewModels::StudioViewModel view_model({});
    expect(view_model.initialize(), "the menu contract requires an initialized Studio ViewModel");
    const Zenvra::Commands::CommandRegistry& registry = view_model.get_command_registry();
    for (const Zenvra::UI::Chrome::WindowMenu& menu : menus)
    {
        for (const Zenvra::UI::Chrome::WindowMenuItem& item : menu.items)
        {
            if (!item.command_id.empty())
            {
                expect(
                    registry.find_command(item.command_id) != nullptr,
                    "every actionable menu item must resolve to a registered command");
            }
        }
    }
}

} // namespace

int main()
{
    test_command_registration_and_execution();
    test_disabled_command();
    test_studio_view_model_routes_actions();
    test_window_chrome_layout_excludes_interactive_regions();
    test_window_chrome_layout_is_responsive_and_dpi_aware();
    test_window_menu_model_matches_chrome_contract();

    if (failure_count != 0)
    {
        std::cerr << failure_count << " test assertion(s) failed.\n";
        return EXIT_FAILURE;
    }

    std::cout << "All ZDE unit tests passed.\n";
    return EXIT_SUCCESS;
}
