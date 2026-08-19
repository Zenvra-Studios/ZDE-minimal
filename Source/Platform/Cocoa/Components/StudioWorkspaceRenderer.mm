#import <Cocoa/Cocoa.h>

#include "Platform/Cocoa/Components/StudioWorkspaceRenderer.h"
#include "Utility/Fonts.h"
#include "UI/Editor/EditorFileSystem.h"
#include "Commands/CommandIds.h"

#include <lunasvg.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

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
    //   1. App bundle Resources  (Assets copied there by CMake)
    //   2. App bundle root
    //   3. CWD project root  (dev run from a terminal at the repo root)
    //   4. Walk up from the executable directory (dev non-bundle binaries)
    std::optional<std::filesystem::path> asset_root;
    const auto contains_assets = [](const std::filesystem::path& directory) {
        std::error_code error_code;
        return (std::filesystem::is_directory(directory, error_code) &&
                std::filesystem::is_directory(directory / "Assets" / "icons", error_code)) ||
               (std::filesystem::is_directory(directory, error_code) &&
                std::filesystem::is_directory(directory / "icons", error_code) &&
                std::filesystem::exists(directory / "icons" / "folder.svg", error_code));
    };

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

    if (!asset_root)
    {
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
        std::error_code ec;
        if (std::filesystem::is_directory(*asset_root / "Assets" / "icons", ec))
        {
            m_icon_asset_root = *asset_root / "Assets" / "icons";
        }
        else if (std::filesystem::is_directory(*asset_root / "icons", ec))
        {
            m_icon_asset_root = *asset_root / "icons";
        }
        else
        {
            m_icon_asset_root = *asset_root;
        }
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
    static_cast<void>(m_shader_sandbox_panel.initialize());
    m_animated_titlebar_left_offset = m_is_fullscreen
        ? 0.0F
        : (UI::Editor::StudioEditorMetrics::titlebar_navigation_width * m_dpi_scale);
    m_last_titlebar_tick_ms = 0;
    return true;
}

void StudioWorkspaceRenderer::set_fullscreen(bool fullscreen) noexcept
{
    m_is_fullscreen = fullscreen;
    m_animated_titlebar_left_offset = m_is_fullscreen
        ? 0.0F
        : (UI::Editor::StudioEditorMetrics::titlebar_navigation_width * m_dpi_scale);
}

bool StudioWorkspaceRenderer::is_fullscreen() const noexcept
{
    return m_is_fullscreen;
}

float StudioWorkspaceRenderer::get_animated_titlebar_left_offset() const noexcept
{
    return m_animated_titlebar_left_offset;
}

UI::Editor::StudioEditorLayoutResult StudioWorkspaceRenderer::calculate_layout(
    int client_width, int client_height, float content_top) const noexcept
{
    return m_layout_engine.calculate(
        static_cast<float>(client_width),
        static_cast<float>(client_height),
        content_top,
        m_dpi_scale,
        m_terminal_panel.is_visible(),
        m_terminal_panel.get_height(),
        m_terminal_panel.is_maximized(),
        m_tool_sidebar.is_visible(),
        m_tool_sidebar.get_width(),
        m_shader_sandbox_panel.is_visible(),
        m_shader_sandbox_panel.get_width(),
        m_animated_titlebar_left_offset);
}

void StudioWorkspaceRenderer::sync_shader_sandbox() const
{
    if (!m_shader_sandbox_panel.is_visible())
    {
        return;
    }
    if (const UI::Editor::TextDocumentModel* doc = m_text_editor.get_document())
    {
        const std::string filename = std::string(doc->get_file_name());
        const std::filesystem::path file_path(filename);
        const std::string ext = file_path.extension().string();

        std::string full_text;
        for (const auto& line : doc->get_lines())
        {
            full_text += line;
            full_text += '\n';
        }

        const bool is_shader_ext = (ext == ".glsl" || ext == ".frag" || ext == ".vert" || 
                                    ext == ".comp" || ext == ".shader" || ext == ".hlsl" ||
                                    ext == ".geom" || ext == ".tesc" || ext == ".tese");
        const bool is_shader_content = (full_text.find("mainImage") != std::string::npos ||
                                        full_text.find("gl_FragColor") != std::string::npos ||
                                        full_text.find("gl_FragCoord") != std::string::npos ||
                                        full_text.find("#version") != std::string::npos);

        if ((is_shader_ext || is_shader_content) && !full_text.empty())
        {
            m_shader_sandbox_panel.set_source_code(full_text);
        }
    }
}

