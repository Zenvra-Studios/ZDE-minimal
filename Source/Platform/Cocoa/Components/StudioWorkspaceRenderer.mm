#import <Cocoa/Cocoa.h>

#include "Platform/Cocoa/Components/StudioWorkspaceRenderer.h"
#include "Utility/Fonts.h"
#include "UI/Editor/EditorFileSystem.h"

#include <lunasvg.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace Zenvra::Platform::Cocoa::Components
{

namespace
{

int round_to_int(float value)
{
    return static_cast<int>(std::lround(value));
}

} // namespace

void StudioWorkspaceRenderer::color_to_rgba(const UI::Theme::Color& color, CGFloat* rgba)
{
    rgba[0] = static_cast<CGFloat>(color.red) / 255.0;
    rgba[1] = static_cast<CGFloat>(color.green) / 255.0;
    rgba[2] = static_cast<CGFloat>(color.blue) / 255.0;
    rgba[3] = static_cast<CGFloat>(color.alpha) / 255.0;
}

std::string StudioWorkspaceRenderer::color_to_hex(const UI::Theme::Color& color)
{
    char value[8]{};
    std::snprintf(
        value, sizeof(value), "#%02x%02x%02x",
        static_cast<unsigned int>(color.red),
        static_cast<unsigned int>(color.green),
        static_cast<unsigned int>(color.blue));
    return value;
}

StudioWorkspaceRenderer::StudioWorkspaceRenderer() = default;

StudioWorkspaceRenderer::~StudioWorkspaceRenderer()
{
    shutdown();
}

bool StudioWorkspaceRenderer::initialize(float dpi_scale)
{
    shutdown();
    m_dpi_scale = std::max(dpi_scale, 0.5F);

    // Resolve the directory that contains Assets/ (icons + fonts). Order:
    //   1. CWD project root  (dev run from a terminal at the repo root)
    //   2. App bundle Resources  (Assets copied there by CMake)
    //   3. App bundle root
    //   4. Walk up from the executable directory (dev non-bundle binaries)
    std::optional<std::filesystem::path> asset_root;
    const auto contains_assets = [](const std::filesystem::path& directory) {
        std::error_code error_code;
        return std::filesystem::is_directory(directory, error_code) &&
               std::filesystem::is_directory(directory / "Assets" / "icons", error_code);
    };

    std::error_code path_error;
    const std::filesystem::path current_path = std::filesystem::current_path(path_error);
    if (!path_error)
    {
        if (std::optional<std::filesystem::path> project_root =
                UI::Editor::EditorFileSystem::find_project_root(current_path);
            project_root && contains_assets(*project_root))
        {
            asset_root = *project_root;
        }
    }
    if (!asset_root)
    {
        NSBundle* main_bundle = [NSBundle mainBundle];
        NSString* resource_path = [main_bundle resourcePath];
        if (resource_path && contains_assets(std::filesystem::path([resource_path UTF8String])))
        {
            asset_root = std::filesystem::path([resource_path UTF8String]);
        }
        else
        {
            NSString* bundle_path = [main_bundle bundlePath];
            if (bundle_path && contains_assets(std::filesystem::path([bundle_path UTF8String])))
            {
                asset_root = std::filesystem::path([bundle_path UTF8String]);
            }
        }
    }
    if (!asset_root)
    {
        NSString* executable_path = [[NSBundle mainBundle] executablePath];
        if (executable_path)
        {
            std::filesystem::path directory = std::filesystem::path([executable_path UTF8String]).parent_path();
            for (int level = 0; level < 6 && !directory.empty(); ++level)
            {
                if (contains_assets(directory))
                {
                    asset_root = directory;
                    break;
                }
                const std::filesystem::path parent = directory.parent_path();
                if (parent == directory) break;
                directory = parent;
            }
        }
    }
    if (asset_root)
    {
        m_icon_asset_root = *asset_root / "Assets" / "icons";
    }

    // Load fonts using CoreText
    // Use the system UI font for the GUI; bundled fonts only for the editor
    std::string editor_font_family = "Menlo";
    std::string ui_font_family = ".AppleSystemUIFont";

    if (asset_root)
    {
        const std::filesystem::path hack_ttf =
            *asset_root / "Assets" / "fonts" / "Hack" / "ttf" / "Hack-Regular.ttf";
        std::error_code size_error;

        if (std::filesystem::exists(hack_ttf) &&
            std::filesystem::file_size(hack_ttf, size_error) > 100)
        {
            // Register font with CoreText
            CFURLRef font_url = CFURLCreateFromFileSystemRepresentation(
                nullptr, reinterpret_cast<const UInt8*>(hack_ttf.c_str()),
                static_cast<CFIndex>(hack_ttf.string().size()), false);
            if (font_url)
            {
                if (CTFontManagerRegisterFontsForURL(font_url, kCTFontManagerScopeProcess, nullptr))
                {
                    editor_font_family = "Hack";
                }
                CFRelease(font_url);
            }
        }
    }

    const float ui_size = std::max(12.0F * m_dpi_scale, 9.0F);
    const float small_size = std::max(12.0F * m_dpi_scale, 9.0F);
    const float editor_size = std::max(14.0F * m_dpi_scale, 10.0F);
    const float minimap_size = std::max(3.0F * m_dpi_scale, 3.0F);
    const float large_size = std::max(24.0F * m_dpi_scale, 18.0F);

    m_ui_font = std::make_unique<AntialiasedFont>(ui_font_family, ui_size);
    m_small_font = std::make_unique<AntialiasedFont>(ui_font_family, small_size);
    m_editor_font = std::make_unique<AntialiasedFont>(editor_font_family, editor_size);
    m_minimap_font = std::make_unique<AntialiasedFont>(editor_font_family, minimap_size);
    m_large_font = std::make_unique<AntialiasedFont>(ui_font_family, large_size, true);

    if (m_ui_font->getHeight() <= 0 || m_small_font->getHeight() <= 0 ||
        m_editor_font->getHeight() <= 0 || m_minimap_font->getHeight() <= 0 ||
        m_large_font->getHeight() <= 0)
    {
        shutdown();
        return false;
    }

    // Convert palette colors to CGFloat RGBA
    color_to_rgba(m_palette.workspace_background, m_colors.workspace_background);
    color_to_rgba(m_palette.tab_background, m_colors.tab_background);
    color_to_rgba(m_palette.tab_active_background, m_colors.tab_active_background);
    color_to_rgba(m_palette.sidebar_background, m_colors.sidebar_background);
    color_to_rgba(m_palette.editor_background, m_colors.editor_background);
    color_to_rgba(m_palette.active_line_background, m_colors.active_line_background);
    color_to_rgba(m_palette.selection_background, m_colors.selection_background);
    color_to_rgba(m_palette.status_background, m_colors.status_background);
    color_to_rgba(m_palette.border, m_colors.border);
    color_to_rgba(m_palette.text_primary, m_colors.text_primary);
    color_to_rgba(m_palette.text_muted, m_colors.text_muted);
    color_to_rgba(m_palette.accent, m_colors.accent);
    color_to_rgba(m_palette.warning, m_colors.warning);
    color_to_rgba(m_palette.success, m_colors.success);
    color_to_rgba(m_palette.hover_background, m_colors.hover_background);
    color_to_rgba(m_palette.indent_guide, m_colors.indent_guide);
    color_to_rgba(m_palette.indent_guide_active, m_colors.indent_guide_active);
    m_text.primary = color_to_hex(m_palette.text_primary);
    m_text.muted = color_to_hex(m_palette.text_muted);
    m_text.keyword = color_to_hex(m_palette.keyword);
    m_text.number = color_to_hex(m_palette.number);
    m_text.label = color_to_hex(m_palette.label);
    m_text.type = color_to_hex(m_palette.type);
    m_text.comment = color_to_hex(m_palette.comment);
    m_text.accent = color_to_hex(m_palette.accent);
    m_text.warning = color_to_hex(m_palette.warning);
    m_text.success = color_to_hex(m_palette.success);
    static_cast<void>(m_tool_sidebar.initialize());
    static_cast<void>(m_terminal_panel.toggle());
    m_terminal_panel.set_focused(false);
    return true;
}

bool StudioWorkspaceRenderer::open_file(const std::filesystem::path& path)
{
    return m_text_editor.open_file(path);
}

bool StudioWorkspaceRenderer::set_workspace_root(const std::filesystem::path& root)
{
    if (!m_tool_sidebar.set_workspace_root(root))
    {
        return false;
    }
    m_terminal_panel.set_working_directory(root);
    return true;
}

std::size_t StudioWorkspaceRenderer::open_dropped_paths(
    std::span<const std::filesystem::path> dropped_paths)
{
    return m_text_editor.open_dropped_paths(dropped_paths);
}

bool StudioWorkspaceRenderer::create_buffer()
{
    return m_text_editor.create_buffer();
}

bool StudioWorkspaceRenderer::toggle_terminal()
{
    return m_terminal_panel.toggle();
}

bool StudioWorkspaceRenderer::handle_pointer_press(
    float point_x, float point_y,
    int client_width, int client_height,
    float content_top, bool extend_selection,
    int click_count, double event_time,
    std::string& command_out)
{
    const UI::Editor::StudioEditorLayoutResult layout = m_layout_engine.calculate(
        static_cast<float>(client_width),
        static_cast<float>(client_height),
        content_top,
        m_dpi_scale,
        m_terminal_panel.is_visible(),
        m_terminal_panel.get_height(),
        m_terminal_panel.is_maximized(),
        m_tool_sidebar.is_visible(),
        m_tool_sidebar.get_width());
    if (const std::optional<std::size_t> sidebar_index =
            UI::Editor::hit_test_studio_sidebar(layout, point_x, point_y))
    {
        const std::span<const UI::Editor::SidebarItem> items =
            UI::Editor::get_studio_sidebar_items();
        if (items[*sidebar_index].icon == UI::Editor::SidebarIcon::Terminal)
        {
            return m_terminal_panel.toggle();
        }
        return m_tool_sidebar.activate(items[*sidebar_index].icon);
    }
    std::optional<std::filesystem::path> sidebar_file;
    if (m_tool_sidebar.handle_pointer_press(layout, point_x, point_y, sidebar_file))
    {
        m_terminal_panel.set_focused(false);
        if (sidebar_file)
        {
            if (sidebar_file->string() == "::OPEN_FOLDER::")
            {
                command_out = "zde.project.open";
            }
            else
            {
                static_cast<void>(m_text_editor.open_file(*sidebar_file));
            }
        }
        return true;
    }
    if (m_terminal_panel.handle_pointer_press(layout, point_x, point_y, event_time))
    {
        return true;
    }
    m_terminal_panel.set_focused(false);
    return m_text_editor.handle_pointer_press(
        *this, layout, point_x, point_y, extend_selection, click_count,
        command_out);
}

bool StudioWorkspaceRenderer::handle_pointer_move(
    float point_x, float point_y,
    int client_width, int client_height,
    float content_top) noexcept
{
    const UI::Editor::StudioEditorLayoutResult layout = m_layout_engine.calculate(
        static_cast<float>(client_width),
        static_cast<float>(client_height),
        content_top,
        m_dpi_scale,
        m_terminal_panel.is_visible(),
        m_terminal_panel.get_height(),
        m_terminal_panel.is_maximized(),
        m_tool_sidebar.is_visible(),
        m_tool_sidebar.get_width());
    const bool sidebar_changed = m_tool_sidebar.handle_pointer_move(
        layout, point_x, point_y);
    const bool editor_changed = m_text_editor.handle_pointer_move(
        layout, point_x, point_y);
    return m_terminal_panel.handle_pointer_move(layout, point_x, point_y) ||
        sidebar_changed || editor_changed;
}

bool StudioWorkspaceRenderer::handle_pointer_drag(
    float point_x, float point_y,
    int client_width, int client_height,
    float content_top)
{
    const UI::Editor::StudioEditorLayoutResult layout = m_layout_engine.calculate(
        static_cast<float>(client_width),
        static_cast<float>(client_height),
        content_top,
        m_dpi_scale,
        m_terminal_panel.is_visible(),
        m_terminal_panel.get_height(),
        m_terminal_panel.is_maximized(),
        m_tool_sidebar.is_visible(),
        m_tool_sidebar.get_width());
    if (m_terminal_panel.handle_pointer_drag(layout, point_y))
    {
        return true;
    }
    if (m_tool_sidebar.handle_pointer_drag(layout, point_x))
    {
        return true;
    }
    return m_text_editor.handle_pointer_drag(*this, layout, point_x, point_y);
}

bool StudioWorkspaceRenderer::handle_pointer_release() noexcept
{
    const bool terminal = m_terminal_panel.handle_pointer_release();
    const bool sidebar = m_tool_sidebar.handle_pointer_release();
    const bool editor = m_text_editor.handle_pointer_release();
    return terminal || sidebar || editor;
}

bool StudioWorkspaceRenderer::handle_scroll(
    float point_x, float point_y,
    std::string& command_out,
    std::ptrdiff_t line_delta, bool horizontal,
    int client_width, int client_height,
    float content_top) noexcept
{
    const UI::Editor::StudioEditorLayoutResult layout = m_layout_engine.calculate(
        static_cast<float>(client_width),
        static_cast<float>(client_height),
        content_top,
        m_dpi_scale,
        m_terminal_panel.is_visible(),
        m_terminal_panel.get_height(),
        m_terminal_panel.is_maximized(),
        m_tool_sidebar.is_visible(),
        m_tool_sidebar.get_width());
    if (m_tool_sidebar.contains(layout, point_x, point_y))
    {
        if (horizontal)
        {
            return false;
        }
        return m_tool_sidebar.handle_scroll(layout, line_delta);
    }
    if (m_terminal_panel.contains(layout, point_x, point_y))
    {
        return m_terminal_panel.handle_scroll(line_delta, horizontal);
    }
    return m_text_editor.handle_scroll(
        *this, layout, point_x, point_y, command_out, line_delta, horizontal);
}

bool StudioWorkspaceRenderer::handle_editor_input(
    UI::Editor::EditorInputCommand command, bool extend_selection)
{
    return m_text_editor.handle_input(command, extend_selection);
}

bool StudioWorkspaceRenderer::handle_editor_action(UI::Editor::EditorAction action)
{
    return m_text_editor.handle_action(action);
}

std::optional<bool> StudioWorkspaceRenderer::handle_editor_command(std::string_view command_id)
{
    return m_text_editor.handle_command(command_id);
}

std::optional<bool> StudioWorkspaceRenderer::is_editor_command_enabled(
    std::string_view command_id) const noexcept
{
    return m_text_editor.is_command_enabled(command_id);
}

bool StudioWorkspaceRenderer::handle_text_input(std::string_view utf8_text)
{
    if (m_terminal_panel.is_focused())
    {
        return m_terminal_panel.handle_text_input(utf8_text);
    }
    return m_text_editor.handle_text_input(utf8_text);
}

bool StudioWorkspaceRenderer::handle_terminal_key(Terminal::TerminalInputKey key)
{
    return m_terminal_panel.handle_key(key);
}

bool StudioWorkspaceRenderer::handle_terminal_control(char letter)
{
    return m_terminal_panel.handle_control(letter);
}

bool StudioWorkspaceRenderer::handle_terminal_scroll(
    std::ptrdiff_t line_delta, bool horizontal) noexcept
{
    return m_terminal_panel.handle_scroll(line_delta, horizontal);
}

bool StudioWorkspaceRenderer::handle_tool_sidebar_scroll(
    std::ptrdiff_t line_delta,
    int client_width, int client_height,
    float content_top) noexcept
{
    const UI::Editor::StudioEditorLayoutResult layout = m_layout_engine.calculate(
        static_cast<float>(client_width),
        static_cast<float>(client_height),
        content_top,
        m_dpi_scale,
        m_terminal_panel.is_visible(),
        m_terminal_panel.get_height(),
        m_terminal_panel.is_maximized(),
        m_tool_sidebar.is_visible(),
        m_tool_sidebar.get_width());
    return m_tool_sidebar.handle_scroll(layout, line_delta);
}

bool StudioWorkspaceRenderer::is_editor_focused() const noexcept
{
    return m_text_editor.is_focused();
}

bool StudioWorkspaceRenderer::is_terminal_focused() const noexcept
{
    return m_terminal_panel.is_focused();
}

bool StudioWorkspaceRenderer::is_activity_bar_point(
    float point_x, float point_y,
    int client_width, int client_height,
    float content_top) const noexcept
{
    const UI::Editor::StudioEditorLayoutResult layout = m_layout_engine.calculate(
        static_cast<float>(client_width), static_cast<float>(client_height),
        content_top, m_dpi_scale,
        m_terminal_panel.is_visible(), m_terminal_panel.get_height(),
        m_terminal_panel.is_maximized(),
        m_tool_sidebar.is_visible(), m_tool_sidebar.get_width());
    return layout.activity_bar_bounds.contains(point_x, point_y);
}

bool StudioWorkspaceRenderer::is_tab_bar_point(
    float point_x, float point_y,
    int client_width, int client_height,
    float content_top) const noexcept
{
    const UI::Editor::StudioEditorLayoutResult layout = m_layout_engine.calculate(
        static_cast<float>(client_width), static_cast<float>(client_height),
        content_top, m_dpi_scale,
        m_terminal_panel.is_visible(), m_terminal_panel.get_height(),
        m_terminal_panel.is_maximized(),
        m_tool_sidebar.is_visible(), m_tool_sidebar.get_width());
    return layout.tab_bar_bounds.contains(point_x, point_y);
}

bool StudioWorkspaceRenderer::is_tab_bar_area_point(
    float point_x, float point_y,
    int client_width, int client_height,
    float content_top) const noexcept
{
    return is_tab_bar_point(point_x, point_y, client_width, client_height, content_top);
}

bool StudioWorkspaceRenderer::is_editor_point(
    float point_x, float point_y,
    int client_width, int client_height,
    float content_top) const noexcept
{
    const UI::Editor::StudioEditorLayoutResult layout = m_layout_engine.calculate(
        static_cast<float>(client_width), static_cast<float>(client_height),
        content_top, m_dpi_scale,
        m_terminal_panel.is_visible(), m_terminal_panel.get_height(),
        m_terminal_panel.is_maximized(),
        m_tool_sidebar.is_visible(), m_tool_sidebar.get_width());
    return layout.editor_bounds.contains(point_x, point_y) ||
           layout.gutter_bounds.contains(point_x, point_y);
}

bool StudioWorkspaceRenderer::is_scrollbar_point(
    float point_x, float point_y,
    int client_width, int client_height,
    float content_top) const noexcept
{
    const UI::Editor::StudioEditorLayoutResult layout = m_layout_engine.calculate(
        static_cast<float>(client_width), static_cast<float>(client_height),
        content_top, m_dpi_scale,
        m_terminal_panel.is_visible(), m_terminal_panel.get_height(),
        m_terminal_panel.is_maximized(),
        m_tool_sidebar.is_visible(), m_tool_sidebar.get_width());
    return layout.scrollbar_bounds.contains(point_x, point_y);
}

bool StudioWorkspaceRenderer::is_minimap_point(
    float point_x, float point_y,
    int client_width, int client_height,
    float content_top) const noexcept
{
    const UI::Editor::StudioEditorLayoutResult layout = m_layout_engine.calculate(
        static_cast<float>(client_width), static_cast<float>(client_height),
        content_top, m_dpi_scale,
        m_terminal_panel.is_visible(), m_terminal_panel.get_height(),
        m_terminal_panel.is_maximized(),
        m_tool_sidebar.is_visible(), m_tool_sidebar.get_width());
    return layout.minimap_bounds.contains(point_x, point_y);
}

bool StudioWorkspaceRenderer::is_fold_margin_point(
    float point_x, float point_y,
    int client_width, int client_height,
    float content_top) const noexcept
{
    (void)point_x; (void)point_y; (void)client_width; (void)client_height; (void)content_top;
    return false; // TODO: implement fold margin hit test
}

bool StudioWorkspaceRenderer::is_terminal_point(
    float point_x, float point_y,
    int client_width, int client_height,
    float content_top) const noexcept
{
    const UI::Editor::StudioEditorLayoutResult layout = m_layout_engine.calculate(
        static_cast<float>(client_width), static_cast<float>(client_height),
        content_top, m_dpi_scale,
        m_terminal_panel.is_visible(), m_terminal_panel.get_height(),
        m_terminal_panel.is_maximized(),
        m_tool_sidebar.is_visible(), m_tool_sidebar.get_width());
    return m_terminal_panel.contains(layout, point_x, point_y);
}

bool StudioWorkspaceRenderer::is_tool_sidebar_point(
    float point_x, float point_y,
    int client_width, int client_height,
    float content_top) const noexcept
{
    const UI::Editor::StudioEditorLayoutResult layout = m_layout_engine.calculate(
        static_cast<float>(client_width), static_cast<float>(client_height),
        content_top, m_dpi_scale,
        m_terminal_panel.is_visible(), m_terminal_panel.get_height(),
        m_terminal_panel.is_maximized(),
        m_tool_sidebar.is_visible(), m_tool_sidebar.get_width());
    return m_tool_sidebar.contains(layout, point_x, point_y);
}

bool StudioWorkspaceRenderer::is_terminal_resize_handle_point(
    float point_x, float point_y,
    int client_width, int client_height,
    float content_top) const noexcept
{
    const UI::Editor::StudioEditorLayoutResult layout = m_layout_engine.calculate(
        static_cast<float>(client_width), static_cast<float>(client_height),
        content_top, m_dpi_scale,
        m_terminal_panel.is_visible(), m_terminal_panel.get_height(),
        m_terminal_panel.is_maximized(),
        m_tool_sidebar.is_visible(), m_tool_sidebar.get_width());
    return m_terminal_panel.is_resize_handle_point(layout, point_x, point_y);
}

bool StudioWorkspaceRenderer::is_terminal_resizing() const noexcept
{
    return m_terminal_panel.is_resizing();
}

bool StudioWorkspaceRenderer::is_editor_interactive_point(
    float point_x, float point_y) const noexcept
{
    return m_text_editor.is_empty_state_interactive_point(point_x, point_y);
}

bool StudioWorkspaceRenderer::is_terminal_interactive_point(
    float point_x, float point_y,
    int client_width, int client_height,
    float content_top) const noexcept
{
    const UI::Editor::StudioEditorLayoutResult layout = m_layout_engine.calculate(
        static_cast<float>(client_width), static_cast<float>(client_height),
        content_top, m_dpi_scale,
        m_terminal_panel.is_visible(), m_terminal_panel.get_height(),
        m_terminal_panel.is_maximized(),
        m_tool_sidebar.is_visible(), m_tool_sidebar.get_width());
    return m_terminal_panel.is_interactive_point(layout, point_x, point_y);
}

bool StudioWorkspaceRenderer::is_sidebar_resize_handle_point(
    float point_x, float point_y,
    int client_width, int client_height,
    float content_top) const noexcept
{
    const UI::Editor::StudioEditorLayoutResult layout = m_layout_engine.calculate(
        static_cast<float>(client_width), static_cast<float>(client_height),
        content_top, m_dpi_scale,
        m_terminal_panel.is_visible(), m_terminal_panel.get_height(),
        m_terminal_panel.is_maximized(),
        m_tool_sidebar.is_visible(), m_tool_sidebar.get_width());
    return m_tool_sidebar.is_resize_handle_point(layout, point_x, point_y);
}

bool StudioWorkspaceRenderer::is_sidebar_resizing() const noexcept
{
    return m_tool_sidebar.is_resizing();
}

bool StudioWorkspaceRenderer::is_empty_state_button_hovered() const noexcept
{
    return m_text_editor.is_empty_state_button_hovered();
}

bool StudioWorkspaceRenderer::tick_animations() noexcept
{
    const bool caret_changed = m_text_editor.tick_animations();
    const bool terminal_changed = m_terminal_panel.poll();
    const bool terminal_blink_changed = m_terminal_panel.tick_animations();
    return caret_changed || terminal_changed || terminal_blink_changed;
}

void StudioWorkspaceRenderer::shutdown()
{
    for (auto& [path, image] : m_image_cache)
    {
        if (image)
        {
            CGImageRelease(image);
        }
    }
    m_image_cache.clear();
    m_terminal_panel.shutdown();
    m_minimap_font.reset();
    m_editor_font.reset();
    m_small_font.reset();
    m_ui_font.reset();
    m_large_font.reset();
    m_icon_asset_root.clear();
}

void StudioWorkspaceRenderer::render(
    CGContextRef context,
    int client_width, int client_height,
    float content_top) const
{
    if (context == nullptr || m_ui_font == nullptr || m_small_font == nullptr ||
        m_editor_font == nullptr || m_minimap_font == nullptr || m_large_font == nullptr)
    {
        return;
    }
    const UI::Editor::StudioEditorLayoutResult layout = m_layout_engine.calculate(
        static_cast<float>(client_width),
        static_cast<float>(client_height),
        content_top,
        m_dpi_scale,
        m_terminal_panel.is_visible(),
        m_terminal_panel.get_height(),
        m_terminal_panel.is_maximized(),
        m_tool_sidebar.is_visible(),
        m_tool_sidebar.get_width());
    fill_rectangle(context, layout.workspace_bounds, m_colors.workspace_background);
    fill_rectangle(context, layout.tab_bar_bounds, m_colors.tab_background);
    fill_rectangle(context, layout.activity_bar_bounds, m_colors.sidebar_background);
    fill_rectangle(context, layout.tool_sidebar_bounds, m_colors.sidebar_background);
    fill_rectangle(context, layout.gutter_bounds, m_colors.editor_background);
    fill_rectangle(context, layout.editor_bounds, m_colors.editor_background);
    fill_rectangle(context, layout.status_bar_bounds, m_colors.status_background);
    draw_line(context, 0, round_to_int(layout.status_bar_bounds.y),
              round_to_int(layout.status_bar_bounds.right()),
              round_to_int(layout.status_bar_bounds.y),
              m_colors.border);

    m_text_editor.render(*this, context, layout);
    m_terminal_panel.render(*this, context, layout);
    m_tool_sidebar.render(*this, context, layout);
    m_activity_sidebar.render(*this, context, layout);
    if (const UI::Editor::TextDocumentModel* document = m_text_editor.get_document())
    {
        const std::vector<UI::Editor::BreadcrumbItem> full_breadcrumbs =
            document->get_full_breadcrumbs();
        m_footer_toolbar.render(
            *this, context, layout, full_breadcrumbs, document->get_status());
    }
}

std::string_view StudioWorkspaceRenderer::get_active_buffer_name() const noexcept
{
    if (const auto* doc = m_text_editor.get_document())
    {
        return doc->get_file_name();
    }
    return {};
}

const std::filesystem::path& StudioWorkspaceRenderer::get_icon_asset_root() const noexcept
{
    return m_icon_asset_root;
}

// --- Drawing Primitives (CoreGraphics equivalents) ---

void StudioWorkspaceRenderer::fill_rectangle(
    CGContextRef context,
    const UI::Rect& rectangle,
    const CGFloat* rgba) const
{
    if (rectangle.is_empty())
    {
        return;
    }
    CGContextSetRGBFillColor(context, rgba[0], rgba[1], rgba[2], rgba[3]);
    CGContextFillRect(context, CGRectMake(
        static_cast<CGFloat>(rectangle.x),
        static_cast<CGFloat>(rectangle.y),
        static_cast<CGFloat>(rectangle.width),
        static_cast<CGFloat>(rectangle.height)));
}

void StudioWorkspaceRenderer::fill_rounded_rectangle(
    CGContextRef context,
    const UI::Rect& rectangle,
    const CGFloat* rgba,
    float radius) const
{
    if (rectangle.is_empty())
    {
        return;
    }
    CGContextSetRGBFillColor(context, rgba[0], rgba[1], rgba[2], rgba[3]);
    CGRect cg_rect = CGRectMake(
        static_cast<CGFloat>(rectangle.x),
        static_cast<CGFloat>(rectangle.y),
        static_cast<CGFloat>(rectangle.width),
        static_cast<CGFloat>(rectangle.height));
    CGPathRef path = CGPathCreateWithRoundedRect(
        cg_rect, static_cast<CGFloat>(radius), static_cast<CGFloat>(radius),
        nullptr);
    CGContextBeginPath(context);
    CGContextAddPath(context, path);
    CGContextFillPath(context);
    CGPathRelease(path);
}

void StudioWorkspaceRenderer::draw_rectangle(
    CGContextRef context,
    const UI::Rect& rectangle,
    const CGFloat* rgba) const
{
    if (rectangle.is_empty())
    {
        return;
    }
    CGContextSetRGBStrokeColor(context, rgba[0], rgba[1], rgba[2], rgba[3]);
    CGContextStrokeRect(context, CGRectMake(
        static_cast<CGFloat>(rectangle.x),
        static_cast<CGFloat>(rectangle.y),
        static_cast<CGFloat>(rectangle.width),
        static_cast<CGFloat>(rectangle.height)));
}

void StudioWorkspaceRenderer::draw_line(
    CGContextRef context,
    int from_x, int from_y, int to_x, int to_y,
    const CGFloat* rgba) const
{
    CGContextSetRGBStrokeColor(context, rgba[0], rgba[1], rgba[2], rgba[3]);
    CGContextSetLineWidth(context, 1.0);
    CGContextBeginPath(context);
    CGContextMoveToPoint(context,
        static_cast<CGFloat>(from_x) + 0.5,
        static_cast<CGFloat>(from_y) + 0.5);
    CGContextAddLineToPoint(context,
        static_cast<CGFloat>(to_x) + 0.5,
        static_cast<CGFloat>(to_y) + 0.5);
    CGContextStrokePath(context);
}

void StudioWorkspaceRenderer::draw_text(
    CGContextRef context,
    AntialiasedFont& font,
    std::string_view text,
    float point_x, float center_y,
    const std::string& color,
    const UI::Rect* clip_rect) const
{
    if (text.empty())
    {
        return;
    }
    const int baseline = round_to_int(
        center_y - static_cast<float>(font.getAscent() + font.getDescent()) * 0.5F +
        static_cast<float>(font.getAscent()));

    if (clip_rect)
    {
        CGRect cg_clip = CGRectMake(
            static_cast<CGFloat>(clip_rect->x),
            static_cast<CGFloat>(clip_rect->y),
            static_cast<CGFloat>(std::max(0.0F, clip_rect->width)),
            static_cast<CGFloat>(std::max(0.0F, clip_rect->height)));
        font.drawString(context, color, round_to_int(point_x), baseline,
                        std::string{text}, &cg_clip);
    }
    else
    {
        font.drawString(context, color, round_to_int(point_x), baseline,
                        std::string{text});
    }
}

void StudioWorkspaceRenderer::push_clip(CGContextRef context, const UI::Rect& rect) const
{
    CGContextSaveGState(context);
    CGContextClipToRect(context, CGRectMake(
        static_cast<CGFloat>(rect.x),
        static_cast<CGFloat>(rect.y),
        static_cast<CGFloat>(std::max(0.0F, rect.width)),
        static_cast<CGFloat>(std::max(0.0F, rect.height))));
}

void StudioWorkspaceRenderer::pop_clip(CGContextRef context) const
{
    CGContextRestoreGState(context);
}

std::filesystem::path StudioWorkspaceRenderer::resolve_icon_path(
    const std::string& path) const
{
    std::error_code path_error;
    std::filesystem::path resolved_path{path};
    if (resolved_path.is_relative() && !m_icon_asset_root.empty())
    {
        // m_icon_asset_root already points at the Assets/icons directory, so
        // strip the redundant prefix used by callers that pass
        // "Assets/icons/foo.svg" or "Assets/icons/material-icon-theme/foo.svg".
        std::string relative = resolved_path.generic_string();
        constexpr std::string_view icon_prefix = "Assets/icons/";
        if (relative.starts_with(icon_prefix))
        {
            relative.erase(0, icon_prefix.size());
            resolved_path = relative;
        }
        const std::filesystem::path themed_path = m_icon_asset_root / resolved_path;
        if (std::filesystem::is_regular_file(themed_path, path_error))
        {
            resolved_path = themed_path;
        }
        else
        {
            const std::filesystem::path legacy_path =
                m_icon_asset_root / resolved_path.filename();
            if (std::filesystem::is_regular_file(legacy_path, path_error))
            {
                resolved_path = legacy_path;
            }
        }
    }
    if (!std::filesystem::is_regular_file(resolved_path, path_error))
    {
        return {};
    }
    return resolved_path;
}

void StudioWorkspaceRenderer::store_cached_image(const std::string& key, CGImageRef image) const
{
    if (!image)
    {
        return;
    }
    if (m_image_cache.size() >= max_image_cache_size)
    {
        auto oldest = m_image_cache.begin();
        if (oldest != m_image_cache.end())
        {
            if (oldest->second)
            {
                CGImageRelease(oldest->second);
            }
            m_image_cache.erase(oldest);
        }
    }
    CGImageRetain(image);
    m_image_cache[key] = image;
}

bool StudioWorkspaceRenderer::draw_png_image(
    CGContextRef context,
    const std::string& path,
    int center_x, int center_y,
    int size) const
{
    if (size <= 0 || context == nullptr)
    {
        return false;
    }
    const std::filesystem::path resolved_path = resolve_icon_path(path);
    if (resolved_path.empty())
    {
        return false;
    }

    const std::string cache_key = resolved_path.string() + "@png#" + std::to_string(size);
    CGImageRef image = nullptr;
    auto it = m_image_cache.find(cache_key);
    if (it != m_image_cache.end())
    {
        image = it->second;
    }
    else
    {
        CGImageSourceRef source = CGImageSourceCreateWithURL(
            (__bridge CFURLRef)[NSURL fileURLWithPath:@(resolved_path.c_str())],
            nullptr);
        if (source == nullptr)
        {
            return false;
        }
        image = CGImageSourceCreateImageAtIndex(source, 0, nullptr);
        CFRelease(source);
        if (image == nullptr)
        {
            return false;
        }
        store_cached_image(cache_key, image);
        CGImageRelease(image);
    }

    if (image)
    {
        const int half = size / 2;
        const int draw_x = center_x - half;
        const int draw_y = center_y - half;

        CGContextSaveGState(context);
        CGContextTranslateCTM(context, static_cast<CGFloat>(draw_x), static_cast<CGFloat>(draw_y + size));
        CGContextScaleCTM(context, 1.0, -1.0);
        CGContextDrawImage(context,
            CGRectMake(0, 0, static_cast<CGFloat>(size), static_cast<CGFloat>(size)),
            image);
        CGContextRestoreGState(context);
        return true;
    }
    return false;
}

void StudioWorkspaceRenderer::draw_svg_icon(
    CGContextRef context,
    const std::string& path,
    int center_x, int center_y,
    int size,
    const UI::Theme::Color& color,
    const UI::Theme::Color& background,
    bool preserve_source_colors) const
{
    if (size <= 0 || context == nullptr)
    {
        return;
    }

    const std::filesystem::path resolved_path = resolve_icon_path(path);
    if (resolved_path.empty())
    {
        return;
    }
    preserve_source_colors = preserve_source_colors &&
        resolved_path.parent_path().filename() == "material-icon-theme";

    const std::string cache_key = resolved_path.string() + "@" + std::to_string(size) + "#" +
        color_to_hex(color) + "/" + color_to_hex(background) + (preserve_source_colors ? "_p" : "");
    CGImageRef cg_image = nullptr;
    auto it = m_image_cache.find(cache_key);
    if (it != m_image_cache.end())
    {
        cg_image = it->second;
    }
    else
    {
        auto document = lunasvg::Document::loadFromFile(resolved_path.string());
        if (!document)
        {
            return;
        }

        auto bitmap = document->renderToBitmap(
            static_cast<std::uint32_t>(size), static_cast<std::uint32_t>(size));
        if (bitmap.isNull())
        {
            return;
        }

        const std::uint32_t width = bitmap.width();
        const std::uint32_t height = bitmap.height();
        const std::uint8_t* data = bitmap.data();

        // lunasvg outputs RGBA premultiplied. Apply tint if not preserving source colors.
        std::vector<std::uint8_t> tinted_data;
        const std::uint8_t* image_data = data;
        if (!preserve_source_colors)
        {
            tinted_data.resize(static_cast<std::size_t>(width * height * 4));
            for (std::uint32_t i = 0; i < width * height; ++i)
            {
                const float alpha = static_cast<float>(data[i * 4 + 3]) / 255.0F;
                tinted_data[i * 4 + 0] = static_cast<std::uint8_t>(static_cast<float>(color.red) * alpha);
                tinted_data[i * 4 + 1] = static_cast<std::uint8_t>(static_cast<float>(color.green) * alpha);
                tinted_data[i * 4 + 2] = static_cast<std::uint8_t>(static_cast<float>(color.blue) * alpha);
                tinted_data[i * 4 + 3] = data[i * 4 + 3];
            }
            image_data = tinted_data.data();
        }

        CGColorSpaceRef color_space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        CGContextRef bitmap_context = CGBitmapContextCreate(
            const_cast<std::uint8_t*>(image_data),
            width, height, 8, width * 4, color_space,
            kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
        if (bitmap_context)
        {
            cg_image = CGBitmapContextCreateImage(bitmap_context);
            CGContextRelease(bitmap_context);
        }
        CGColorSpaceRelease(color_space);

        if (cg_image)
        {
            store_cached_image(cache_key, cg_image);
            CGImageRelease(cg_image);
        }
    }

    if (cg_image)
    {
        const int half = size / 2;
        const int draw_x = center_x - half;
        const int draw_y = center_y - half;

        CGContextSaveGState(context);
        CGContextTranslateCTM(context, static_cast<CGFloat>(draw_x), static_cast<CGFloat>(draw_y + size));
        CGContextScaleCTM(context, 1.0, -1.0);
        
        CGContextDrawImage(context,
            CGRectMake(0, 0,
                       static_cast<CGFloat>(size),
                       static_cast<CGFloat>(size)),
            cg_image);
            
        CGContextRestoreGState(context);
    }
}

} // namespace Zenvra::Platform::Cocoa::Components
