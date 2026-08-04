#include "Platform/X11/Components/StudioWorkspaceRenderer.h"
#include "Utility/IcoDecoder.h"
#include "Utility/stb_image.h"

#include "UI/Editor/EditorFileSystem.h"
#include "Utility/Fonts.h"
#include "Utility/X11Rounded.h"
#include <lunasvg.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fontconfig/fontconfig.h>

namespace Zenvra::Platform::X11::Components
{

namespace
{

int round_to_int(float value)
{
    return static_cast<int>(std::lround(value));
}

std::string to_xft_color(const UI::Theme::Color& color)
{
    char value[8]{};
    std::snprintf(
        value,
        sizeof(value),
        "#%02x%02x%02x",
        static_cast<unsigned int>(color.red),
        static_cast<unsigned int>(color.green),
        static_cast<unsigned int>(color.blue));
    return value;
}

} // namespace

StudioWorkspaceRenderer::StudioWorkspaceRenderer() = default;

StudioWorkspaceRenderer::~StudioWorkspaceRenderer()
{
    shutdown();
}

bool StudioWorkspaceRenderer::initialize(Display* display, int screen, float dpi_scale)
{
    shutdown();
    m_display = display;
    m_screen = screen;
    m_dpi_scale = std::max(dpi_scale, 0.5F);
    if (m_display == nullptr)
    {
        return false;
    }

    std::error_code path_error;
    const std::filesystem::path current_path = std::filesystem::current_path(path_error);
    std::optional<std::filesystem::path> project_root;
    if (!path_error)
    {
        project_root = UI::Editor::EditorFileSystem::find_project_root(current_path);
    }
    if (!project_root)
    {
        path_error.clear();
        const std::filesystem::path executable_path =
            std::filesystem::read_symlink("/proc/self/exe", path_error);
        if (!path_error)
        {
            project_root = UI::Editor::EditorFileSystem::find_project_root(executable_path);
        }
    }
    if (project_root)
    {
        m_icon_asset_root = *project_root / "Assets" / "icons";
    }
    m_graphics_context = XCreateGC(m_display, RootWindow(m_display, m_screen), 0, nullptr);
    if (m_graphics_context == nullptr)
    {
        shutdown();
        return false;
    }

    // Load bundled fonts from Assets/fonts/. For each font, register the TTF
    // file with Fontconfig when found (and not a placeholder). If not found or
    // the file is invalid, fall back to the system default family.
    bool hack_loaded = false;
    bool opensans_loaded = false;
    if (project_root)
    {
        const std::filesystem::path hack_ttf =
            *project_root / "Assets" / "fonts" / "Hack" / "ttf" / "Hack-Regular.ttf";
        const std::filesystem::path opensans_ttf =
            *project_root / "Assets" / "fonts" / "OpenSans" / "OpenSans-Regular.ttf";
        std::error_code size_error;

        // Hack – editor / minimap / terminal font
        if (std::filesystem::exists(hack_ttf) &&
            std::filesystem::file_size(hack_ttf, size_error) > 100)
        {
            hack_loaded = FcConfigAppFontAddFile(nullptr,
                reinterpret_cast<const FcChar8*>(hack_ttf.c_str()));
        }

        // Open Sans – UI / sidebar / tab / large title font
        if (std::filesystem::exists(opensans_ttf) &&
            std::filesystem::file_size(opensans_ttf, size_error) > 100)
        {
            opensans_loaded = FcConfigAppFontAddFile(nullptr,
                reinterpret_cast<const FcChar8*>(opensans_ttf.c_str()));
        }
    }

    const char* editor_font_family  = hack_loaded     ? "Hack"      : "monospace";
    const char* ui_font_family      = opensans_loaded  ? "Open Sans" : "sans";

    char ui_pattern[128]{};
    char small_pattern[128]{};
    char editor_pattern[128]{};
    char minimap_pattern[128]{};
    char large_pattern[128]{};
    std::snprintf(ui_pattern, sizeof(ui_pattern),
        "%s:pixelsize=%d:antialias=true:hinting=true",
        ui_font_family,
        std::max(round_to_int(12.0F * m_dpi_scale), 9));
    std::snprintf(small_pattern, sizeof(small_pattern),
        "%s:pixelsize=%d:antialias=true:hinting=true",
        ui_font_family,
        std::max(round_to_int(12.0F * m_dpi_scale), 9));
    std::snprintf(editor_pattern, sizeof(editor_pattern),
        "%s:pixelsize=%d:antialias=true:hinting=true",
        editor_font_family,
        std::max(round_to_int(14.0F * m_dpi_scale), 10));
    std::snprintf(minimap_pattern, sizeof(minimap_pattern),
        "%s:pixelsize=%d:antialias=false:hinting=false",
        editor_font_family,
        std::max(round_to_int(3.0F * m_dpi_scale), 3));
    std::snprintf(large_pattern, sizeof(large_pattern),
        "%s:pixelsize=%d:antialias=true:hinting=true:weight=bold",
        ui_font_family,
        std::max(round_to_int(24.0F * m_dpi_scale), 18));
    m_ui_font = std::make_unique<AntialiasedFont>(m_display, m_screen, ui_pattern);
    m_small_font = std::make_unique<AntialiasedFont>(m_display, m_screen, small_pattern);
    m_editor_font = std::make_unique<AntialiasedFont>(m_display, m_screen, editor_pattern);
    m_minimap_font = std::make_unique<AntialiasedFont>(
        m_display, m_screen, minimap_pattern);
    m_large_font = std::make_unique<AntialiasedFont>(m_display, m_screen, large_pattern);
    if (m_ui_font->getHeight() <= 0 || m_small_font->getHeight() <= 0 ||
        m_editor_font->getHeight() <= 0 || m_minimap_font->getHeight() <= 0 || m_large_font->getHeight() <= 0)
    {
        shutdown();
        return false;
    }

    m_pixels.workspace_background = allocate_color(m_palette.workspace_background);
    m_pixels.tab_background = allocate_color(m_palette.tab_background);
    m_pixels.tab_active_background = allocate_color(m_palette.tab_active_background);
    m_pixels.sidebar_background = allocate_color(m_palette.sidebar_background);
    m_pixels.editor_background = allocate_color(m_palette.editor_background);
    m_pixels.active_line_background = allocate_color(m_palette.active_line_background);
    m_pixels.selection_background = allocate_color(m_palette.selection_background);
    m_pixels.status_background = allocate_color(m_palette.status_background);
    m_pixels.border = allocate_color(m_palette.border);
    m_pixels.text_primary = allocate_color(m_palette.text_primary);
    m_pixels.text_muted = allocate_color(m_palette.text_muted);
    m_pixels.accent = allocate_color(m_palette.accent);
    m_pixels.warning = allocate_color(m_palette.warning);
    m_pixels.success = allocate_color(m_palette.success);
    m_text.primary = to_xft_color(m_palette.text_primary);
    m_text.muted = to_xft_color(m_palette.text_muted);
    m_text.keyword = to_xft_color(m_palette.keyword);
    m_text.number = to_xft_color(m_palette.number);
    m_text.label = to_xft_color(m_palette.label);
    m_text.type = to_xft_color(m_palette.type);
    m_text.comment = to_xft_color(m_palette.comment);
    m_text.accent = to_xft_color(m_palette.accent);
    m_text.warning = to_xft_color(m_palette.warning);
    m_text.success = to_xft_color(m_palette.success);
    static_cast<void>(m_tool_sidebar.initialize());
    static_cast<void>(m_terminal_panel.toggle());
    m_terminal_panel.set_focused(false);
    return true;
}

bool StudioWorkspaceRenderer::open_file(const std::filesystem::path& path)
{
    return m_text_editor.open_file(path);
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

bool StudioWorkspaceRenderer::handle_pointer_press(
    float point_x,
    float point_y,
    int client_width,
    int client_height,
    float content_top,
    bool extend_selection,
    Time event_time,
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
    if (m_tool_sidebar.handle_pointer_press(
            layout, point_x, point_y, sidebar_file))
    {
        m_terminal_panel.set_focused(false);
        if (sidebar_file)
        {
            if (sidebar_file->string() == "::OPEN_FOLDER::") {
                command_out = "zde.project.open";
            } else {
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
        *this, layout, point_x, point_y, extend_selection, command_out);
}

bool StudioWorkspaceRenderer::handle_pointer_move(
    float point_x,
    float point_y,
    int client_width,
    int client_height,
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
    float point_x,
    float point_y,
    int client_width,
    int client_height,
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
    if (m_terminal_panel.is_resizing())
    {
        return m_terminal_panel.handle_pointer_drag(layout, point_y);
    }
    return m_text_editor.handle_pointer_drag(*this, layout, point_x, point_y);
}

bool StudioWorkspaceRenderer::handle_pointer_release() noexcept
{
    const bool terminal_changed = m_terminal_panel.handle_pointer_release();
    const bool editor_changed = m_text_editor.handle_pointer_release();
    return terminal_changed || editor_changed;
}

bool StudioWorkspaceRenderer::handle_scroll(
    std::ptrdiff_t line_delta,
    int client_width,
    int client_height,
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
    return m_text_editor.handle_scroll(*this, layout, line_delta);
}

bool StudioWorkspaceRenderer::handle_editor_input(
    UI::Editor::EditorInputCommand command,
    bool extend_selection)
{
    return m_text_editor.handle_input(command, extend_selection);
}

bool StudioWorkspaceRenderer::handle_editor_action(UI::Editor::EditorAction action)
{
    return m_text_editor.handle_action(action);
}

std::optional<bool> StudioWorkspaceRenderer::handle_editor_command(
    std::string_view command_id)
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
    return m_terminal_panel.is_focused()
        ? m_terminal_panel.handle_text_input(utf8_text)
        : m_text_editor.handle_text_input(utf8_text);
}

bool StudioWorkspaceRenderer::handle_terminal_key(Terminal::TerminalInputKey key)
{
    return m_terminal_panel.handle_key(key);
}

bool StudioWorkspaceRenderer::handle_terminal_control(char letter)
{
    return m_terminal_panel.handle_control(letter);
}

bool StudioWorkspaceRenderer::handle_terminal_scroll(std::ptrdiff_t line_delta) noexcept
{
    return m_terminal_panel.handle_scroll(line_delta);
}

bool StudioWorkspaceRenderer::handle_tool_sidebar_scroll(
    std::ptrdiff_t line_delta,
    int client_width,
    int client_height,
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
    return !m_terminal_panel.is_focused() && m_text_editor.is_focused();
}

bool StudioWorkspaceRenderer::is_terminal_focused() const noexcept
{
    return m_terminal_panel.is_focused();
}

bool StudioWorkspaceRenderer::is_activity_bar_point(
    float point_x,
    float point_y,
    int client_width,
    int client_height,
    float content_top) const noexcept
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
    return layout.activity_bar_bounds.contains(point_x, point_y);
}

bool StudioWorkspaceRenderer::is_tab_bar_point(
    float point_x,
    float point_y,
    int client_width,
    int client_height,
    float content_top) const noexcept
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
    if (m_ui_font == nullptr)
    {
        return false;
    }
    return m_text_editor.is_tab_interactive_point(*this, layout, point_x, point_y);
}

bool StudioWorkspaceRenderer::is_editor_point(
    float point_x,
    float point_y,
    int client_width,
    int client_height,
    float content_top) const noexcept
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
    return (layout.gutter_bounds.contains(point_x, point_y) ||
        layout.editor_bounds.contains(point_x, point_y)) &&
        !layout.minimap_bounds.contains(point_x, point_y) &&
        !layout.scrollbar_bounds.contains(point_x, point_y);
}