bool StudioWorkspaceRenderer::open_file(const std::filesystem::path& path)
{
    const bool res = m_text_editor.open_file(path);
    if (res)
    {
        const std::string ext = path.extension().string();
        if (ext == ".glsl" || ext == ".frag" || ext == ".vert" || ext == ".comp" ||
            ext == ".shader" || ext == ".hlsl")
        {
            m_shader_sandbox_panel.set_visible(true);
        }
        sync_shader_sandbox();
    }
    return res;
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

bool StudioWorkspaceRenderer::close_project()
{
    static_cast<void>(m_tool_sidebar.set_workspace_root({}));
    m_tool_sidebar.get_model().clear_workspace();
    m_text_editor.close_all_documents();
    m_text_editor.reset_split();
    m_terminal_panel.shutdown();
    m_shader_sandbox_panel.set_visible(false);
    return true;
}

std::size_t StudioWorkspaceRenderer::open_dropped_paths(
    std::span<const std::filesystem::path> dropped_paths)
{
    const std::size_t count = m_text_editor.open_dropped_paths(dropped_paths);
    if (count > 0)
    {
        sync_shader_sandbox();
    }
    return count;
}

bool StudioWorkspaceRenderer::create_buffer()
{
    const bool res = m_text_editor.create_buffer();
    if (res)
    {
        sync_shader_sandbox();
    }
    return res;
}

bool StudioWorkspaceRenderer::toggle_shader_sandbox()
{
    const bool res = m_shader_sandbox_panel.toggle();
    if (res)
    {
        sync_shader_sandbox();
    }
    return res;
}

bool StudioWorkspaceRenderer::toggle_terminal()
{
    return m_terminal_panel.toggle();
}

bool StudioWorkspaceRenderer::create_terminal()
{
    return m_terminal_panel.create_terminal();
}

bool StudioWorkspaceRenderer::handle_pointer_press(
    float point_x, float point_y,
    int client_width, int client_height,
    float content_top, bool extend_selection,
    int click_count, double event_time,
    std::string& command_out)
{
    if (m_explorer_context_menu.visible)
    {
        for (std::size_t i = 0; i < m_explorer_context_menu.item_bounds.size(); ++i)
        {
            if (!m_explorer_context_menu.items[i].separator &&
                m_explorer_context_menu.item_bounds[i].contains(point_x, point_y))
            {
                execute_explorer_context_menu_item(i);
                return true;
            }
        }
        close_explorer_context_menu();
        return true;
    }

    const UI::Editor::StudioEditorLayoutResult layout =
        calculate_layout(client_width, client_height, content_top);
    if (const std::optional<std::size_t> sidebar_index =
            UI::Editor::hit_test_studio_sidebar(layout, point_x, point_y))
    {
        const std::span<const UI::Editor::SidebarItem> items =
            UI::Editor::get_studio_sidebar_items();
        if (items[*sidebar_index].icon == UI::Editor::SidebarIcon::Terminal)
        {
            return m_terminal_panel.toggle();
        }
        if (items[*sidebar_index].icon == UI::Editor::SidebarIcon::Shader)
        {
            const bool res = m_shader_sandbox_panel.toggle();
            if (res)
            {
                sync_shader_sandbox();
            }
            return true;
        }
        return m_tool_sidebar.activate(items[*sidebar_index].icon);
    }
    std::optional<std::filesystem::path> sidebar_file;
    std::optional<std::size_t> target_line;
    std::optional<std::size_t> target_col;
    if (m_tool_sidebar.handle_pointer_press(*this, layout, point_x, point_y, sidebar_file, target_line, target_col))
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
                static_cast<void>(open_file(*sidebar_file));
                if (target_line.has_value())
                {
                    const std::size_t l = target_line.value_or(1);
                    const std::size_t c = target_col.value_or(0);
                    if (auto* doc = m_text_editor.get_focused_document())
                    {
                        doc->set_caret(l > 0 ? l - 1 : 0, c, false);
                    }
                }
            }
        }
        return true;
    }
    if (m_terminal_panel.handle_pointer_press(layout, point_x, point_y, event_time))
    {
        m_text_editor.set_focused(false);
        return true;
    }
    m_terminal_panel.set_focused(false);
    if (m_shader_sandbox_panel.handle_pointer_press(layout, point_x, point_y))
    {
        return true;
    }
    const bool editor_pressed = m_text_editor.handle_pointer_press(
        *this, layout, point_x, point_y, extend_selection, click_count,
        command_out);
    if (editor_pressed)
    {
        sync_shader_sandbox();
    }
    return editor_pressed;
}

bool StudioWorkspaceRenderer::handle_pointer_move(
    float point_x, float point_y,
    int client_width, int client_height,
    float content_top) noexcept
{
    if (m_explorer_context_menu.visible)
    {
        std::optional<std::size_t> new_hover;
        for (std::size_t i = 0; i < m_explorer_context_menu.item_bounds.size(); ++i)
        {
            if (!m_explorer_context_menu.items[i].separator &&
                m_explorer_context_menu.item_bounds[i].contains(point_x, point_y))
            {
                new_hover = i;
                break;
            }
        }
        m_explorer_context_menu.hovered_index = new_hover;
        return true;
    }

    const UI::Editor::StudioEditorLayoutResult layout =
        calculate_layout(client_width, client_height, content_top);
    const bool sidebar_changed = m_tool_sidebar.handle_pointer_move(
        layout, point_x, point_y);
    const bool editor_changed = m_text_editor.handle_pointer_move(
        layout, point_x, point_y);
    const bool shader_changed = m_shader_sandbox_panel.handle_pointer_move(
        layout, point_x, point_y);
    return m_terminal_panel.handle_pointer_move(layout, point_x, point_y) ||
        sidebar_changed || editor_changed || shader_changed;
}

