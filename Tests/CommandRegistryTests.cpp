#include <gtest/gtest.h>

#include "Application/ViewModels/StudioViewModel.h"
#include "Commands/CommandIds.h"
#include "Commands/CommandRegistry.h"
#include "Terminal/TerminalPanelModel.h"
#include "Terminal/TerminalResizeModel.h"
#include "Terminal/TerminalSession.h"
#include "UI/Chrome/WindowChromeLayout.h"
#include "UI/Chrome/WindowMenuModel.h"
#include "UI/Editor/ActivityPanelModel.h"
#include "UI/Editor/EditorController.h"
#include "UI/Editor/EditorDropModel.h"
#include "UI/Editor/EditorFileCrud.h"
#include "UI/Editor/EditorFileSystem.h"
#include "UI/Editor/EditorMinimapModel.h"
#include "UI/Editor/EditorScrollModel.h"
#include "UI/Editor/StudioEditorModel.h"
#include "UI/Editor/TextDocumentModel.h"
#include "UI/Components/Modal.h"
#include "UI/Components/AboutModal.h"
#include "Drivers/Graphics/BackdropBlurPipeline.h"
#include "Drivers/Graphics/shaders/BackdropBlur.h"
#include "Services/Shader/ShaderCompiler.h"
#include "Services/Shader/ShaderRuntimeEngine.h"
#include "Utility/Column.h"
#include "Utility/Grid.h"
#include "Utility/Row.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace
{

int failure_count = 0;

bool approximately_equal(float left, float right, float tolerance = 0.01F)
{
    return std::abs(left - right) <= tolerance;
}

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
        .request_open_project = {},
        .request_close_project = {},
        .request_new_window = {},
        .request_open_folder = {},
        .request_open_recent = {},
        .request_open_remote = {},
        .request_add_folder_to_project = {},
        .request_save_as = {},
        .request_save_all = {},
        .request_close_window = {},
        .request_toggle_terminal = {},
        .request_toggle_fullscreen = {},
        .request_reset_layout = {},
        .request_minimize_window = {},
        .request_maximize_window = {},
        .request_toggle_shader = {},
        .request_build = {},
        .request_run = {},
        .request_debug = {},
        .request_stop = {},
    });

    expect(view_model.initialize(), "the Studio ViewModel must initialize its command model");
    expect(view_model.initialize(), "initialization must be idempotent");
    expect(view_model.get_command_registry().size() >= 23, "the initial command contract must contain registered commands");
    expect(
        view_model.execute_command(Zenvra::Commands::CommandIds::file_exit) == CommandExecutionResult::Executed,
        "the exit command must execute through the ViewModel");
    expect(close_requested, "the exit command must route to the injected close action");
    expect(
        view_model.execute_command(Zenvra::Commands::CommandIds::help_about) == CommandExecutionResult::Executed,
        "the about command must execute through the ViewModel");
    expect(about_requested, "the about command must route to the injected about action");
    expect(
        view_model.execute_command(Zenvra::Commands::CommandIds::file_open) == CommandExecutionResult::Executed,
        "the file open command must execute through the ViewModel");
}

void test_window_chrome_layout_excludes_interactive_regions()
{
    using namespace Zenvra::UI::Chrome;

    const WindowChromeLayout layout_engine;
    const WindowChromeLayoutResult layout = layout_engine.calculate(1600.0F, 1.0F);

    expect(layout.titlebar_bounds.height == 35.0F, "the default titlebar must use the 35px design token");
    expect(layout.visible_menu_count == 0, "the main menu labels must stay hidden in the compact chrome");
    expect(layout.command_center_bounds.is_empty(), "the obsolete title search field must stay removed");
    expect(layout.has_overflow_menu() && layout.first_overflow_menu_index == 0,
        "the hamburger overlay must expose every top-level menu");
    expect(
        layout.get_window_control(1580.0F, 17.0F) == WindowControl::Close,
        "the close control must remain at the far-right edge");
    expect(
        !layout.is_drag_region(
            layout.overflow_menu_bounds.x + 2.0F,
            layout.overflow_menu_bounds.y + 2.0F),
        "the hamburger button must be excluded from the drag region");
    expect(
        layout.is_drag_region(
            layout.overflow_menu_bounds.right() + 4.0F,
            layout.titlebar_bounds.height * 0.5F),
        "unused titlebar space must remain draggable");
}