bool StudioWorkspaceRenderer::is_scrollbar_point(
    float point_x,
    float point_y,
    int client_width,
    int client_height,
    float content_top) const noexcept
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
    return m_text_editor.is_scrollbar_point(layout, point_x, point_y);
}

bool StudioWorkspaceRenderer::is_minimap_point(
    float point_x,
    float point_y,
    int client_width,
    int client_height,
    float content_top) const noexcept
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
    return m_text_editor.is_minimap_point(layout, point_x, point_y);
}

bool StudioWorkspaceRenderer::is_terminal_point(
    float point_x,
    float point_y,
    int client_width,
    int client_height,
    float content_top) const noexcept
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
    return m_terminal_panel.contains(layout, point_x, point_y);
}

bool StudioWorkspaceRenderer::is_tool_sidebar_point(
    float point_x,
    float point_y,
    int client_width,
    int client_height,
    float content_top) const noexcept
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
    return m_tool_sidebar.contains(layout, point_x, point_y);
}

bool StudioWorkspaceRenderer::is_terminal_resize_handle_point(
    float point_x,
    float point_y,
    int client_width,
    int client_height,
    float content_top) const noexcept
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
    return m_terminal_panel.is_resize_handle_point(layout, point_x, point_y);
}

bool StudioWorkspaceRenderer::is_terminal_resizing() const noexcept
{
    return m_terminal_panel.is_resizing();
}