bool StudioWorkspaceRenderer::handle_pointer_drag(
    float point_x, float point_y,
    int client_width, int client_height,
    float content_top)
{
    const UI::Editor::StudioEditorLayoutResult layout =
        calculate_layout(client_width, client_height, content_top);
    if (m_shader_sandbox_panel.handle_pointer_drag(layout, point_x, point_y))
    {
        return true;
    }
    if (m_terminal_panel.handle_pointer_drag(layout, point_y))
    {
        return true;
    }
    if (m_tool_sidebar.handle_pointer_drag(layout, point_x, point_y))
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
    const bool shader = m_shader_sandbox_panel.handle_pointer_release();
    return terminal || sidebar || editor || shader;
}

bool StudioWorkspaceRenderer::handle_scroll(
    float point_x, float point_y,
    std::string& command_out,
    std::ptrdiff_t line_delta, bool horizontal,
    int client_width, int client_height,
    float content_top) noexcept
{
    const UI::Editor::StudioEditorLayoutResult layout =
        calculate_layout(client_width, client_height, content_top);
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
    if (m_tool_sidebar.is_search_focused())
    {
        return m_tool_sidebar.handle_search_command(command, extend_selection);
    }
    if (m_tool_sidebar.is_source_control_focused())
    {
        return m_tool_sidebar.handle_source_control_command(command, extend_selection);
    }
    const bool res = m_text_editor.handle_input(command, extend_selection);
    if (res)
    {
        sync_shader_sandbox();
    }
    return res;
}

bool StudioWorkspaceRenderer::handle_editor_action(UI::Editor::EditorAction action)
{
    if (m_tool_sidebar.is_search_focused())
    {
        return m_tool_sidebar.handle_search_action(action);
    }
    if (m_tool_sidebar.is_source_control_focused())
    {
        return m_tool_sidebar.handle_source_control_action(action);
    }
    const bool res = m_text_editor.handle_action(action);
    if (res)
    {
        sync_shader_sandbox();
    }
    return res;
}

void StudioWorkspaceRenderer::reset_layout() noexcept
{
    m_tool_sidebar.get_model().set_visible(true);
    static_cast<void>(m_tool_sidebar.activate(UI::Editor::SidebarIcon::Project));
    if (m_terminal_panel.is_visible())
    {
        static_cast<void>(m_terminal_panel.toggle());
    }
    m_shader_sandbox_panel.set_visible(false);
    m_text_editor.reset_split();
}

std::optional<bool> StudioWorkspaceRenderer::handle_editor_command(std::string_view command_id)
{
    if (command_id == Commands::CommandIds::window_reset_layout)
    {
        reset_layout();
        return true;
    }
    if (command_id == Commands::CommandIds::view_toggle_right_dock ||
        command_id == "zde.view.shaderPanel" ||
        command_id == "zde.view.shader_sandbox")
    {
        const bool res = toggle_shader_sandbox();
        if (res)
        {
            sync_shader_sandbox();
        }
        return res;
    }
    if (command_id == Commands::CommandIds::view_toggle_bottom_dock ||
        command_id == Commands::CommandIds::view_terminal_panel ||
        command_id == Commands::CommandIds::view_output ||
        command_id == Commands::CommandIds::view_problems ||
        command_id == Commands::CommandIds::view_diagnostics)
    {
        return toggle_terminal();
    }
    if (command_id == Commands::CommandIds::view_toggle_left_dock)
    {
        const bool vis = !m_tool_sidebar.is_visible();
        m_tool_sidebar.get_model().set_visible(vis);
        return true;
    }
    if (command_id == Commands::CommandIds::view_explorer ||
        command_id == Commands::CommandIds::view_project_panel ||
        command_id == Commands::CommandIds::view_outline_panel)
    {
        m_tool_sidebar.get_model().set_visible(true);
        static_cast<void>(m_tool_sidebar.activate(UI::Editor::SidebarIcon::Project));
        return true;
    }
    if (command_id == Commands::CommandIds::view_search)
    {
        m_tool_sidebar.get_model().set_visible(true);
        static_cast<void>(m_tool_sidebar.activate(UI::Editor::SidebarIcon::Search));
        m_tool_sidebar.get_search_model().set_focused_input(UI::Editor::SearchInputFocus::Search);
        m_tool_sidebar.get_search_model().select_all();
        return true;
    }
    if (command_id == Commands::CommandIds::view_git_panel)
    {
        m_tool_sidebar.get_model().set_visible(true);
        static_cast<void>(m_tool_sidebar.activate(UI::Editor::SidebarIcon::VersionControl));
        m_tool_sidebar.get_source_control_model().refresh_status();
        m_tool_sidebar.get_source_control_model().set_input_focused(true);
        return true;
    }
    if (command_id == Commands::CommandIds::view_debugger_panel)
    {
        m_tool_sidebar.get_model().set_visible(true);
        static_cast<void>(m_tool_sidebar.activate(UI::Editor::SidebarIcon::Run));
        return true;
    }
    if (command_id == Commands::CommandIds::open_plugins)
    {
        m_tool_sidebar.get_model().set_visible(true);
        static_cast<void>(m_tool_sidebar.activate(UI::Editor::SidebarIcon::Services));
        return true;
    }
    if (command_id == Commands::CommandIds::open_settings ||
        command_id == Commands::CommandIds::open_themes ||
        command_id == Commands::CommandIds::more_tools)
    {
        m_tool_sidebar.get_model().set_visible(true);
        static_cast<void>(m_tool_sidebar.activate(UI::Editor::SidebarIcon::More));
        return true;
    }
    const auto res = m_text_editor.handle_command(command_id);
    if (res.has_value() && *res)
    {
        sync_shader_sandbox();
    }
    return res;
}