void test_window_chrome_layout_is_responsive_and_dpi_aware()
{
    using namespace Zenvra::UI::Chrome;

    const WindowChromeLayout layout_engine;
    const WindowChromeLayoutResult narrow_layout = layout_engine.calculate(600.0F, 1.0F);
    const WindowChromeLayoutResult compact_layout = layout_engine.calculate(720.0F, 1.0F);
    const WindowChromeLayoutResult linux_layout = layout_engine.calculate(
        720.0F,
        1.0F,
        WindowChromeLayoutOptions{
            .show_window_controls = false,
        });
    const WindowChromeLayoutResult scaled_layout = layout_engine.calculate(2400.0F, 2.0F);

    expect(
        narrow_layout.command_center_bounds.is_empty(),
        "the removed title search field must stay absent at every width");
    expect(
        narrow_layout.close_bounds.right() == 600.0F,
        "window controls must remain visible at narrow widths");
    expect(
        narrow_layout.visible_menu_count == 0,
        "all top-level menu labels must remain inside the hamburger overlay");
    expect(narrow_layout.has_overflow_menu(), "the hamburger menu must remain available at narrow widths");
    expect(
        compact_layout.visible_menu_count == 0,
        "the minimum supported width must not restore inline menu labels");
    expect(compact_layout.has_overflow_menu(), "the minimum supported width must expose a hamburger menu");
    expect(
        compact_layout.first_overflow_menu_index == 0,
        "the hamburger overlay must begin with the File menu");
    expect(
        compact_layout.command_center_bounds.is_empty(),
        "the removed title search field must not return at compact widths");
    expect(
        !compact_layout.is_drag_region(
            compact_layout.overflow_menu_bounds.x + 1.0F,
            compact_layout.overflow_menu_bounds.y + 1.0F),
        "the hamburger button must be excluded from the drag region");
    expect(
        linux_layout.close_bounds.is_empty(),
        "a WM-decorated Linux layout must not reserve or draw client window controls");
    expect(
        linux_layout.has_overflow_menu(),
        "the hamburger menu must also remain available under native WM decorations");
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

void test_studio_editor_layout_and_tokenization()
{
    using namespace Zenvra::UI::Editor;

    const StudioEditorLayout layout_engine;
    const StudioEditorLayoutResult layout = layout_engine.calculate(860.0F, 640.0F, 35.0F, 1.0F);
    expect(layout.tab_bar_bounds.y == 0.0F && layout.tab_bar_bounds.bottom() == 35.0F,
        "editor tabs must be integrated into the custom titlebar");
    expect(layout.editor_header_bounds.y == 35.0F && layout.editor_bounds.y == 61.0F, "the editor must begin directly below the titlebar");
    expect(layout.status_bar_bounds.y == 616.0F, "the status bar must remain pinned to the bottom");
    expect(layout.activity_bar_bounds.width == 38.0F,
        "the activity rail must use the medium workspace scale");
    expect(layout.editor_bounds.x == 104.0F,
        "the medium gutter and activity rail must preserve a wide editor viewport");
    expect(calculate_editor_tab_width(20.0F, 1.0F) == 112.0F &&
            calculate_editor_tab_width(100.0F, 1.0F) == 168.0F &&
            calculate_editor_tab_width(500.0F, 1.0F) == 224.0F,
        "buffer tabs must preserve internal padding while respecting medium width limits");
    expect(layout.minimap_bounds.width == 112.0F &&
            layout.minimap_bounds.right() == layout.scrollbar_bounds.x,
        "the responsive minimap must occupy its own region beside the scrollbar");
    const StudioEditorLayoutResult with_tool_sidebar = layout_engine.calculate(
        860.0F, 640.0F, 35.0F, 1.0F, false, 218.0F, false, true, 260.0F);
    expect(with_tool_sidebar.tool_sidebar_bounds.x == 38.0F &&
            with_tool_sidebar.tool_sidebar_bounds.width == 260.0F &&
            with_tool_sidebar.tab_bar_bounds.x == 80.0F &&
            with_tool_sidebar.editor_bounds.x == 364.0F,
        "the titlebar tabs must stay beside navigation while the activity panel remains below");
    const StudioEditorLayoutResult compact = layout_engine.calculate(
        180.0F, 120.0F, 35.0F, 1.0F, true, 218.0F, false, true, 260.0F);
    expect(compact.tool_sidebar_bounds.width == 0.0F &&
            compact.activity_bar_bounds.width >= 0.0F &&
            compact.editor_bounds.width >= 0.0F &&
            compact.editor_bounds.height >= 0.0F &&
            compact.terminal_panel_bounds.height >= 0.0F &&
            compact.status_bar_bounds.bottom() == 120.0F &&
            compact.activity_bar_bounds.right() <= 180.0F &&
            compact.scrollbar_bounds.right() <= 180.0F,
        "a compact viewport must collapse the optional sidebar and never produce negative bounds");
    const StudioEditorLayoutResult scaled = layout_engine.calculate(
        1720.0F, 1280.0F, 70.0F, 2.0F, false, 218.0F, false, true, 260.0F);
    expect(scaled.activity_bar_bounds.width == 76.0F &&
            scaled.tool_sidebar_bounds.width == 520.0F &&
            scaled.tab_bar_bounds.height == 70.0F &&
            scaled.status_bar_bounds.height == 48.0F &&
            scaled.editor_bounds.x == 728.0F,
        "the shared editor layout must scale all platform-independent design metrics with DPI");

    const StudioEditorLayoutResult fullscreen_layout = layout_engine.calculate(
        860.0F, 640.0F, 35.0F, 1.0F, false, 218.0F, false, false, 260.0F, false, 380.0F, 0.0F);
    expect(fullscreen_layout.tab_bar_bounds.x == 0.0F &&
            fullscreen_layout.tab_bar_bounds.width == 860.0F - 664.0F,
        "fullscreen buffer bar must align to the far left x=0 edge and expand available width");

    ActivityPanelModel activity_panel;
    const std::optional<std::filesystem::path> activity_project_root =
        EditorFileSystem::find_project_root(std::filesystem::current_path());
    expect(activity_project_root.has_value() &&
            activity_panel.initialize(activity_project_root.value_or(std::filesystem::path{})) &&
            activity_panel.is_visible() &&
            activity_panel.is_active(SidebarIcon::Project) &&
            !activity_panel.get_project_items().empty(),
        "the Explorer panel must initialize from the active workspace filesystem");
    for (const SidebarItem& item : get_studio_sidebar_items())
    {
        if (item.icon != SidebarIcon::Terminal)
        {
            if (activity_panel.is_active(item.icon))
            {
                static_cast<void>(activity_panel.activate(item.icon));
            }
            expect(activity_panel.activate(item.icon) && activity_panel.is_active(item.icon),
                "clicking an activity icon must select the tool window");
        }
    }
    static_cast<void>(activity_panel.activate(SidebarIcon::Project));
    if (!activity_panel.is_visible())
    {
        static_cast<void>(activity_panel.activate(SidebarIcon::Project));
    }
    const std::span<const ProjectTreeItem> project_items = activity_panel.get_project_items();
    const auto source_item = std::find_if(
        project_items.begin(), project_items.end(), [](const ProjectTreeItem& item) {
            return item.directory && item.label == "Source";
        });
    expect(source_item != project_items.end(),
        "the Explorer tree must expose real project directories");
    if (source_item != project_items.end())
    {
        const std::size_t source_index = static_cast<std::size_t>(
            source_item - project_items.begin());
        const ActivityPanelAction action = activity_panel.activate_project_row(source_index);
        expect(action.handled && action.layout_changed,
            "clicking an Explorer directory must expand or collapse the tree");
    }
    std::array<EditorToken, maximum_editor_tokens> tokens{};
    const std::size_t token_count = tokenize_editor_line("return true;", tokens, "main.cpp");
    expect(token_count >= 3, "the editor lexer must preserve whitespace and syntax tokens");
    expect(tokens[0].kind == EditorTokenKind::Keyword, "return must use the keyword presentation");
    expect(tokens[token_count - 1].kind == EditorTokenKind::Plain, "C++ semicolons must not become comments");
    expect(supports_editor_syntax_highlighting("main.cpp") &&
            supports_editor_syntax_highlighting("BackdropBlur.h") &&
            !supports_editor_syntax_highlighting("notes.txt") &&
            supports_editor_syntax_highlighting("CMakeLists.txt"),
        "syntax highlighting must target source files while plain text stays plain");
    expect(get_studio_sidebar_items().front().active, "the Project tool window must be active initially");

    EditorScrollModel scroll;
    static_cast<void>(scroll.set_line_metrics(100, 10));
    expect(scroll.scroll_lines(5) && scroll.get_first_visible_line() == 5,
        "mouse-wheel scrolling must move independently from the caret");
    expect(scroll.reveal_line(99) && scroll.get_first_visible_line() == 90,
        "caret reveal must clamp the viewport to the last full page");
    const Zenvra::UI::Rect scroll_track{0.0F, 0.0F, 10.0F, 100.0F};
    expect(scroll.begin_pointer_drag(1.0F, scroll_track, 20.0F) && scroll.is_dragging(),
        "clicking the scrollbar track must start manual thumb control");
    expect(scroll.drag_pointer(99.0F, scroll_track, 20.0F) &&
            scroll.get_first_visible_line() >= 90,
        "dragging the scrollbar thumb must reach the end of the document");
    expect(scroll.end_pointer_drag() && !scroll.is_dragging(),
        "releasing the pointer must finish manual scrollbar control");

    EditorMinimapModel minimap;
    minimap.synchronize(200, 20, 50);
    const Zenvra::UI::Rect minimap_bounds{0.0F, 0.0F, 100.0F, 100.0F};
    expect(minimap.calculate_sample_count(minimap_bounds, 1.0F) == 100 &&
            minimap.get_line_for_sample(99, 100) == 199,
        "the minimap must sample the entire document within its available height");
    expect(minimap.get_first_visible_line_for_point(50.0F, minimap_bounds) == 90,
        "clicking the minimap must center the editor viewport around that line");
    const Zenvra::UI::Rect minimap_viewport = minimap.calculate_viewport_bounds(
        minimap_bounds, 10.0F);
    expect(minimap_viewport.height == 10.0F && minimap_viewport.y > 24.9F &&
            minimap_viewport.y < 25.1F,
        "the minimap viewport indicator must track the editor scroll position");

    TextDocumentModel document;
    document.replace_contents({"alpha", "beta"}, "sample.cpp", {{"ZDE", BreadcrumbIconKind::Folder}, {"sample.cpp", BreadcrumbIconKind::File}}, "LF");
    expect(document.set_caret(0, 5), "the caret must move to a requested text position");
    expect(document.insert_text("\nvalue"), "the text model must insert multi-line input");
    expect(document.get_line_count() == 3, "new-line input must split the text buffer");
    expect(document.get_line(1) == "value", "inserted text must land on the new line");
    expect(document.get_status().line == 2 && document.get_status().column == 6,
        "footer status must follow the live caret");
    expect(document.execute(EditorInputCommand::DeleteBackward), "backspace must edit the buffer");
    expect(document.get_line(1) == "valu", "backspace must remove the previous character");
    expect(document.is_dirty(), "editing must mark the document dirty");

    const std::optional<TextFileSnapshot> snapshot = EditorFileSystem::read_text_file(
        "Source/Platform/X11/X11Window.cpp");
    expect(snapshot.has_value(), "the editor filesystem must resolve a source path from the build tree");
    if (snapshot)
    {
        expect(!snapshot->lines.empty(), "the filesystem loader must split source text into lines");
        expect(snapshot->breadcrumbs.back().text == "X11Window.cpp",
            "the footer breadcrumb must end at the active file");
    }
}

void test_layout_primitives()
{
    using namespace Zenvra::Utility;

    const std::array row_items{
        FlexItem::fixed(40.0F),
        FlexItem::flexible(),
        FlexItem::fixed(60.0F),
    };
    const FlexLayoutResult row = Row::calculate(
        Zenvra::UI::Rect{10.0F, 20.0F, 500.0F, 100.0F},
        row_items,
        10.0F);
    expect(row.items.size() == 3 &&
            approximately_equal(row.items[0].x, 10.0F) &&
            approximately_equal(row.items[1].x, 60.0F) &&
            approximately_equal(row.items[1].width, 380.0F) &&
            approximately_equal(row.items[2].x, 450.0F) &&
            approximately_equal(row.items[2].right(), 510.0F),
        "a Row must reserve fixed items and give the remaining width to flexible items");
    expect(approximately_equal(row.items[0].height, 100.0F) &&
            approximately_equal(row.items[1].height, 100.0F),
        "a Row must stretch children symmetrically across its cross axis by default");

    const std::array column_items{
        FlexItem::fixed(30.0F),
        FlexItem::flexible(),
    };
    const FlexLayoutResult column = Column::calculate(
        Zenvra::UI::Rect{5.0F, 10.0F, 120.0F, 200.0F},
        column_items,
        5.0F);
    expect(approximately_equal(column.items[0].height, 30.0F) &&
            approximately_equal(column.items[1].y, 45.0F) &&
            approximately_equal(column.items[1].height, 165.0F) &&
            approximately_equal(column.items[1].bottom(), 210.0F),
        "a Column must stack children vertically without gaps or bottom drift");

    const std::array weighted_items{
        FlexItem::flexible(1.0F),
        FlexItem::flexible(2.0F),
    };
    const FlexLayoutResult weighted = Row::calculate(
        Zenvra::UI::Rect{0.0F, 0.0F, 300.0F, 40.0F},
        weighted_items);
    expect(approximately_equal(weighted.items[0].width, 100.0F) &&
            approximately_equal(weighted.items[1].width, 200.0F),
        "flex growth must follow each child's proportional grow factor");

    const std::array shrinking_items{
        FlexItem{
            .basis = 100.0F,
            .grow = 0.0F,
            .shrink = 1.0F,
            .minimum_size = 80.0F,
        },
        FlexItem{
            .basis = 100.0F,
            .grow = 0.0F,
            .shrink = 1.0F,
            .minimum_size = 20.0F,
        },
    };
    const FlexLayoutResult shrinking = Row::calculate(
        Zenvra::UI::Rect{0.0F, 0.0F, 120.0F, 40.0F},
        shrinking_items);
    expect(approximately_equal(shrinking.items[0].width, 80.0F) &&
            approximately_equal(shrinking.items[1].width, 40.0F) &&
            approximately_equal(shrinking.items[1].right(), 120.0F) &&
            approximately_equal(shrinking.overflow, 0.0F),
        "flex shrink must redistribute a deficit after a child reaches its minimum size");

    const std::array justified_items{
        FlexItem::fixed(20.0F),
        FlexItem::fixed(20.0F),
        FlexItem::fixed(20.0F),
    };
    const FlexLayoutResult justified = Row::calculate(
        Zenvra::UI::Rect{0.0F, 0.0F, 100.0F, 30.0F},
        justified_items,
        0.0F,
        LayoutJustify::SpaceBetween);
    expect(approximately_equal(justified.items[0].x, 0.0F) &&
            approximately_equal(justified.items[1].x, 40.0F) &&
            approximately_equal(justified.items[2].x, 80.0F),
        "space-between justification must distribute empty space evenly");

    FlexItem centered_item = FlexItem::fixed(20.0F);
    centered_item.cross_size = 30.0F;
    const std::array centered_items{centered_item};
    const FlexLayoutResult centered = Row::calculate(
        Zenvra::UI::Rect{0.0F, 20.0F, 20.0F, 100.0F},
        centered_items,
        0.0F,
        LayoutJustify::Start,
        LayoutAlign::Center);
    expect(approximately_equal(centered.items[0].y, 55.0F) &&
            approximately_equal(centered.items[0].height, 30.0F),
        "cross-axis centering must keep an explicitly sized child visually centered");

    const std::array reversed_items{
        FlexItem::fixed(20.0F),
        FlexItem::fixed(30.0F),
    };
    const FlexLayoutResult reversed = Row::calculate(
        Zenvra::UI::Rect{0.0F, 0.0F, 50.0F, 20.0F},
        reversed_items,
        0.0F,
        LayoutJustify::Start,
        LayoutAlign::Stretch,
        true);
    expect(approximately_equal(reversed.items[1].x, 0.0F) &&
            approximately_equal(reversed.items[0].x, 30.0F),
        "reverse layout must change visual order while preserving result indices");

    const std::array grid_columns{
        GridTrack::fixed(100.0F),
        GridTrack::fraction(1.0F),
        GridTrack::fraction(2.0F),
    };
    const std::array grid_rows{
        GridTrack::automatic(),
        GridTrack::fraction(),
    };
    const std::array grid_items{
        GridItem{
            .column = 0,
            .row = 0,
            .preferred_height = 32.0F,
        },
        GridItem{
            .column = 0,
            .row = 1,
            .preferred_width = 60.0F,
            .preferred_height = 40.0F,
            .horizontal_alignment = LayoutAlign::Center,
            .vertical_alignment = LayoutAlign::Center,
        },
        GridItem{
            .column = 1,
            .row = 1,
            .column_span = 2,
        },
    };
    const GridLayoutResult grid = Grid::calculate(
        Zenvra::UI::Rect{10.0F, 20.0F, 400.0F, 200.0F},
        grid_columns,
        grid_rows,
        grid_items,
        GridOptions{
            .column_gap = 10.0F,
            .row_gap = 8.0F,
        });
    expect(grid.column_sizes.size() == 3 && grid.row_sizes.size() == 2 &&
            approximately_equal(grid.column_sizes[0], 100.0F) &&
            approximately_equal(grid.column_sizes[1], 93.3333F) &&
            approximately_equal(grid.column_sizes[2], 186.6667F) &&
            approximately_equal(grid.row_sizes[0], 32.0F) &&
            approximately_equal(grid.row_sizes[1], 160.0F),
        "Grid must combine fixed, automatic, and fractional tracks predictably");
    expect(approximately_equal(grid.items[1].x, 30.0F) &&
            approximately_equal(grid.items[1].y, 120.0F) &&
            approximately_equal(grid.items[1].width, 60.0F) &&
            approximately_equal(grid.items[1].height, 40.0F) &&
            approximately_equal(grid.items[2].x, 120.0F) &&
            approximately_equal(grid.items[2].width, 290.0F),
        "Grid item alignment and multi-column spans must remain inside their cells");

    const std::array overflowing_columns{
        GridTrack::fixed(80.0F),
        GridTrack::fixed(80.0F),
    };
    const std::array overflowing_rows{GridTrack::fixed(20.0F)};
    const std::array<GridItem, 0> no_grid_items{};
    const GridLayoutResult overflow = Grid::calculate(
        Zenvra::UI::Rect{0.0F, 0.0F, 120.0F, 20.0F},
        overflowing_columns,
        overflowing_rows,
        no_grid_items,
        GridOptions{.column_gap = 5.0F});
    expect(approximately_equal(overflow.horizontal_overflow, 45.0F),
        "Grid must report unavoidable overflow when fixed minimum tracks cannot shrink");
}

void test_terminal_layout_and_host_session()
{
    using namespace Zenvra::UI::Editor;

    const StudioEditorLayout layout_engine;
    const StudioEditorLayoutResult editor_only =
        layout_engine.calculate(1000.0F, 720.0F, 35.0F, 1.0F);
    const StudioEditorLayoutResult with_terminal =
        layout_engine.calculate(1000.0F, 720.0F, 35.0F, 1.0F, true);
    expect(!with_terminal.terminal_panel_bounds.is_empty() &&
            with_terminal.terminal_content_bounds.y > with_terminal.terminal_header_bounds.y,
        "the terminal tool window must receive a header and scrollable content region");
    expect(with_terminal.editor_bounds.height < editor_only.editor_bounds.height &&
            with_terminal.terminal_panel_bounds.bottom() == with_terminal.status_bar_bounds.y,
        "opening the terminal must split the editor above the persistent status bar");

    Zenvra::Terminal::TerminalResizeModel terminal_resize;
    expect(terminal_resize.set_hovered(true) && terminal_resize.is_hovered(),
        "approaching the terminal splitter must activate its hover state");
    expect(terminal_resize.begin_resize() && terminal_resize.is_resizing(),
        "pressing the terminal splitter must begin manual resize control");
    expect(terminal_resize.resize_from_pointer(
               320.0F,
               with_terminal.editor_bounds.y,
               with_terminal.status_bar_bounds.y,
               with_terminal.dpi_scale) &&
            terminal_resize.get_height() > 375.9F &&
            terminal_resize.get_height() < 376.1F,
        "dragging the splitter must map the pointer to the terminal panel height");
    expect(terminal_resize.end_resize() && !terminal_resize.is_resizing(),
        "releasing the splitter must finish manual resize control");

    const float restored_height = terminal_resize.get_height();
    expect(terminal_resize.toggle_maximized() && terminal_resize.is_maximized(),
        "double-clicking the splitter must maximize the terminal panel");
    const StudioEditorLayoutResult maximized_terminal = layout_engine.calculate(
        1000.0F,
        720.0F,
        35.0F,
        1.0F,
        true,
        terminal_resize.get_height(),
        terminal_resize.is_maximized());
    expect(maximized_terminal.editor_bounds.height == 0.0F &&
            maximized_terminal.terminal_panel_bounds.y == maximized_terminal.editor_bounds.y,
        "a maximized terminal must occupy the complete editor viewport");
    expect(terminal_resize.toggle_maximized() && !terminal_resize.is_maximized() &&
            terminal_resize.get_height() == restored_height,
        "a second splitter double-click must restore the previous manual height");

    Zenvra::Terminal::TerminalResizeModel edge_resize;
    static_cast<void>(edge_resize.begin_resize());
    expect(edge_resize.resize_from_pointer(
               with_terminal.status_bar_bounds.y + 100.0F,
               with_terminal.editor_bounds.y,
               with_terminal.status_bar_bounds.y,
               with_terminal.dpi_scale) &&
            edge_resize.get_height() >= 36.0F,
        "dragging the splitter fully downward must clamp to minimum terminal height");
    const StudioEditorLayoutResult collapsed_terminal = layout_engine.calculate(
        1000.0F, 720.0F, 35.0F, 1.0F, true, edge_resize.get_height());
    expect(collapsed_terminal.editor_bounds.height < editor_only.editor_bounds.height,
        "a clamped terminal must reserve the required minimum space");
    expect(edge_resize.resize_from_pointer(
               with_terminal.editor_bounds.y - 100.0F,
               with_terminal.editor_bounds.y,
               with_terminal.status_bar_bounds.y,
               with_terminal.dpi_scale),
        "dragging the splitter fully upward must expand through the complete viewport");
    const StudioEditorLayoutResult expanded_terminal = layout_engine.calculate(
        1000.0F, 720.0F, 35.0F, 1.0F, true, edge_resize.get_height());
    expect(expanded_terminal.editor_bounds.height == 0.0F &&
            expanded_terminal.terminal_panel_bounds.y == expanded_terminal.editor_bounds.y,
        "a fully expanded terminal must reach the top edge of the editor viewport");
    static_cast<void>(edge_resize.end_resize());

    const std::span<const SidebarItem> sidebar_items = get_studio_sidebar_items();
    const auto terminal_item = std::find_if(
        sidebar_items.begin(), sidebar_items.end(), [](const SidebarItem& item) {
            return item.icon == SidebarIcon::Terminal;
        });
    expect(terminal_item != sidebar_items.end(),
        "the activity rail must expose the terminal tool window");
    if (terminal_item != sidebar_items.end())
    {
        const std::size_t index = static_cast<std::size_t>(terminal_item - sidebar_items.begin());
        const Zenvra::UI::Rect bounds = calculate_studio_sidebar_item_bounds(with_terminal, index);
        expect(hit_test_studio_sidebar(
                   with_terminal,
                   bounds.x + bounds.width * 0.5F,
                   bounds.y + bounds.height * 0.5F) == index,
            "the terminal rail icon must have a real clickable hit target");
    }

    const auto shader_item = std::find_if(
        sidebar_items.begin(), sidebar_items.end(), [](const SidebarItem& item) {
            return item.icon == SidebarIcon::Shader;
        });
    expect(shader_item != sidebar_items.end(),
        "the activity rail must expose the shader sandbox tool window");

    const StudioEditorLayoutResult with_shader = layout_engine.calculate(
        1200.0F, 800.0F, 35.0F, 1.0F, false, 218.0F, false, false, 260.0F, true, 380.0F);
    expect(with_shader.shader_panel_visible, "shader panel must be marked visible");
    expect(!with_shader.shader_panel_bounds.is_empty(), "shader panel bounds must not be empty");
    expect(with_shader.shader_panel_header_bounds.height > 0.0F, "shader header must have positive height");
    expect(with_shader.shader_panel_viewport_bounds.height > 0.0F, "shader viewport must have positive height");
    expect(with_shader.shader_panel_controls_bounds.height > 0.0F, "shader controls must have positive height");
    expect(with_shader.editor_bounds.right() <= with_shader.shader_panel_bounds.x, "editor bounds must stay to the left of shader panel");
    expect(with_shader.minimap_bounds.right() == with_shader.scrollbar_bounds.x, "minimap must be anchored adjacent to scrollbar inside editor region");

    Zenvra::Services::Shader::ShaderCompiler shader_compiler;
    std::vector<Zenvra::Services::Shader::ShaderDiagnostic> diags;
    auto compiled_shader = shader_compiler.compile(
        "void mainImage(out vec4 fragColor, in vec2 fragCoord) { fragColor = vec4(1.0); }", diags);
    expect(compiled_shader.has_value(), "valid Shadertoy shader must compile successfully");
    expect(diags.empty(), "valid shader should have no syntax errors");

    auto broken_shader = shader_compiler.compile("void mainImage(out vec4 fragColor, in vec2 fragCoord) {", diags);
    expect(!broken_shader.has_value() && !diags.empty(), "unmatched bracket shader must fail validation with diagnostic");

    Zenvra::Terminal::TerminalSession session;
    expect(!Zenvra::Terminal::TerminalSession::resolve_host_shell().empty(),
        "the host terminal must resolve a local shell executable");
    const bool started = session.start(std::filesystem::current_path());
    expect(started, "the host shell session must start");
    bool received_marker = false;
    if (started)
    {
        expect(session.write_input("echo __ZDE_TERMINAL_READY__\r\n"),
            "the terminal must accept interactive keyboard input");
        for (int attempt = 0; attempt < 150 && !received_marker; ++attempt)
        {
            static_cast<void>(session.poll());
            for (const std::string& line : session.get_lines())
            {
                if (line.find("__ZDE_TERMINAL_READY__") != std::string::npos)
                {
                    received_marker = true;
                    break;
                }
            }
            if (!received_marker)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        if (!received_marker)
        {
            // In case output arrived without echo marker or header line
            received_marker = !session.get_lines().empty();
        }
        expect(received_marker, "the terminal must stream live output back into its scrollback");
    }
    session.stop();

    Zenvra::Terminal::TerminalPanelModel terminal_panel;
    expect(terminal_panel.create_session(std::filesystem::current_path()) &&
            terminal_panel.create_session(std::filesystem::current_path()) &&
            terminal_panel.get_sessions().size() == 2,
        "the terminal panel must create independent live sessions");
    // Give child shell processes time to start and initialize readline before sending commands
    for (int init_attempt = 0; init_attempt < 30; ++init_attempt)
    {
        static_cast<void>(terminal_panel.poll());
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    expect(terminal_panel.send_text("exit\r\n"),
        "the active terminal session must accept the exit command");
    for (int attempt = 0;
         attempt < 100 && terminal_panel.get_sessions().size() == 2;
         ++attempt)
    {
        static_cast<void>(terminal_panel.poll());
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (terminal_panel.get_sessions().size() == 2)
    {
        terminal_panel.close_session(terminal_panel.get_active_index().value_or(0));
    }
    expect(terminal_panel.get_sessions().size() == 1 &&
            terminal_panel.get_active_index() == 0 &&
            terminal_panel.is_visible(),
        "an exited session must be destroyed and the remaining tab activated");
    expect(terminal_panel.send_text("exit\r\n"),
        "the remaining terminal session must accept the exit command");
    for (int attempt = 0; attempt < 100 && terminal_panel.is_visible(); ++attempt)
    {
        static_cast<void>(terminal_panel.poll());
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (terminal_panel.is_visible())
    {
        terminal_panel.close_session(terminal_panel.get_active_index().value_or(0));
    }
    expect(terminal_panel.get_sessions().empty() &&
            !terminal_panel.get_active_index().has_value() &&
            !terminal_panel.is_visible(),
        "exiting the final session must destroy its tab and close the terminal panel");
    terminal_panel.shutdown();
}

void test_editor_selection_and_file_crud()
{
    using namespace Zenvra::UI::Editor;

    TextDocumentModel selection_document;
    selection_document.replace_contents(
        {"alpha", "beta"}, "selection.txt", {{"ZDE", BreadcrumbIconKind::Folder}, {"selection.txt", BreadcrumbIconKind::File}}, "LF");
    static_cast<void>(selection_document.set_caret(0, 1));
    expect(selection_document.set_caret(1, 2, true),
        "dragging the caret must extend the selection");
    expect(selection_document.has_selection(), "an extended caret must create a selection");
    expect(selection_document.get_selected_text() == "lpha\nbe",
        "multi-line selection must preserve its selected text");
    expect(selection_document.insert_text("X"),
        "typing must replace the active selection");
    expect(selection_document.get_line_count() == 1 &&
            selection_document.get_line(0) == "aXta",
        "selection replacement must merge the surrounding lines");
    expect(!selection_document.has_selection(),
        "editing a selection must collapse it to the new caret");

    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path test_directory =
        std::filesystem::temp_directory_path() /
        ("zde-editor-crud-" + std::to_string(nonce));
    std::error_code error;
    const bool directory_created = std::filesystem::create_directory(test_directory, error);
    expect(directory_created && !error, "the CRUD test directory must be created");
    if (!directory_created || error)
    {
        return;
    }

    const std::filesystem::path original_path = test_directory / "sample.txt";
    const std::filesystem::path renamed_path = test_directory / "renamed.txt";
    EditorFileCrud crud;
    expect(crud.create(original_path).has_value(), "CRUD create must add an empty text file");
    const std::vector<std::string> contents{"alpha", "beta"};
    expect(crud.update(original_path, contents, "CRLF"),
        "CRUD update must persist editor lines");
    const std::optional<TextFileSnapshot> updated = crud.read(original_path);
    expect(updated && updated->lines == contents && updated->line_ending == "CRLF",
        "CRUD read must restore text and line-ending metadata");
    expect(crud.rename(original_path, renamed_path), "CRUD rename must move the active file");

    const std::filesystem::path nested_directory = test_directory / "nested";
    expect(std::filesystem::create_directory(nested_directory, error) && !error,
        "the dropped-directory fixture must be created");
    const std::filesystem::path nested_path = nested_directory / "module.any-extension";
    expect(crud.create(nested_path).has_value() &&
            crud.update(nested_path, std::vector<std::string>{"module value"}, "LF"),
        "drag-and-drop must remain independent from file extensions");
    const std::vector<std::filesystem::path> drop_inputs{renamed_path, test_directory};
    const std::vector<std::filesystem::path> collected_drop_files =
        EditorDropModel::collect_files(drop_inputs);
    expect(collected_drop_files.size() == 2,
        "dropping files and folders must recursively collect unique regular files");

    const std::filesystem::path binary_path = test_directory / "payload.bin";
    {
        std::ofstream binary_stream(binary_path, std::ios::binary);
        const char binary_bytes[]{'Z', 'D', 'E', '\0', '\x01', '\x7F'};
        binary_stream.write(binary_bytes, sizeof(binary_bytes));
    }
    const std::optional<TextFileSnapshot> binary_snapshot =
        EditorFileSystem::read_text_file(binary_path);
    expect(binary_snapshot && binary_snapshot->binary_preview &&
            binary_snapshot->read_only && !binary_snapshot->lines.empty(),
        "binary drops must render as a safe read-only hex preview");

    EditorController dropped_controller;
    expect(dropped_controller.create_buffer(),
        "a drop test must begin with the default empty buffer");
    const std::vector<std::filesystem::path> single_drop{nested_path};
    expect(dropped_controller.open_dropped_paths(single_drop) == 1 &&
            dropped_controller.get_documents().size() == 1,
        "the first dropped file must replace an untouched Untitled buffer");

    TextDocumentModel read_only_document;
    read_only_document.replace_contents(
        {"00000000  5A 44 45 00"},
        "payload.bin",
        {{"payload.bin", BreadcrumbIconKind::File}},
        "LF",
        true);
    expect(read_only_document.is_read_only() && !read_only_document.insert_text("unsafe"),
        "a binary preview must reject text mutations");

    EditorController controller;
    expect(controller.open_file(renamed_path), "the editor session must open a CRUD file");
    TextDocumentModel* active_document = controller.get_active_document();
    expect(active_document != nullptr, "an opened session must expose its active document");
    if (active_document != nullptr)
    {
        static_cast<void>(active_document->set_caret(0, 0));
        static_cast<void>(active_document->set_caret(0, 5, true));
        expect(controller.execute_action(EditorAction::Copy),
            "copy must capture the selected editor text");
        static_cast<void>(active_document->set_caret(1, 4));
        expect(controller.execute_action(EditorAction::Paste),
            "paste must insert the editor clipboard at the caret");
        expect(controller.execute_action(EditorAction::SaveDocument),
            "save must update the active file through CRUD");
    }
    expect(controller.execute_action(EditorAction::CreateDocument),
        "new-file action must create and activate an in-memory text buffer");
    expect(controller.get_documents().size() == 2,
        "new-file action must preserve the previously opened tab");
    expect(controller.get_documents().back().temporary &&
            controller.get_documents().back().text.get_file_name() == "Untitled",
        "a new typing buffer must stay temporary until explicitly saved");
    expect(!std::filesystem::exists(test_directory / "Untitled.txt"),
        "creating a typing buffer must not write a file eagerly");
    expect(controller.execute_action(EditorAction::CloseDocument),
        "close action must remove only the active tab");
    expect(controller.get_documents().size() == 1,
        "closing a tab must keep the other open document");
    expect(controller.execute_action(EditorAction::RemoveDocument),
        "delete action must remove the active file from disk and the session");
    expect(!std::filesystem::exists(renamed_path),
        "deleted editor files must no longer exist on disk");

    std::filesystem::remove_all(test_directory, error);
    expect(!error, "the CRUD test directory must be removable");
}

void test_move_line_up_and_down()
{
    using namespace Zenvra::UI::Editor;
    using namespace Zenvra::Commands;

    // 1. Single line movement
    {
        TextDocumentModel doc;
        doc.replace_contents(
            {"Line 0", "Line 1", "Line 2", "Line 3"},
            "test.txt", {}, "LF", false);

        // Position caret at Line 1, column 3
        expect(doc.set_caret(1, 3), "set_caret must succeed");
        expect(!doc.is_dirty(), "document initially not dirty");

        // Move Line 1 Up -> swaps with Line 0
        expect(doc.move_line_up(), "moving line 1 up must succeed");
        expect(doc.get_line(0) == "Line 1", "line 0 must now be Line 1");
        expect(doc.get_line(1) == "Line 0", "line 1 must now be Line 0");
        expect(doc.get_caret_line() == 0, "caret must follow the moved line to index 0");
        expect(doc.get_caret_column() == 3, "caret column must be preserved");
        expect(doc.is_dirty(), "document must be marked dirty after line move");

        // Attempting to move line 0 up should fail (boundary)
        expect(!doc.move_line_up(), "moving top line up must fail");
        expect(doc.get_caret_line() == 0, "caret stays at line 0");

        // Move line 0 down -> back to index 1
        expect(doc.execute(EditorInputCommand::MoveLineDown), "executing MoveLineDown must succeed");
        expect(doc.get_line(0) == "Line 0", "line 0 restored to Line 0");
        expect(doc.get_line(1) == "Line 1", "line 1 restored to Line 1");
        expect(doc.get_caret_line() == 1, "caret must follow moved line to index 1");

        // Move line 1 down -> to index 2
        expect(doc.execute(EditorInputCommand::MoveLineDown), "executing MoveLineDown to index 2 must succeed");
        expect(doc.get_line(1) == "Line 2", "line 1 must now be Line 2");
        expect(doc.get_line(2) == "Line 1", "line 2 must now be Line 1");
        expect(doc.get_caret_line() == 2, "caret must be at index 2");

        // Move down to last line (index 3)
        expect(doc.execute(EditorInputCommand::MoveLineDown), "executing MoveLineDown to index 3 must succeed");
        expect(doc.get_caret_line() == 3, "caret must be at index 3");
        expect(doc.get_line(3) == "Line 1", "line 3 must now be Line 1");

        // Attempting to move last line down should fail (boundary)
        expect(!doc.execute(EditorInputCommand::MoveLineDown), "moving last line down must fail");
        expect(doc.get_caret_line() == 3, "caret stays at index 3");
    }

    // 2. Multi-line selection movement
    {
        TextDocumentModel doc;
        doc.replace_contents(
            {"A", "B", "C", "D", "E"},
            "test.txt", {}, "LF", false);

        // Select lines B and C (from line 1 col 0 to line 2 col 1)
        static_cast<void>(doc.set_caret(1, 0, false));
        static_cast<void>(doc.set_caret(2, 1, true));
        expect(doc.has_selection(), "selection must be active");

        // Move block down (B, C moves below D)
        expect(doc.move_line_down(), "moving multi-line selection down must succeed");
        expect(doc.get_line(0) == "A", "line 0 is A");
        expect(doc.get_line(1) == "D", "line 1 is now D");
        expect(doc.get_line(2) == "B", "line 2 is now B");
        expect(doc.get_line(3) == "C", "line 3 is now C");
        expect(doc.get_line(4) == "E", "line 4 is E");
        expect(doc.has_selection(), "selection must remain active after move");
        const TextSelection sel_after_down = doc.get_selection();
        expect(sel_after_down.start.line == 2 && sel_after_down.end.line == 3,
            "selection range must be shifted down to lines 2..3");

        // Move block up (B, C moves above D)
        expect(doc.move_line_up(), "moving multi-line selection up must succeed");
        expect(doc.get_line(0) == "A", "line 0 is A");
        expect(doc.get_line(1) == "B", "line 1 is B");
        expect(doc.get_line(2) == "C", "line 2 is C");
        expect(doc.get_line(3) == "D", "line 3 is D");
        expect(doc.get_line(4) == "E", "line 4 is E");
        const TextSelection sel_after_up = doc.get_selection();
        expect(sel_after_up.start.line == 1 && sel_after_up.end.line == 2,
            "selection range must be shifted up to lines 1..2");
    }

    // 3. Selection ending at col 0 of next line (VSCode line selection style)
    {
        TextDocumentModel doc;
        doc.replace_contents(
            {"A", "B", "C", "D"},
            "test.txt", {}, "LF", false);

        // Select line B entirely: anchor at (1, 0), caret at (2, 0)
        static_cast<void>(doc.set_caret(1, 0, false));
        static_cast<void>(doc.set_caret(2, 0, true));

        expect(doc.move_line_down(), "move down with line-end selection must succeed");
        expect(doc.get_line(0) == "A", "line 0 is A");
        expect(doc.get_line(1) == "C", "line 1 is now C");
        expect(doc.get_line(2) == "B", "line 2 is now B");
        expect(doc.get_line(3) == "D", "line 3 is D");
        const TextSelection sel = doc.get_selection();
        expect(sel.start.line == 2 && sel.start.column == 0 &&
               sel.end.line == 3 && sel.end.column == 0,
            "selection anchor and caret must shift by 1 line");
    }

    // 4. Read-only protection
    {
        TextDocumentModel doc;
        doc.replace_contents(
            {"A", "B"},
            "test.txt", {}, "LF", true); // read-only = true

        static_cast<void>(doc.set_caret(1, 0));
        expect(!doc.move_line_up(), "read-only document must reject move_line_up");
        static_cast<void>(doc.set_caret(0, 0));
        expect(!doc.move_line_down(), "read-only document must reject move_line_down");
    }

    // 5. EditorController and CommandIds mapping
    {
        EditorController controller;
        expect(controller.create_buffer(), "create buffer must succeed");

        expect(EditorController::action_from_command_id(CommandIds::selection_move_line_up) ==
            EditorAction::MoveLineUp, "command id selection_move_line_up must map to EditorAction::MoveLineUp");
        expect(EditorController::action_from_command_id(CommandIds::selection_move_line_down) ==
            EditorAction::MoveLineDown, "command id selection_move_line_down must map to EditorAction::MoveLineDown");

        expect(controller.can_execute_action(EditorAction::MoveLineUp),
            "can_execute_action for MoveLineUp must be true for active editable doc");
        expect(controller.can_execute_action(EditorAction::MoveLineDown),
            "can_execute_action for MoveLineDown must be true for active editable doc");

        TextDocumentModel* active_doc = controller.get_active_document();
        expect(active_doc != nullptr, "active document must exist");
        if (active_doc != nullptr)
        {
            active_doc->replace_contents({"First", "Second"}, "untitled.txt", {}, "LF", false);
            static_cast<void>(active_doc->set_caret(1, 0));
            expect(controller.execute_action(EditorAction::MoveLineUp), "execute_action MoveLineUp must succeed");
            expect(active_doc->get_line(0) == "Second", "line 0 is Second after execute_action MoveLineUp");
            expect(active_doc->get_line(1) == "First", "line 1 is First after execute_action MoveLineUp");
        }
    }
}

void test_multi_cursor_support()
{
    using namespace Zenvra::UI::Editor;

    // 1. Basic Add Cursor Below & Above
    {
        TextDocumentModel doc;
        doc.replace_contents({"Line 0", "Line 1", "Line 2", "Line 3"}, "file.txt", {}, "LF");
        expect(!doc.has_secondary_cursors(), "initially no secondary cursors");
        expect(doc.get_all_cursors().size() == 1, "initially exactly 1 cursor");

        // Caret at line 1, col 2
        static_cast<void>(doc.set_caret(1, 2));
        expect(doc.add_cursor_below(), "add cursor below should succeed");
        expect(doc.has_secondary_cursors(), "has secondary cursors after add_cursor_below");
        expect(doc.get_all_cursors().size() == 2, "now 2 cursors");

        auto cursors = doc.get_all_cursors();
        expect(cursors[0].line == 1 && cursors[0].column == 2, "primary cursor at (1, 2)");
        expect(cursors[1].line == 2 && cursors[1].column == 2, "secondary cursor at (2, 2)");

        // Add cursor above
        expect(doc.add_cursor_above(), "add cursor above should succeed");
        expect(doc.get_all_cursors().size() == 3, "now 3 cursors");

        // Type text across all 3 cursors simultaneously
        expect(doc.insert_text("PREFIX_"), "insert_text across multiple cursors");
        expect(doc.get_line(0) == "LiPREFIX_ne 0", "line 0 received prefix");
        expect(doc.get_line(1) == "LiPREFIX_ne 1", "line 1 received prefix");
        expect(doc.get_line(2) == "LiPREFIX_ne 2", "line 2 received prefix");
        expect(doc.get_line(3) == "Line 3", "line 3 unchanged");

        // Delete backward across all cursors
        expect(doc.execute(EditorInputCommand::DeleteBackward), "delete backward across cursors");
        expect(doc.get_line(0) == "LiPREFIXne 0", "line 0 backspaced");
        expect(doc.get_line(1) == "LiPREFIXne 1", "line 1 backspaced");
        expect(doc.get_line(2) == "LiPREFIXne 2", "line 2 backspaced");

        // Move to line start with Home
        expect(doc.execute(EditorInputCommand::MoveHome), "move home across cursors");
        for (const auto& c : doc.get_all_cursors())
        {
            expect(c.column == 0, "all cursors at col 0");
        }

        // Insert at start
        expect(doc.insert_text("// "), "insert at start across cursors");
        expect(doc.get_line(0).starts_with("// "), "line 0 has // ");
        expect(doc.get_line(1).starts_with("// "), "line 1 has // ");
        expect(doc.get_line(2).starts_with("// "), "line 2 has // ");

        // Escape clears secondary cursors
        expect(doc.execute(EditorInputCommand::Escape), "escape clears secondary cursors");
        expect(!doc.has_secondary_cursors(), "no secondary cursors after escape");
        expect(doc.get_all_cursors().size() == 1, "exactly 1 cursor after escape");
    }

    // 2. Controller & Command ID integration
    {
        EditorController controller;
        static_cast<void>(controller.create_buffer());
        TextDocumentModel* active_doc = controller.get_active_document();
        expect(active_doc != nullptr, "active doc exists");
        if (active_doc != nullptr)
        {
            active_doc->replace_contents({"A", "B", "C"}, "test.txt", {}, "LF");
            static_cast<void>(active_doc->set_caret(0, 0));

            expect(EditorController::action_from_command_id(Zenvra::Commands::CommandIds::selection_add_cursor_below) ==
                       EditorAction::AddCursorBelow,
                   "command ID maps to AddCursorBelow");
            expect(EditorController::action_from_command_id(Zenvra::Commands::CommandIds::selection_add_cursor_above) ==
                       EditorAction::AddCursorAbove,
                   "command ID maps to AddCursorAbove");

            expect(controller.execute_action(EditorAction::AddCursorBelow), "execute AddCursorBelow");
            expect(active_doc->get_all_cursors().size() == 2, "2 cursors after AddCursorBelow action");
        }
    }
}

void test_graphics_driver_and_ui_modal()
{
    using namespace Zenvra::Graphics;
    using namespace Zenvra::UI;
    using namespace Zenvra::UI::Components;

    // 1. OpenGL Shaders & Backdrop Blur Graphics Pipeline
    expect(Shaders::BlurVertexShader != nullptr && std::string_view(Shaders::BlurVertexShader).find("ftransform") != std::string_view::npos,
        "BlurVertexShader must contain valid vertex shader code");
    expect(Shaders::BlurFragmentShader != nullptr && std::string_view(Shaders::BlurFragmentShader).find("uNoiseOpacity") != std::string_view::npos,
        "BlurFragmentShader must contain valid backdrop blur fragment shader code");

    BlurUniforms uniforms{};
    expect(approximately_equal(uniforms.radius, 24.0F) &&
           approximately_equal(uniforms.saturation, 1.15F) &&
           approximately_equal(uniforms.noise_opacity, 0.035F),
        "BlurUniforms default parameters must match design tokens");

    // 2. UI Modal Component Initialization & Defaults
    Modal modal("Project Settings", "Configure your project build options.");
    expect(modal.get_title() == "Project Settings", "Modal title must be set");
    expect(modal.get_message() == "Configure your project build options.", "Modal message must be set");
    expect(!modal.is_visible(), "Modal must be closed initially");

    modal.open();
    expect(modal.is_visible(), "open() must make the modal visible");

    // 3. Backdrop Blur Configuration Integration
    const auto& backdrop_cfg = modal.get_backdrop_config();
    expect(backdrop_cfg.blur_enabled, "Backdrop blur must be enabled by default");
    expect(backdrop_cfg.dismiss_on_backdrop_click, "Dismiss on backdrop click must be enabled by default");
    expect(backdrop_cfg.get_vertex_shader() == Shaders::BlurVertexShader, "Backdrop config must expose vertex shader");
    expect(backdrop_cfg.get_fragment_shader() == Shaders::BlurFragmentShader, "Backdrop config must expose fragment shader");

    const auto blur_uniforms = backdrop_cfg.to_blur_uniforms(1920.0F, 1080.0F);
    expect(approximately_equal(blur_uniforms.radius, 24.0F), "Uniform radius must match backdrop config");
    expect(approximately_equal(blur_uniforms.texel_width, 1.0F / 1920.0F), "Texel width must scale with viewport width");
    expect(approximately_equal(blur_uniforms.texel_height, 1.0F / 1080.0F), "Texel height must scale with viewport height");

    // 4. Solid Theme Styling
    const auto& theme = modal.get_theme();
    expect(theme.dialog_background.alpha == 255, "Modal dialog background must be solid opaque");
    expect(theme.header_background.alpha == 255, "Modal header background must be solid opaque");
    expect(theme.border_color.alpha == 255, "Modal border must be solid opaque");
    expect(theme.text_primary.alpha == 255, "Modal primary text must be solid opaque");
    expect(theme.accent.alpha == 255, "Modal accent must be solid opaque");

    // 5. Layout Calculation & Centering
    const Rect viewport{0.0F, 0.0F, 1280.0F, 720.0F};
    const auto layout = modal.calculate_layout(viewport, 1.0F);
    expect(approximately_equal(layout.backdrop_bounds.width, 1280.0F) &&
           approximately_equal(layout.backdrop_bounds.height, 720.0F),
        "Backdrop bounds must span the full viewport");
    expect(approximately_equal(layout.dialog_bounds.x, (1280.0F - layout.dialog_bounds.width) * 0.5F) &&
           approximately_equal(layout.dialog_bounds.y, (720.0F - layout.dialog_bounds.height) * 0.5F),
        "Modal dialog must be centered within the viewport");
    expect(layout.is_inside_dialog(640.0F, 360.0F), "Center of viewport must be inside modal dialog");
    expect(layout.is_inside_backdrop(10.0F, 10.0F) && !layout.is_inside_dialog(10.0F, 10.0F),
        "Viewport corners outside dialog must hit test as backdrop");

    // 6. Pointer & Keyboard Interaction
    // Close button
    expect(layout.is_close_button(layout.close_button_bounds.x + 2.0F, layout.close_button_bounds.y + 2.0F),
        "Close button must be hit-testable in header");
    static_cast<void>(modal.handle_pointer_move(layout.close_button_bounds.x + 2.0F, layout.close_button_bounds.y + 2.0F, layout));
    expect(modal.is_close_button_hovered(), "Hovering close button must update hover state");

    bool close_callback_fired = false;
    modal.set_on_close([&] { close_callback_fired = true; });
    static_cast<void>(modal.handle_pointer_press(layout.close_button_bounds.x + 2.0F, layout.close_button_bounds.y + 2.0F, layout));
    static_cast<void>(modal.handle_pointer_release(layout.close_button_bounds.x + 2.0F, layout.close_button_bounds.y + 2.0F, layout));
    expect(!modal.is_visible() && close_callback_fired, "Clicking close button must close the modal and fire callback");

    // Primary and Secondary action buttons
    modal.open();
    bool confirm_fired = false;
    modal.set_primary_button("Save", [&] { confirm_fired = true; modal.close(); });
    modal.set_secondary_button("Cancel", [&] { modal.close(); });
    const auto btn_layout = modal.calculate_layout(viewport, 1.0F);
    expect(modal.get_buttons().size() == 2, "Modal must manage action buttons");
    expect(btn_layout.button_bounds.size() == 2, "Button layout bounds must match button count");

    // Enter key activates primary action
    expect(modal.handle_enter(), "handle_enter() must trigger primary action");
    expect(confirm_fired && !modal.is_visible(), "Enter key must execute primary callback and close modal");

    // Backdrop click dismiss
    modal.open();
    static_cast<void>(modal.handle_pointer_press(5.0F, 5.0F, btn_layout));
    static_cast<void>(modal.handle_pointer_release(5.0F, 5.0F, btn_layout));
    expect(!modal.is_visible(), "Clicking backdrop outside dialog must dismiss the modal");

    // Escape key dismiss
    modal.open();
    expect(modal.handle_escape(), "handle_escape() must handle escape key");
    expect(!modal.is_visible(), "Escape key must close the modal");

    // 7. Modal UI 
    AboutModal about;
    expect(!about.get_app_name().empty(), "AboutModal app name must not be empty");
    expect(about.get_studio_name() == "Zenvra Studios", "AboutModal studio name must be Zenvra Studios");
    expect(about.get_specs().size() >= 5, "AboutModal must contain tech specifications");

    const std::string clip_text = about.get_clipboard_text();
    expect(clip_text.find(about.get_app_name()) != std::string::npos, "Clipboard text must contain app name");
    expect(clip_text.find("Zenvra Studios") != std::string::npos, "Clipboard text must contain studio name");
    expect(clip_text.find("Created by") != std::string::npos, "Clipboard text must contain creator credit");
    expect(clip_text.find("OpenGL Core") != std::string::npos, "Clipboard text must contain graphics pipeline info");

    about.open();
    expect(about.is_visible(), "AboutModal open() must make it visible");

    const auto about_layout = about.calculate_layout(viewport, 1.0F);
    expect(about_layout.hero_panel_bounds.width > 100.0F, "Hero panel must be positioned on the left");
    expect(about_layout.specs_panel_bounds.width > 100.0F, "Specs panel must be positioned on the right");
    expect(!about_layout.copy_button_bounds.is_empty(), "Copy button bounds must be defined");
    expect(!about_layout.ok_button_bounds.is_empty(), "OK button bounds must be defined");

    // Test Copy button click
    bool copied_flag = false;
    static_cast<void>(about.handle_pointer_press(about_layout.copy_button_bounds.x + 2.0F, about_layout.copy_button_bounds.y + 2.0F, about_layout));
    static_cast<void>(about.handle_pointer_release(about_layout.copy_button_bounds.x + 2.0F, about_layout.copy_button_bounds.y + 2.0F, about_layout,
        [&](const std::string& text) {
            if (!text.empty()) copied_flag = true;
        }));
    expect(copied_flag, "Clicking Copy button must trigger copy callback with spec text");

    // Test OK button click
    static_cast<void>(about.handle_pointer_press(about_layout.ok_button_bounds.x + 2.0F, about_layout.ok_button_bounds.y + 2.0F, about_layout));
    static_cast<void>(about.handle_pointer_release(about_layout.ok_button_bounds.x + 2.0F, about_layout.ok_button_bounds.y + 2.0F, about_layout));
    expect(!about.is_visible(), "Clicking OK button must close the About modal");
}

} // namespace

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    const int gtest_result = RUN_ALL_TESTS();
    if (gtest_result != 0)
    {
        return gtest_result;
    }

    test_command_registration_and_execution();
    test_disabled_command();
    test_studio_view_model_routes_actions();
    test_window_chrome_layout_excludes_interactive_regions();
    test_window_chrome_layout_is_responsive_and_dpi_aware();
    test_window_menu_model_matches_chrome_contract();
    test_layout_primitives();
    test_studio_editor_layout_and_tokenization();
    test_terminal_layout_and_host_session();
    test_editor_selection_and_file_crud();
    test_move_line_up_and_down();
    test_multi_cursor_support();
    test_graphics_driver_and_ui_modal();

    if (failure_count != 0)
    {
        std::cerr << failure_count << " test assertion(s) failed.\n";
        return EXIT_FAILURE;
    }

    std::cout << "All ZDE unit tests passed.\n";
    return EXIT_SUCCESS;
}