bool StudioWorkspaceRenderer::is_empty_state_button_hovered() const noexcept
{
    return m_text_editor.is_empty_state_button_hovered();
}

bool StudioWorkspaceRenderer::tick_animations() noexcept
{
    const bool caret_changed = m_text_editor.tick_animations();
    const bool terminal_changed = m_terminal_panel.poll();
    return caret_changed || terminal_changed;
}

void StudioWorkspaceRenderer::shutdown()
{
    m_terminal_panel.shutdown();
    m_minimap_font.reset();
    m_editor_font.reset();
    m_small_font.reset();
    m_ui_font.reset();
    m_large_font.reset();
    if (m_display != nullptr && m_graphics_context != nullptr)
    {
        XFreeGC(m_display, m_graphics_context);
    }
    for (auto& [path, image] : m_svg_cache)
    {
        static_cast<void>(path);
        if (image)
        {
            XDestroyImage(image);
        }
    }
    m_svg_cache.clear();
    m_icon_asset_root.clear();
    m_graphics_context = nullptr;
    m_display = nullptr;
}

void StudioWorkspaceRenderer::render(
    Drawable drawable,
    int client_width,
    int client_height,
    float content_top) const
{
    if (m_display == nullptr || m_graphics_context == nullptr || drawable == 0 ||
        m_ui_font == nullptr || m_small_font == nullptr || m_editor_font == nullptr ||
        m_minimap_font == nullptr || m_large_font == nullptr)
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
    fill_rectangle(drawable, layout.workspace_bounds, m_pixels.workspace_background);
    fill_rectangle(drawable, layout.tab_bar_bounds, m_pixels.tab_background);
    fill_rectangle(drawable, layout.activity_bar_bounds, m_pixels.sidebar_background);
    fill_rectangle(drawable, layout.tool_sidebar_bounds, m_pixels.sidebar_background);
    fill_rectangle(drawable, layout.gutter_bounds, m_pixels.editor_background);
    fill_rectangle(drawable, layout.editor_bounds, m_pixels.editor_background);
    fill_rectangle(drawable, layout.status_bar_bounds, m_pixels.status_background);

    m_text_editor.render(*this, drawable, layout);
    m_terminal_panel.render(*this, drawable, layout);
    m_tool_sidebar.render(*this, drawable, layout);
    m_activity_sidebar.render(*this, drawable, layout);
    if (const UI::Editor::TextDocumentModel* document = m_text_editor.get_document())
    {
        m_footer_toolbar.render(
            *this,
            drawable,
            layout,
            document->get_breadcrumbs(),
            document->get_status());
    }
}