std::optional<bool> StudioWorkspaceRenderer::is_editor_command_enabled(
    std::string_view command_id) const noexcept
{
    if (command_id == Commands::CommandIds::window_reset_layout ||
        command_id == Commands::CommandIds::view_toggle_right_dock ||
        command_id == "zde.view.shaderPanel" ||
        command_id == "zde.view.shader_sandbox" ||
        command_id == Commands::CommandIds::view_toggle_bottom_dock ||
        command_id == Commands::CommandIds::view_terminal_panel ||
        command_id == Commands::CommandIds::view_output ||
        command_id == Commands::CommandIds::view_problems ||
        command_id == Commands::CommandIds::view_diagnostics ||
        command_id == Commands::CommandIds::view_toggle_left_dock ||
        command_id == Commands::CommandIds::view_explorer ||
        command_id == Commands::CommandIds::view_project_panel ||
        command_id == Commands::CommandIds::view_outline_panel ||
        command_id == Commands::CommandIds::view_search ||
        command_id == Commands::CommandIds::view_git_panel ||
        command_id == Commands::CommandIds::view_debugger_panel ||
        command_id == Commands::CommandIds::open_plugins ||
        command_id == Commands::CommandIds::open_settings ||
        command_id == Commands::CommandIds::open_themes ||
        command_id == Commands::CommandIds::more_tools ||
        command_id == Commands::CommandIds::edit_profiles)
    {
        return true;
    }
    return m_text_editor.is_command_enabled(command_id);
}

bool StudioWorkspaceRenderer::handle_text_input(std::string_view utf8_text)
{
    if (m_tool_sidebar.is_search_focused())
    {
        return m_tool_sidebar.handle_search_text(utf8_text);
    }
    if (m_tool_sidebar.is_source_control_focused())
    {
        return m_tool_sidebar.handle_source_control_text(utf8_text);
    }
    const bool res = m_terminal_panel.is_focused()
        ? m_terminal_panel.handle_text_input(utf8_text)
        : m_text_editor.handle_text_input(utf8_text);
    if (res && !m_terminal_panel.is_focused())
    {
        sync_shader_sandbox();
    }
    return res;
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
    const UI::Editor::StudioEditorLayoutResult layout =
        calculate_layout(client_width, client_height, content_top);
    return m_tool_sidebar.handle_scroll(layout, line_delta);
}

bool StudioWorkspaceRenderer::is_editor_focused() const noexcept
{
    return m_text_editor.is_focused();
}

bool StudioWorkspaceRenderer::is_search_focused() const noexcept
{
    return m_tool_sidebar.is_search_focused();
}

bool StudioWorkspaceRenderer::handle_search_text(std::string_view text)
{
    return m_tool_sidebar.handle_search_text(text);
}

bool StudioWorkspaceRenderer::handle_search_command(UI::Editor::EditorInputCommand cmd, bool extend)
{
    return m_tool_sidebar.handle_search_command(cmd, extend);
}

bool StudioWorkspaceRenderer::handle_search_action(UI::Editor::EditorAction action)
{
    return m_tool_sidebar.handle_search_action(action);
}

bool StudioWorkspaceRenderer::is_source_control_focused() const noexcept
{
    return m_tool_sidebar.is_source_control_focused();
}

bool StudioWorkspaceRenderer::handle_source_control_text(std::string_view text)
{
    return m_tool_sidebar.handle_source_control_text(text);
}

bool StudioWorkspaceRenderer::handle_source_control_command(UI::Editor::EditorInputCommand cmd, bool extend)
{
    return m_tool_sidebar.handle_source_control_command(cmd, extend);
}

bool StudioWorkspaceRenderer::handle_source_control_action(UI::Editor::EditorAction action)
{
    return m_tool_sidebar.handle_source_control_action(action);
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
    const UI::Editor::StudioEditorLayoutResult layout =
        calculate_layout(client_width, client_height, content_top);
    return layout.activity_bar_bounds.contains(point_x, point_y);
}

bool StudioWorkspaceRenderer::is_tab_bar_point(
    float point_x, float point_y,
    int client_width, int client_height,
    float content_top) const noexcept
{
    const UI::Editor::StudioEditorLayoutResult layout =
        calculate_layout(client_width, client_height, content_top);
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
    const UI::Editor::StudioEditorLayoutResult layout =
        calculate_layout(client_width, client_height, content_top);
    return layout.editor_bounds.contains(point_x, point_y) ||
           layout.gutter_bounds.contains(point_x, point_y);
}

bool StudioWorkspaceRenderer::is_scrollbar_point(
    float point_x, float point_y,
    int client_width, int client_height,
    float content_top) const noexcept
{
    const UI::Editor::StudioEditorLayoutResult layout =
        calculate_layout(client_width, client_height, content_top);
    return layout.scrollbar_bounds.contains(point_x, point_y);
}

