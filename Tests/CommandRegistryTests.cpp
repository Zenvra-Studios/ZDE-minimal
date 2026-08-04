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
    });

    expect(view_model.initialize(), "the Studio ViewModel must initialize its command model");
    expect(view_model.initialize(), "initialization must be idempotent");
    expect(view_model.get_command_registry().size() == 23, "the initial command contract must contain 23 commands");
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
    expect(layout.tab_bar_bounds.y == 5.0F && layout.tab_bar_bounds.bottom() == 35.0F,
        "editor tabs must be integrated into the custom titlebar");
    expect(layout.editor_bounds.y == 35.0F, "the editor must begin directly below the titlebar");
    expect(layout.status_bar_bounds.y == 616.0F, "the status bar must remain pinned to the bottom");
    expect(layout.activity_bar_bounds.width == 38.0F,
        "the activity rail must use the medium workspace scale");
    expect(layout.editor_bounds.x == 90.0F,
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
            with_tool_sidebar.tab_bar_bounds.x == 298.0F &&
            with_tool_sidebar.editor_bounds.x == 350.0F,
        "an open activity panel must reserve a dedicated sidebar beside the activity rail");
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
            scaled.tab_bar_bounds.height == 60.0F &&
            scaled.status_bar_bounds.height == 48.0F &&
            scaled.editor_bounds.x == 700.0F,
        "the shared editor layout must scale all platform-independent design metrics with DPI");

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
                "every non-terminal activity icon must open its matching tool sidebar");
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
    const std::size_t token_count = tokenize_editor_line("return true;", tokens);
    expect(token_count >= 4, "the editor lexer must preserve whitespace and syntax tokens");
    expect(tokens[0].kind == EditorTokenKind::Keyword, "return must use the keyword presentation");
    expect(tokens[token_count - 1].kind == EditorTokenKind::Plain, "C++ semicolons must not become comments");
    expect(supports_editor_syntax_highlighting("main.cpp") &&
            supports_editor_syntax_highlighting("BackdropBlur.h") &&
            !supports_editor_syntax_highlighting("notes.txt") &&
            !supports_editor_syntax_highlighting("CMakeLists.txt"),
        "syntax highlighting must target source files while plain text and CMake stay plain");
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
            scroll.get_first_visible_line() == 90,
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
    document.replace_contents({"alpha", "beta"}, "sample.cpp", {"ZDE", "sample.cpp"}, "LF");
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
        expect(snapshot->breadcrumbs.back() == "X11Window.cpp",
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
            edge_resize.get_height() == 0.0F,
        "dragging the splitter fully downward must collapse the terminal to the status bar");
    const StudioEditorLayoutResult collapsed_terminal = layout_engine.calculate(
        1000.0F, 720.0F, 35.0F, 1.0F, true, edge_resize.get_height());
    expect(collapsed_terminal.editor_bounds.height == editor_only.editor_bounds.height &&
            collapsed_terminal.terminal_panel_bounds.y ==
                collapsed_terminal.status_bar_bounds.y,
        "a fully collapsed terminal must return the complete viewport to the editor");
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

    Zenvra::Terminal::TerminalSession session;
    expect(!Zenvra::Terminal::TerminalSession::resolve_host_shell().empty(),
        "the host terminal must resolve a local shell executable");
    const bool started = session.start(std::filesystem::current_path());
    expect(started, "the host shell session must start");
    bool received_marker = false;
    if (started)
    {
        expect(session.write_input("echo __ZDE_TERMINAL_READY__\r"),
            "the terminal must accept interactive keyboard input");
        for (int attempt = 0; attempt < 100 && !received_marker; ++attempt)
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
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
        expect(received_marker, "the terminal must stream live output back into its scrollback");
    }
    session.stop();

    Zenvra::Terminal::TerminalPanelModel terminal_panel;
    expect(terminal_panel.create_session(std::filesystem::current_path()) &&
            terminal_panel.create_session(std::filesystem::current_path()) &&
            terminal_panel.get_sessions().size() == 2,
        "the terminal panel must create independent live sessions");
    expect(terminal_panel.send_text("exit\r"),
        "the active terminal session must accept the exit command");
    for (int attempt = 0;
         attempt < 100 && terminal_panel.get_sessions().size() == 2;
         ++attempt)
    {
        static_cast<void>(terminal_panel.poll());
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    expect(terminal_panel.get_sessions().size() == 1 &&
            terminal_panel.get_active_index() == 0 &&
            terminal_panel.is_visible(),
        "an exited session must be destroyed and the remaining tab activated");
    expect(terminal_panel.send_text("exit\r"),
        "the remaining terminal session must accept the exit command");
    for (int attempt = 0; attempt < 100 && terminal_panel.is_visible(); ++attempt)
    {
        static_cast<void>(terminal_panel.poll());
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
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
        {"alpha", "beta"}, "selection.txt", {"ZDE", "selection.txt"}, "LF");
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
        {"payload.bin"},
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

} // namespace

int main()
{
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

    if (failure_count != 0)
    {
        std::cerr << failure_count << " test assertion(s) failed.\n";
        return EXIT_FAILURE;
    }

    std::cout << "All ZDE unit tests passed.\n";
    return EXIT_SUCCESS;
}