const std::filesystem::path& StudioWorkspaceRenderer::get_icon_asset_root() const noexcept
{
    return m_icon_asset_root;
}

unsigned long StudioWorkspaceRenderer::allocate_color(const UI::Theme::Color& color) const
{
    XColor x_color{};
    x_color.red = static_cast<unsigned short>(color.red * 257U);
    x_color.green = static_cast<unsigned short>(color.green * 257U);
    x_color.blue = static_cast<unsigned short>(color.blue * 257U);
    x_color.flags = DoRed | DoGreen | DoBlue;
    if (XAllocColor(m_display, DefaultColormap(m_display, m_screen), &x_color) == 0)
    {
        return BlackPixel(m_display, m_screen);
    }
    return x_color.pixel;
}

void StudioWorkspaceRenderer::fill_rectangle(
    Drawable drawable,
    const UI::Rect& rectangle,
    unsigned long color) const
{
    if (rectangle.is_empty())
    {
        return;
    }
    XSetForeground(m_display, m_graphics_context, color);
    XFillRectangle(m_display, drawable, m_graphics_context,
        round_to_int(rectangle.x), round_to_int(rectangle.y),
        static_cast<unsigned int>(std::max(round_to_int(rectangle.width), 0)),
        static_cast<unsigned int>(std::max(round_to_int(rectangle.height), 0)));
}