bool StudioWorkspaceRenderer::is_minimap_point(
    float point_x, float point_y,
    int client_width, int client_height,
    float content_top) const noexcept
{
    const UI::Editor::StudioEditorLayoutResult layout =
        calculate_layout(client_width, client_height, content_top);
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
    const UI::Editor::StudioEditorLayoutResult layout =
        calculate_layout(client_width, client_height, content_top);
    return m_terminal_panel.contains(layout, point_x, point_y);
}

bool StudioWorkspaceRenderer::is_tool_sidebar_point(
    float point_x, float point_y,
    int client_width, int client_height,
    float content_top) const noexcept
{
    const UI::Editor::StudioEditorLayoutResult layout =
        calculate_layout(client_width, client_height, content_top);
    return m_tool_sidebar.contains(layout, point_x, point_y);
}

bool StudioWorkspaceRenderer::is_editor_split_resize_handle(
    float point_x, float point_y,
    int client_width, int client_height,
    float content_top) const noexcept
{
    const UI::Editor::StudioEditorLayoutResult layout =
        calculate_layout(client_width, client_height, content_top);
    return m_text_editor.is_split_resize_handle_point(layout, point_x, point_y);
}

bool StudioWorkspaceRenderer::is_editor_split_resizing() const noexcept
{
    return m_text_editor.is_split_resizing();
}

bool StudioWorkspaceRenderer::is_editor_split_active() const noexcept
{
    return m_text_editor.is_split_active();
}

bool StudioWorkspaceRenderer::is_terminal_resize_handle_point(
    float point_x, float point_y,
    int client_width, int client_height,
    float content_top) const noexcept
{
    const UI::Editor::StudioEditorLayoutResult layout =
        calculate_layout(client_width, client_height, content_top);
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
    const UI::Editor::StudioEditorLayoutResult layout =
        calculate_layout(client_width, client_height, content_top);
    return m_terminal_panel.is_interactive_point(layout, point_x, point_y);
}