void StudioWorkspaceRenderer::fill_rounded_rectangle(
    Drawable drawable,
    const UI::Rect& rectangle,
    unsigned long color,
    float radius) const
{
    if (rectangle.is_empty())
    {
        return;
    }
    XSetForeground(m_display, m_graphics_context, color);
    Utility::X11Rounded::X11Rounded::fillRoundedRect(
        m_display,
        drawable,
        m_graphics_context,
        round_to_int(rectangle.x),
        round_to_int(rectangle.y),
        std::max(round_to_int(rectangle.width), 0),
        std::max(round_to_int(rectangle.height), 0),
        std::max(round_to_int(radius), 0));
}

void StudioWorkspaceRenderer::draw_rectangle(
    Drawable drawable,
    const UI::Rect& rectangle,
    unsigned long color) const
{
    if (rectangle.is_empty())
    {
        return;
    }
    XSetForeground(m_display, m_graphics_context, color);
    XDrawRectangle(m_display, drawable, m_graphics_context,
        round_to_int(rectangle.x), round_to_int(rectangle.y),
        static_cast<unsigned int>(std::max(round_to_int(rectangle.width) - 1, 0)),
        static_cast<unsigned int>(std::max(round_to_int(rectangle.height) - 1, 0)));
}

void StudioWorkspaceRenderer::draw_line(
    Drawable drawable,
    int from_x,
    int from_y,
    int to_x,
    int to_y,
    unsigned long color) const
{
    XSetForeground(m_display, m_graphics_context, color);
    XDrawLine(m_display, drawable, m_graphics_context, from_x, from_y, to_x, to_y);
}

void StudioWorkspaceRenderer::draw_text(
    Drawable drawable,
    AntialiasedFont& font,
    std::string_view text,
    float point_x,
    float center_y,
    const std::string& color) const
{
    if (text.empty())
    {
        return;
    }
    const int baseline = round_to_int(
        center_y - static_cast<float>(font.getAscent() + font.getDescent()) * 0.5F +
        static_cast<float>(font.getAscent()));
    font.drawString(drawable, color, round_to_int(point_x), baseline, std::string{text});
}