bool StudioWorkspaceRenderer::is_sidebar_resize_handle_point(
    float point_x, float point_y,
    int client_width, int client_height,
    float content_top) const noexcept
{
    const UI::Editor::StudioEditorLayoutResult layout =
        calculate_layout(client_width, client_height, content_top);
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

    const float target_left_offset = m_is_fullscreen
        ? 0.0F
        : (UI::Editor::StudioEditorMetrics::titlebar_navigation_width * m_dpi_scale);

    bool titlebar_anim_changed = false;
    if (m_animated_titlebar_left_offset != target_left_offset)
    {
        m_animated_titlebar_left_offset = target_left_offset;
        titlebar_anim_changed = true;
    }

    const bool shader_changed = m_shader_sandbox_panel.tick_animations();
    const bool sidebar_changed = m_tool_sidebar.tick_animations();

    return caret_changed || terminal_changed || terminal_blink_changed || titlebar_anim_changed || shader_changed || sidebar_changed;
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
    const UI::Editor::StudioEditorLayoutResult layout =
        calculate_layout(client_width, client_height, content_top);
    fill_rectangle(context, layout.workspace_bounds, m_colors.workspace_background);
    fill_rectangle(context, layout.tab_bar_bounds, m_colors.tab_background);
    fill_rectangle(context, layout.activity_bar_bounds, m_colors.sidebar_background);
    fill_rectangle(context, layout.tool_sidebar_bounds, m_colors.sidebar_background);
    fill_rectangle(context, layout.editor_header_bounds, m_colors.editor_background);
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
    m_shader_sandbox_panel.render(*this, context, layout);
    m_activity_sidebar.render(*this, context, layout);
    if (const UI::Editor::TextDocumentModel* document = m_text_editor.get_document())
    {
        m_footer_toolbar.render(
            *this, context, layout, document->get_full_breadcrumbs(), document->get_status());
    }

    if (m_explorer_context_menu.visible)
    {
        render_explorer_context_menu(context, layout);
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

// --- Explorer Context Menu Implementation ---

void StudioWorkspaceRenderer::show_explorer_context_menu(
    const std::filesystem::path& target_path, float client_x, float client_y)
{
    m_explorer_context_menu.visible = true;
    m_explorer_context_menu.target_path = target_path;
    m_explorer_context_menu.hovered_index.reset();

    m_explorer_context_menu.items = {
        {"New File...", "zde.explorer.newFile", false, ""},
        {"New Folder...", "zde.explorer.newFolder", false, ""},
        {"", "", true, ""},
        {"Open to the Side", "zde.explorer.openToSide", false, "Cmd+Enter"},
        {"Reveal in Finder", "zde.explorer.reveal", false, "Shift+Alt+R"},
        {"Open in Integrated Terminal", "zde.explorer.openTerminal", false, ""},
        {"", "", true, ""},
        {"Cut", "zde.explorer.cut", false, "Cmd+X"},
        {"Copy", "zde.explorer.copy", false, "Cmd+C"},
        {"Paste", "zde.explorer.paste", false, "Cmd+V"},
        {"", "", true, ""},
        {"Copy Path", "zde.explorer.copyPath", false, "Shift+Alt+C"},
        {"Copy Relative Path", "zde.explorer.copyRelativePath", false, "Cmd+K Cmd+Shift+C"},
        {"", "", true, ""},
        {"Rename...", "zde.explorer.rename", false, "Enter"},
        {"Delete", "zde.explorer.delete", false, "Cmd+Backspace"}
    };

    const float scale = m_dpi_scale;
    const float row_height = 26.0F * scale;
    const float sep_height = 8.0F * scale;
    float total_h = 8.0F * scale;
    float popup_width = 240.0F * scale;

    for (const auto& item : m_explorer_context_menu.items) {
        if (item.separator) {
            total_h += sep_height;
        } else {
            total_h += row_height;
            float w = static_cast<float>(item.label.size()) * 7.5F * scale + 48.0F * scale;
            if (!item.shortcut.empty()) {
                w += static_cast<float>(item.shortcut.size()) * 7.5F * scale + 36.0F * scale;
            }
            popup_width = std::max(popup_width, w);
        }
    }
    popup_width = std::min(popup_width, 380.0F * scale);

    float menu_x = client_x;
    float menu_y = client_y;

    m_explorer_context_menu.bounds = {menu_x, menu_y, popup_width, total_h};
    m_explorer_context_menu.item_bounds.clear();

    float curr_y = menu_y + 4.0F * scale;
    for (const auto& item : m_explorer_context_menu.items) {
        if (item.separator) {
            m_explorer_context_menu.item_bounds.push_back({menu_x, curr_y, popup_width, sep_height});
            curr_y += sep_height;
        } else {
            m_explorer_context_menu.item_bounds.push_back({menu_x, curr_y, popup_width, row_height});
            curr_y += row_height;
        }
    }
}

void StudioWorkspaceRenderer::close_explorer_context_menu() noexcept
{
    m_explorer_context_menu.visible = false;
    m_explorer_context_menu.hovered_index.reset();
}

void StudioWorkspaceRenderer::execute_explorer_context_menu_item(std::size_t item_index)
{
    if (item_index >= m_explorer_context_menu.items.size()) return;
    const auto& item = m_explorer_context_menu.items[item_index];
    if (item.separator) return;

    const auto target_path = m_explorer_context_menu.target_path;
    close_explorer_context_menu();
    execute_explorer_command(item.command_id, target_path);
}

void StudioWorkspaceRenderer::execute_explorer_command(
    std::string_view command_id, const std::filesystem::path& target_path)
{
    if (command_id == "zde.explorer.newFile") {
        m_prompt_dialog.open_new_file(target_path, [this](const std::string& name, const std::string& content) {
            std::filesystem::path created_p;
            if (m_tool_sidebar.get_model().create_file(name, created_p)) {
                if (!content.empty()) {
                    std::ofstream out(created_p, std::ios::binary);
                    if (out.is_open()) {
                        out.write(content.data(), content.size());
                        out.close();
                    }
                }
                static_cast<void>(m_text_editor.open_file(created_p));
            }
        });
    } else if (command_id == "zde.explorer.newFolder") {
        m_prompt_dialog.open_new_folder(target_path, [this](const std::string& name) {
            std::filesystem::path created_p;
            static_cast<void>(m_tool_sidebar.get_model().create_directory(name, created_p));
        });
    } else if (command_id == "zde.explorer.openToSide") {
        if (!target_path.empty() && !std::filesystem::is_directory(target_path)) {
            static_cast<void>(m_text_editor.open_file(target_path));
        }
    } else if (command_id == "zde.explorer.reveal") {
        if (!target_path.empty()) {
            NSString* p = [NSString stringWithUTF8String:target_path.c_str()];
            [[NSWorkspace sharedWorkspace] selectFile:p inFileViewerRootedAtPath:@""];
        }
    } else if (command_id == "zde.explorer.openTerminal") {
        const std::filesystem::path term_dir = std::filesystem::is_directory(target_path) ? target_path : target_path.parent_path();
        m_terminal_panel.set_working_directory(term_dir);
        if (!m_terminal_panel.is_visible()) {
            static_cast<void>(m_terminal_panel.toggle());
        }
    } else if (command_id == "zde.explorer.copyPath") {
        if (!target_path.empty()) {
            NSPasteboard* pb = [NSPasteboard generalPasteboard];
            [pb clearContents];
            [pb setString:[NSString stringWithUTF8String:target_path.c_str()] forType:NSPasteboardTypeString];
        }
    } else if (command_id == "zde.explorer.copyRelativePath") {
        if (!target_path.empty()) {
            const auto root = m_tool_sidebar.get_model().get_workspace_root();
            std::error_code ec;
            const auto rel = std::filesystem::relative(target_path, root, ec);
            NSPasteboard* pb = [NSPasteboard generalPasteboard];
            [pb clearContents];
            [pb setString:[NSString stringWithUTF8String:rel.c_str()] forType:NSPasteboardTypeString];
        }
    } else if (command_id == "zde.explorer.rename") {
        m_prompt_dialog.open_rename(target_path, [this, target_path](const std::string& new_name) {
            std::filesystem::path out_p;
            static_cast<void>(m_tool_sidebar.get_model().rename_item(target_path, new_name, out_p));
        });
    } else if (command_id == "zde.explorer.delete") {
        m_prompt_dialog.open_delete(target_path, [this, target_path]() {
            static_cast<void>(m_tool_sidebar.get_model().delete_item(target_path));
        });
    }
}

void StudioWorkspaceRenderer::render_explorer_context_menu(
    CGContextRef context, const UI::Editor::StudioEditorLayoutResult&) const
{
    if (!m_explorer_context_menu.visible) return;

    const float scale = m_dpi_scale;
    const auto& bounds = m_explorer_context_menu.bounds;
    const CGFloat card_bg[4] = {0.11F, 0.11F, 0.14F, 0.98F};      // #1d1d23
    const CGFloat card_border[4] = {0.24F, 0.24F, 0.28F, 1.0F};  // #3c3c46
    const CGFloat hover_bg[4] = {0.0F, 0.48F, 0.80F, 1.0F};      // #007acc

    fill_rounded_rectangle(context, bounds, card_bg, 6.0F * scale);
    draw_rectangle(context, bounds, card_border);

    for (std::size_t i = 0; i < m_explorer_context_menu.items.size() && i < m_explorer_context_menu.item_bounds.size(); ++i) {
        const auto& item = m_explorer_context_menu.items[i];
        const auto& item_bounds = m_explorer_context_menu.item_bounds[i];

        if (item.separator) {
            draw_line(context,
                round_to_int(item_bounds.x + 8.0F * scale),
                round_to_int(item_bounds.y + item_bounds.height * 0.5F),
                round_to_int(item_bounds.right() - 8.0F * scale),
                round_to_int(item_bounds.y + item_bounds.height * 0.5F),
                card_border);
            continue;
        }

        const bool hovered = (m_explorer_context_menu.hovered_index && *m_explorer_context_menu.hovered_index == i);
        if (hovered) {
            UI::Rect h_rect = item_bounds;
            h_rect.x += 4.0F * scale;
            h_rect.width -= 8.0F * scale;
            h_rect.y += 1.0F * scale;
            h_rect.height -= 2.0F * scale;
            fill_rounded_rectangle(context, h_rect, hover_bg, 4.0F * scale);
        }

        draw_text(context, *m_small_font, item.label,
            item_bounds.x + 14.0F * scale,
            item_bounds.y + item_bounds.height * 0.5F,
            hovered ? "#ffffff" : m_text.primary);

        if (!item.shortcut.empty()) {
            const int sc_w = m_small_font->getTextWidth(item.shortcut);
            draw_text(context, *m_small_font, item.shortcut,
                item_bounds.right() - 14.0F * scale - static_cast<float>(sc_w),
                item_bounds.y + item_bounds.height * 0.5F,
                hovered ? "#ffffff" : m_text.muted);
        }
    }
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
    if (path.empty())
    {
        return {};
    }

    std::error_code path_error;
    std::filesystem::path input_path{path};
    if (input_path.is_absolute() && std::filesystem::is_regular_file(input_path, path_error))
    {
        return input_path;
    }

    if (m_icon_asset_root.empty())
    {
        return {};
    }

    std::string rel_str = input_path.generic_string();
    constexpr std::string_view p1 = "Assets/icons/";
    constexpr std::string_view p2 = "Resources/icons/";
    constexpr std::string_view p3 = "Assets/";
    constexpr std::string_view p4 = "Resources/";
    if (rel_str.starts_with(p1))
    {
        rel_str.erase(0, p1.size());
    }
    else if (rel_str.starts_with(p2))
    {
        rel_str.erase(0, p2.size());
    }
    else if (rel_str.starts_with(p3))
    {
        rel_str.erase(0, p3.size());
    }
    else if (rel_str.starts_with(p4))
    {
        rel_str.erase(0, p4.size());
    }

    std::string filename = std::filesystem::path{rel_str}.filename().string();
    if (filename == "source-control.svg" || filename == "branch.svg" || filename == "git.svg")
    {
        filename = "git-branch.svg";
    }
    else if (filename == "extensions.svg")
    {
        filename = "puzzle.svg";
    }
    else if (filename == "settings.svg")
    {
        filename = "gear.svg";
    }
    else if (filename == "folder-opened.svg")
    {
        filename = "folder-open.svg";
    }

    // 1. Direct path under m_icon_asset_root (e.g. vscode-symbols/icons/files/cplus.svg)
    const std::filesystem::path direct_path = m_icon_asset_root / rel_str;
    if (std::filesystem::is_regular_file(direct_path, path_error))
    {
        return direct_path;
    }

    // 1b. Handle vscode-symbols path normalization
    if (rel_str.starts_with("vscode-symbols/files/"))
    {
        std::string sub = rel_str;
        sub.replace(0, std::string_view("vscode-symbols/files/").size(), "vscode-symbols/icons/files/");
        const std::filesystem::path p = m_icon_asset_root / sub;
        if (std::filesystem::is_regular_file(p, path_error))
        {
            return p;
        }
    }
    if (rel_str.starts_with("vscode-symbols/folders/"))
    {
        std::string sub = rel_str;
        sub.replace(0, std::string_view("vscode-symbols/folders/").size(), "vscode-symbols/icons/folders/");
        const std::filesystem::path p = m_icon_asset_root / sub;
        if (std::filesystem::is_regular_file(p, path_error))
        {
            return p;
        }
    }

    // 2. Direct filename under m_icon_asset_root (e.g. folder.svg, git-branch.svg)
    const std::filesystem::path root_file = m_icon_asset_root / filename;
    if (std::filesystem::is_regular_file(root_file, path_error))
    {
        return root_file;
    }

    // 3. material-icon-theme subdirectory (e.g. shader.svg, cpp.svg)
    const std::filesystem::path material_file = m_icon_asset_root / "material-icon-theme" / filename;
    if (std::filesystem::is_regular_file(material_file, path_error))
    {
        return material_file;
    }

    // 4. vscode-symbols subdirectories (with and without icons/)
    const std::filesystem::path symbol_file = m_icon_asset_root / "vscode-symbols" / "icons" / "files" / filename;
    if (std::filesystem::is_regular_file(symbol_file, path_error))
    {
        return symbol_file;
    }
    const std::filesystem::path symbol_folder = m_icon_asset_root / "vscode-symbols" / "icons" / "folders" / filename;
    if (std::filesystem::is_regular_file(symbol_folder, path_error))
    {
        return symbol_folder;
    }
    const std::filesystem::path symbol_file_legacy = m_icon_asset_root / "vscode-symbols" / "files" / filename;
    if (std::filesystem::is_regular_file(symbol_file_legacy, path_error))
    {
        return symbol_file_legacy;
    }

    // 5. vscode-codicons subdirectory
    const std::filesystem::path codicon_file = m_icon_asset_root / "vscode-codicons" / "icons" / filename;
    if (std::filesystem::is_regular_file(codicon_file, path_error))
    {
        return codicon_file;
    }

    // 6. vscode-icons subdirectory
    const std::filesystem::path vsicon_file = m_icon_asset_root / "vscode-icons" / "icons" / filename;
    if (std::filesystem::is_regular_file(vsicon_file, path_error))
    {
        return vsicon_file;
    }

    // 7. Final fallback check on original path
    if (std::filesystem::is_regular_file(input_path, path_error))
    {
        return input_path;
    }

    return {};
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
    preserve_source_colors =
        preserve_source_colors &&
        (resolved_path.parent_path().filename() == "material-icon-theme" ||
         resolved_path.string().find("material-icon-theme") != std::string::npos ||
         resolved_path.string().find("vscode-symbols") != std::string::npos ||
         resolved_path.string().find("vscode-icons") != std::string::npos);

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
        if (m_image_cache.size() >= 128)
        {
            for (auto& [k, img] : m_image_cache)
            {
                if (img) CGImageRelease(img);
            }
            m_image_cache.clear();
        }
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
        const auto* src32 = reinterpret_cast<const std::uint32_t*>(bitmap.data());

        // lunasvg stores ARGB32 uint32_t ((a << 24) | (r << 16) | (g << 8) | b).
        // CoreGraphics with kCGBitmapByteOrder32Big | kCGImageAlphaPremultipliedLast
        // expects byte-order [R, G, B, A].
        std::vector<std::uint8_t> converted_data(static_cast<std::size_t>(width * height * 4));
        for (std::uint32_t i = 0; i < width * height; ++i)
        {
            const std::uint32_t pixel = src32[i];
            const std::uint8_t alpha = static_cast<std::uint8_t>((pixel >> 24U) & 0xFFU);
            if (alpha == 0)
            {
                converted_data[i * 4 + 0] = 0;
                converted_data[i * 4 + 1] = 0;
                converted_data[i * 4 + 2] = 0;
                converted_data[i * 4 + 3] = 0;
                continue;
            }
            if (preserve_source_colors)
            {
                const std::uint8_t src_red = static_cast<std::uint8_t>((pixel >> 16U) & 0xFFU);
                const std::uint8_t src_green = static_cast<std::uint8_t>((pixel >> 8U) & 0xFFU);
                const std::uint8_t src_blue = static_cast<std::uint8_t>(pixel & 0xFFU);
                converted_data[i * 4 + 0] = src_red;
                converted_data[i * 4 + 1] = src_green;
                converted_data[i * 4 + 2] = src_blue;
                converted_data[i * 4 + 3] = alpha;
            }
            else
            {
                const float a = static_cast<float>(alpha) / 255.0F;
                converted_data[i * 4 + 0] = static_cast<std::uint8_t>(static_cast<float>(color.red) * a);
                converted_data[i * 4 + 1] = static_cast<std::uint8_t>(static_cast<float>(color.green) * a);
                converted_data[i * 4 + 2] = static_cast<std::uint8_t>(static_cast<float>(color.blue) * a);
                converted_data[i * 4 + 3] = alpha;
            }
        }

        CGColorSpaceRef color_space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        CGContextRef bitmap_context = CGBitmapContextCreate(
            converted_data.data(),
            width, height, 8, width * 4, color_space,
            static_cast<uint32_t>(kCGImageAlphaPremultipliedLast) | kCGBitmapByteOrder32Big);
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