void StudioWorkspaceRenderer::draw_svg_icon(
    Drawable drawable,
    const std::string& path,
    int center_x,
    int center_y,
    int size,
    const UI::Theme::Color& color,
    const UI::Theme::Color& background,
    bool preserve_source_colors) const
{
    if (size <= 0 || m_display == nullptr || m_graphics_context == nullptr)
    {
        return;
    }

    std::error_code path_error;
    std::filesystem::path resolved_path{path};
    if (resolved_path.is_relative() && !m_icon_asset_root.empty())
    {
        const std::filesystem::path themed_path = m_icon_asset_root / resolved_path;
        if (std::filesystem::is_regular_file(themed_path, path_error))
        {
            resolved_path = themed_path;
        }
        else
        {
            // Keep compatibility with callers that pass Assets/icons/foo.svg.
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
        return;
    }
    preserve_source_colors = preserve_source_colors &&
        resolved_path.parent_path().filename() == "material-icon-theme";

    const int half = size / 2;
    const int draw_x = center_x - half;
    const int draw_y = center_y - half;

    const std::string resolved_string = resolved_path.string();
    const std::string cache_key = resolved_string + "@" + std::to_string(size) + "#" +
        to_xft_color(color) + "/" + to_xft_color(background);
    XImage* image = nullptr;
    auto it = m_svg_cache.find(cache_key);
    if (it != m_svg_cache.end())
    {
        image = it->second;
    }
    else
    {
        auto document = lunasvg::Document::loadFromFile(resolved_string);
        if (!document)
        {
            return;
        }

        auto bitmap = document->renderToBitmap(static_cast<std::uint32_t>(size), static_cast<std::uint32_t>(size));
        if (bitmap.isNull())
        {
            return;
        }

        char* x11_data = static_cast<char*>(std::malloc(size * size * 4));
        if (!x11_data)
        {
            return;
        }

        const uint32_t bg_r = static_cast<uint32_t>(background.red);
        const uint32_t bg_g = static_cast<uint32_t>(background.green);
        const uint32_t bg_b = static_cast<uint32_t>(background.blue);

        const uint32_t tint_r = static_cast<uint32_t>(color.red);
        const uint32_t tint_g = static_cast<uint32_t>(color.green);
        const uint32_t tint_b = static_cast<uint32_t>(color.blue);

        const uint32_t* src = reinterpret_cast<const uint32_t*>(bitmap.data());
        uint32_t* dst = reinterpret_cast<uint32_t*>(x11_data);

        for (int i = 0; i < size * size; ++i)
        {
            uint32_t pixel = src[i];
            uint32_t a = (pixel >> 24) & 0xFF;

            const uint32_t source_r = (pixel >> 16) & 0xFF;
            const uint32_t source_g = (pixel >> 8) & 0xFF;
            const uint32_t source_b = pixel & 0xFF;
            const uint32_t out_r = preserve_source_colors
                ? source_r + (bg_r * (255 - a)) / 255
                : (tint_r * a + bg_r * (255 - a)) / 255;
            const uint32_t out_g = preserve_source_colors
                ? source_g + (bg_g * (255 - a)) / 255
                : (tint_g * a + bg_g * (255 - a)) / 255;
            const uint32_t out_b = preserve_source_colors
                ? source_b + (bg_b * (255 - a)) / 255
                : (tint_b * a + bg_b * (255 - a)) / 255;

            // X11 ZPixmap expects BGRx for 24-bit depth on little-endian
            dst[i] = (out_r << 16) | (out_g << 8) | out_b;
        }

        image = XCreateImage(
            m_display,
            DefaultVisual(m_display, m_screen),
            static_cast<unsigned int>(DefaultDepth(m_display, m_screen)),
            ZPixmap,
            0,
            x11_data,
            size,
            size,
            32,
            0);
        if (!image)
        {
            std::free(x11_data);
            return;
        }
        m_svg_cache[cache_key] = image;
    }

    if (image)
    {
        XPutImage(m_display, drawable, m_graphics_context, image, 0, 0, draw_x, draw_y, size, size);
    }
}


void StudioWorkspaceRenderer::draw_png_icon(
    Drawable drawable,
    const std::string& asset_path,
    int center_x,
    int center_y,
    int max_size,
    const UI::Theme::Color& background) const
{
    if (asset_path.empty())
    {
        return;
    }

    std::error_code path_error;
    std::filesystem::path resolved_path{asset_path};
    if (!std::filesystem::is_regular_file(resolved_path, path_error))
    {
        resolved_path = m_icon_asset_root / resolved_path;
    }
    if (!std::filesystem::is_regular_file(resolved_path, path_error))
    {
        return;
    }

    const std::string resolved_string = resolved_path.string();
    const std::string cache_key = resolved_string + "@png#" +
        std::to_string(max_size) + "/" + to_xft_color(background);
    
    XImage* image = nullptr;
    auto it = m_svg_cache.find(cache_key);
    if (it != m_svg_cache.end())
    {
        image = it->second;
    }
    else
    {
        int width = 0;
        int height = 0;
        int channels = 0;
        unsigned char* data = stbi_load(resolved_string.c_str(), &width, &height, &channels, 4);
        if (!data)
        {
            return;
        }

        // Compute size to fit in max_size
        int draw_w = width;
        int draw_h = height;
        if (width > max_size || height > max_size)
        {
            float aspect = static_cast<float>(width) / static_cast<float>(height);
            if (width > height)
            {
                draw_w = max_size;
                draw_h = static_cast<int>(max_size / aspect);
            }
            else
            {
                draw_h = max_size;
                draw_w = static_cast<int>(max_size * aspect);
            }
        }
        
        char* x11_data = static_cast<char*>(std::malloc(draw_w * draw_h * 4));
        if (!x11_data)
        {
            stbi_image_free(data);
            return;
        }

        const uint32_t bg_r = static_cast<uint32_t>(background.red);
        const uint32_t bg_g = static_cast<uint32_t>(background.green);
        const uint32_t bg_b = static_cast<uint32_t>(background.blue);

        uint32_t* dst = reinterpret_cast<uint32_t*>(x11_data);

        for (int y = 0; y < draw_h; ++y)
        {
            for (int x = 0; x < draw_w; ++x)
            {
                float gx = (x + 0.5f) * width / draw_w - 0.5f;
                float gy = (y + 0.5f) * height / draw_h - 0.5f;
                int gxi = static_cast<int>(gx);
                int gyi = static_cast<int>(gy);
                if (gxi < 0) gxi = 0;
                if (gyi < 0) gyi = 0;
                if (gxi >= width - 1) gxi = width - 2;
                if (gyi >= height - 1) gyi = height - 2;

                float dx = gx - gxi;
                float dy = gy - gyi;

                auto get_pixel = [&](int px, int py, int offset) -> float {
                    return static_cast<float>(data[(py * width + px) * 4 + offset]);
                };

                float r00 = get_pixel(gxi, gyi, 0), r10 = get_pixel(gxi + 1, gyi, 0);
                float r01 = get_pixel(gxi, gyi + 1, 0), r11 = get_pixel(gxi + 1, gyi + 1, 0);
                float g00 = get_pixel(gxi, gyi, 1), g10 = get_pixel(gxi + 1, gyi, 1);
                float g01 = get_pixel(gxi, gyi + 1, 1), g11 = get_pixel(gxi + 1, gyi + 1, 1);
                float b00 = get_pixel(gxi, gyi, 2), b10 = get_pixel(gxi + 1, gyi, 2);
                float b01 = get_pixel(gxi, gyi + 1, 2), b11 = get_pixel(gxi + 1, gyi + 1, 2);
                float a00 = get_pixel(gxi, gyi, 3), a10 = get_pixel(gxi + 1, gyi, 3);
                float a01 = get_pixel(gxi, gyi + 1, 3), a11 = get_pixel(gxi + 1, gyi + 1, 3);

                auto bilerp = [dx, dy](float v00, float v10, float v01, float v11) -> uint32_t {
                    float val = v00 * (1 - dx) * (1 - dy) + v10 * dx * (1 - dy) +
                                v01 * (1 - dx) * dy + v11 * dx * dy;
                    return static_cast<uint32_t>(val + 0.5f);
                };

                uint32_t source_r = bilerp(r00, r10, r01, r11);
                uint32_t source_g = bilerp(g00, g10, g01, g11);
                uint32_t source_b = bilerp(b00, b10, b01, b11);
                uint32_t a = bilerp(a00, a10, a01, a11);

                const uint32_t out_r = source_r * a / 255 + bg_r * (255 - a) / 255;
                const uint32_t out_g = source_g * a / 255 + bg_g * (255 - a) / 255;
                const uint32_t out_b = source_b * a / 255 + bg_b * (255 - a) / 255;

                dst[y * draw_w + x] = (out_r << 16) | (out_g << 8) | out_b;
            }
        }
        stbi_image_free(data);

        image = XCreateImage(
            m_display,
            DefaultVisual(m_display, m_screen),
            static_cast<unsigned int>(DefaultDepth(m_display, m_screen)),
            ZPixmap,
            0,
            x11_data,
            draw_w,
            draw_h,
            32,
            0);
        if (!image)
        {
            std::free(x11_data);
            return;
        }
        
        // We abuse m_svg_cache to also store PNGs for simplicity
        m_svg_cache[cache_key] = image;
    }

    if (image)
    {
        const int draw_x = center_x - image->width / 2;
        const int draw_y = center_y - image->height / 2;
        XPutImage(m_display, drawable, m_graphics_context, image, 0, 0, draw_x, draw_y, image->width, image->height);
    }
}

bool StudioWorkspaceRenderer::draw_ico_icon(
    Drawable drawable,
    const std::string& asset_path,
    int center_x,
    int center_y,
    int max_size,
    const UI::Theme::Color& background) const
{
    if (asset_path.empty() || max_size <= 0 || m_display == nullptr ||
        m_graphics_context == nullptr)
    {
        return false;
    }

    std::error_code path_error;
    std::filesystem::path resolved_path{asset_path};
    if (resolved_path.is_relative() && !m_icon_asset_root.empty())
    {
        const std::filesystem::path themed_path = m_icon_asset_root / resolved_path;
        if (std::filesystem::is_regular_file(themed_path, path_error))
        {
            resolved_path = themed_path;
        }
        else
        {
            // Keep compatibility with callers that pass Assets/icons/foo.ico.
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
        return false;
    }

    const std::string resolved_string = resolved_path.string();
    const std::string cache_key = resolved_string + "@ico#" +
        std::to_string(max_size) + "/" + to_xft_color(background);

    XImage* image = nullptr;
    auto it = m_svg_cache.find(cache_key);
    if (it != m_svg_cache.end())
    {
        image = it->second;
    }
    else
    {
        auto decoded = Utility::decode_ico_file(resolved_string);
        if (!decoded || decoded->width <= 0 || decoded->height <= 0 ||
            decoded->pixels.empty())
        {
            return false;
        }

        const int width = decoded->width;
        const int height = decoded->height;
        const unsigned char* data = decoded->pixels.data();

        // Compute size to fit in max_size.
        int draw_w = width;
        int draw_h = height;
        if (width > max_size || height > max_size)
        {
            const float aspect = static_cast<float>(width) / static_cast<float>(height);
            if (width > height)
            {
                draw_w = max_size;
                draw_h = static_cast<int>(max_size / aspect);
            }
            else
            {
                draw_h = max_size;
                draw_w = static_cast<int>(max_size * aspect);
            }
        }

        char* x11_data = static_cast<char*>(std::malloc(draw_w * draw_h * 4));
        if (!x11_data)
        {
            return false;
        }

        const uint32_t bg_r = static_cast<uint32_t>(background.red);
        const uint32_t bg_g = static_cast<uint32_t>(background.green);
        const uint32_t bg_b = static_cast<uint32_t>(background.blue);

        uint32_t* dst = reinterpret_cast<uint32_t*>(x11_data);

        for (int y = 0; y < draw_h; ++y)
        {
            for (int x = 0; x < draw_w; ++x)
            {
                float gx = (x + 0.5f) * width / draw_w - 0.5f;
                float gy = (y + 0.5f) * height / draw_h - 0.5f;
                int gxi = static_cast<int>(gx);
                int gyi = static_cast<int>(gy);
                if (gxi < 0) gxi = 0;
                if (gyi < 0) gyi = 0;
                if (gxi >= width - 1) gxi = width - 2;
                if (gyi >= height - 1) gyi = height - 2;

                float dx = gx - gxi;
                float dy = gy - gyi;

                auto get_pixel = [&](int px, int py, int offset) -> float {
                    return static_cast<float>(data[(py * width + px) * 4 + offset]);
                };

                float r00 = get_pixel(gxi, gyi, 0), r10 = get_pixel(gxi + 1, gyi, 0);
                float r01 = get_pixel(gxi, gyi + 1, 0), r11 = get_pixel(gxi + 1, gyi + 1, 0);
                float g00 = get_pixel(gxi, gyi, 1), g10 = get_pixel(gxi + 1, gyi, 1);
                float g01 = get_pixel(gxi, gyi + 1, 1), g11 = get_pixel(gxi + 1, gyi + 1, 1);
                float b00 = get_pixel(gxi, gyi, 2), b10 = get_pixel(gxi + 1, gyi, 2);
                float b01 = get_pixel(gxi, gyi + 1, 2), b11 = get_pixel(gxi + 1, gyi + 1, 2);
                float a00 = get_pixel(gxi, gyi, 3), a10 = get_pixel(gxi + 1, gyi, 3);
                float a01 = get_pixel(gxi, gyi + 1, 3), a11 = get_pixel(gxi + 1, gyi + 1, 3);

                auto bilerp = [dx, dy](float v00, float v10, float v01, float v11) -> uint32_t {
                    float val = v00 * (1 - dx) * (1 - dy) + v10 * dx * (1 - dy) +
                                v01 * (1 - dx) * dy + v11 * dx * dy;
                    return static_cast<uint32_t>(val + 0.5f);
                };

                uint32_t source_r = bilerp(r00, r10, r01, r11);
                uint32_t source_g = bilerp(g00, g10, g01, g11);
                uint32_t source_b = bilerp(b00, b10, b01, b11);
                uint32_t a = bilerp(a00, a10, a01, a11);

                const uint32_t out_r = source_r * a / 255 + bg_r * (255 - a) / 255;
                const uint32_t out_g = source_g * a / 255 + bg_g * (255 - a) / 255;
                const uint32_t out_b = source_b * a / 255 + bg_b * (255 - a) / 255;

                dst[y * draw_w + x] = (out_r << 16) | (out_g << 8) | out_b;
            }
        }

        image = XCreateImage(
            m_display,
            DefaultVisual(m_display, m_screen),
            static_cast<unsigned int>(DefaultDepth(m_display, m_screen)),
            ZPixmap,
            0,
            x11_data,
            draw_w,
            draw_h,
            32,
            0);
        if (!image)
        {
            std::free(x11_data);
            return false;
        }

        // Reuse the SVG/PNG cache for ICOs as well.
        m_svg_cache[cache_key] = image;
    }

    const int draw_x = center_x - image->width / 2;
    const int draw_y = center_y - image->height / 2;
    XPutImage(m_display, drawable, m_graphics_context, image, 0, 0, draw_x,
              draw_y, image->width, image->height);
    return true;
}

} // namespace Zenvra::Platform::X11::Components
