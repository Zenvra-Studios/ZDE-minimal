#include "Platform/Win32/Components/StudioWorkspaceRenderer.h"

#include "UI/Editor/EditorFileSystem.h"
#include "Utility/Fonts.h"
#include <lunasvg.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

namespace Zenvra::Platform::Win32::Components
{

namespace
{

int round_to_int(float value)
{
    return static_cast<int>(std::lround(value));
}

COLORREF to_color_ref(const UI::Theme::Color& color)
{
    return RGB(color.red, color.green, color.blue);
}

std::string to_font_color(const UI::Theme::Color& color)
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

RECT to_native_rect(const UI::Rect& rectangle)
{
    return RECT{
        round_to_int(rectangle.x),
        round_to_int(rectangle.y),
        round_to_int(rectangle.right()),
        round_to_int(rectangle.bottom()),
    };
}

} // namespace

StudioWorkspaceRenderer::StudioWorkspaceRenderer() = default;

StudioWorkspaceRenderer::~StudioWorkspaceRenderer()
{
    shutdown();
}

bool StudioWorkspaceRenderer::initialize(UINT dpi)
{
    shutdown();
    m_dpi = std::max(dpi, 48U);
    m_dpi_scale = static_cast<float>(m_dpi) / 96.0F;

    std::error_code path_error;
    const std::filesystem::path current_path = std::filesystem::current_path(path_error);
    std::optional<std::filesystem::path> project_root;
    if (!path_error)
    {
        project_root = UI::Editor::EditorFileSystem::find_project_root(current_path);
    }
    if (!project_root)
    {
        std::array<wchar_t, 32768> executable_path{};
        const DWORD length = GetModuleFileNameW(
            nullptr, executable_path.data(), static_cast<DWORD>(executable_path.size()));
        if (length > 0 && length < executable_path.size())
        {
            project_root = UI::Editor::EditorFileSystem::find_project_root(
                std::filesystem::path{executable_path.data()});
        }
    }
    if (project_root)
    {
        m_icon_asset_root = *project_root / "Assets" / "icons";
    }

    m_ui_font = std::make_unique<AntialiasedFont>(
        "Segoe UI", std::max(round_to_int(12.0F * m_dpi_scale), 9));
    m_small_font = std::make_unique<AntialiasedFont>(
        "Segoe UI", std::max(round_to_int(12.0F * m_dpi_scale), 9));
    m_editor_font = std::make_unique<AntialiasedFont>(
        "Consolas", std::max(round_to_int(14.0F * m_dpi_scale), 10));
    m_minimap_font = std::make_unique<AntialiasedFont>(
        "Consolas", std::max(round_to_int(3.0F * m_dpi_scale), 3));
    if (!m_ui_font->isValid() || !m_small_font->isValid() ||
        !m_editor_font->isValid() || !m_minimap_font->isValid())
    {
        shutdown();
        return false;
    }
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
    HDC device_context,
    float point_x,
    float point_y,
    int client_width,
    int client_height,
    float content_top,
    bool extend_selection)
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
            static_cast<void>(m_text_editor.open_file(*sidebar_file));
        }
        return true;
    }
    if (m_terminal_panel.handle_pointer_press(layout, point_x, point_y))
    {
        return true;
    }
    m_terminal_panel.set_focused(false);
    return m_text_editor.handle_pointer_press(
        *this, device_context, layout, point_x, point_y, extend_selection);
}

bool StudioWorkspaceRenderer::handle_double_click(
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
    return m_terminal_panel.handle_double_click(layout, point_x, point_y);
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
    HDC device_context,
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
    return m_text_editor.handle_pointer_drag(
        *this, device_context, layout, point_x, point_y);
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
    return layout.tab_bar_bounds.contains(point_x, point_y);
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

bool StudioWorkspaceRenderer::tick_caret_blink() noexcept
{
    const bool caret_changed = m_text_editor.tick_caret_blink();
    const bool terminal_changed = m_terminal_panel.poll();
    return caret_changed || terminal_changed;
}

void StudioWorkspaceRenderer::shutdown()
{
    m_terminal_panel.shutdown();
    m_svg_cache.clear();
    m_icon_asset_root.clear();
    m_minimap_font.reset();
    m_editor_font.reset();
    m_small_font.reset();
    m_ui_font.reset();
}

void StudioWorkspaceRenderer::render(
    HDC device_context,
    int client_width,
    int client_height,
    float content_top) const
{
    if (device_context == nullptr || m_ui_font == nullptr || m_small_font == nullptr ||
        m_editor_font == nullptr || m_minimap_font == nullptr)
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
    fill_rectangle(device_context, layout.workspace_bounds, m_palette.workspace_background);
    fill_rectangle(device_context, layout.tab_bar_bounds, m_palette.tab_background);
    fill_rectangle(device_context, layout.activity_bar_bounds, m_palette.sidebar_background);
    fill_rectangle(device_context, layout.tool_sidebar_bounds, m_palette.sidebar_background);
    fill_rectangle(device_context, layout.gutter_bounds, m_palette.editor_background);
    fill_rectangle(device_context, layout.editor_bounds, m_palette.editor_background);
    fill_rectangle(device_context, layout.status_bar_bounds, m_palette.status_background);
    SetBkMode(device_context, TRANSPARENT);

    m_text_editor.render(*this, device_context, layout);
    m_terminal_panel.render(*this, device_context, layout);
    m_tool_sidebar.render(*this, device_context, layout);
    m_activity_sidebar.render(*this, device_context, layout);
    if (const UI::Editor::TextDocumentModel* document = m_text_editor.get_document())
    {
        m_footer_toolbar.render(
            *this,
            device_context,
            layout,
            document->get_breadcrumbs(),
            document->get_status());
    }
}

void StudioWorkspaceRenderer::fill_rectangle(
    HDC device_context,
    const UI::Rect& rectangle,
    const UI::Theme::Color& color) const
{
    if (rectangle.is_empty())
    {
        return;
    }
    RECT native_rectangle = to_native_rect(rectangle);
    HBRUSH brush = CreateSolidBrush(to_color_ref(color));
    FillRect(device_context, &native_rectangle, brush);
    DeleteObject(brush);
}

void StudioWorkspaceRenderer::draw_rectangle(
    HDC device_context,
    const UI::Rect& rectangle,
    const UI::Theme::Color& color) const
{
    if (rectangle.is_empty())
    {
        return;
    }
    const RECT bounds = to_native_rect(rectangle);
    HPEN pen = CreatePen(PS_SOLID, 1, to_color_ref(color));
    HGDIOBJ previous_pen = SelectObject(device_context, pen);
    HGDIOBJ previous_brush = SelectObject(device_context, GetStockObject(HOLLOW_BRUSH));
    Rectangle(device_context, bounds.left, bounds.top, bounds.right, bounds.bottom);
    SelectObject(device_context, previous_brush);
    SelectObject(device_context, previous_pen);
    DeleteObject(pen);
}

void StudioWorkspaceRenderer::draw_line(
    HDC device_context,
    int from_x,
    int from_y,
    int to_x,
    int to_y,
    const UI::Theme::Color& color) const
{
    HPEN pen = CreatePen(PS_SOLID, 1, to_color_ref(color));
    HGDIOBJ previous_pen = SelectObject(device_context, pen);
    MoveToEx(device_context, from_x, from_y, nullptr);
    LineTo(device_context, to_x, to_y);
    SelectObject(device_context, previous_pen);
    DeleteObject(pen);
}

void StudioWorkspaceRenderer::draw_text(
    HDC device_context,
    AntialiasedFont& font,
    std::string_view text,
    float point_x,
    float center_y,
    const UI::Theme::Color& color) const
{
    if (text.empty())
    {
        return;
    }
    const int baseline = round_to_int(
        center_y - static_cast<float>(font.getAscent(device_context) + font.getDescent(device_context)) * 0.5F +
        static_cast<float>(font.getAscent(device_context)));
    font.drawString(
        device_context,
        to_font_color(color),
        round_to_int(point_x),
        baseline,
        std::string{text});
}

void StudioWorkspaceRenderer::draw_svg_icon(
    HDC device_context,
    std::string_view asset_name,
    int center_x,
    int center_y,
    int size,
    const UI::Theme::Color& color,
    const UI::Theme::Color& background) const
{
    if (device_context == nullptr || size <= 0 || asset_name.empty())
    {
        return;
    }

    std::filesystem::path resolved_path{asset_name};
    if (resolved_path.is_relative() && !m_icon_asset_root.empty())
    {
        resolved_path = m_icon_asset_root / resolved_path.filename();
    }
    std::error_code path_error;
    if (!std::filesystem::is_regular_file(resolved_path, path_error))
    {
        return;
    }

    const std::string resolved_string = resolved_path.string();
    const std::string cache_key = resolved_string + "@" + std::to_string(size) + "#" +
        to_font_color(color) + "/" + to_font_color(background);
    auto cached = m_svg_cache.find(cache_key);
    if (cached == m_svg_cache.end())
    {
        auto document = lunasvg::Document::loadFromFile(resolved_string);
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

        std::vector<std::uint32_t> pixels(
            static_cast<std::size_t>(size) * static_cast<std::size_t>(size));
        const auto* source = reinterpret_cast<const std::uint32_t*>(bitmap.data());
        for (std::size_t index = 0; index < pixels.size(); ++index)
        {
            const std::uint32_t alpha = (source[index] >> 24U) & 0xFFU;
            const std::uint32_t inverse_alpha = 255U - alpha;
            const std::uint32_t red =
                (static_cast<std::uint32_t>(color.red) * alpha +
                    static_cast<std::uint32_t>(background.red) * inverse_alpha) /
                255U;
            const std::uint32_t green =
                (static_cast<std::uint32_t>(color.green) * alpha +
                    static_cast<std::uint32_t>(background.green) * inverse_alpha) /
                255U;
            const std::uint32_t blue =
                (static_cast<std::uint32_t>(color.blue) * alpha +
                    static_cast<std::uint32_t>(background.blue) * inverse_alpha) /
                255U;
            pixels[index] = blue | (green << 8U) | (red << 16U);
        }
        cached = m_svg_cache.emplace(cache_key, std::move(pixels)).first;
    }

    BITMAPINFO bitmap_info{};
    bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap_info.bmiHeader.biWidth = size;
    bitmap_info.bmiHeader.biHeight = -size;
    bitmap_info.bmiHeader.biPlanes = 1;
    bitmap_info.bmiHeader.biBitCount = 32;
    bitmap_info.bmiHeader.biCompression = BI_RGB;
    const int half = size / 2;
    SetDIBitsToDevice(
        device_context,
        center_x - half,
        center_y - half,
        static_cast<DWORD>(size),
        static_cast<DWORD>(size),
        0,
        0,
        0,
        static_cast<UINT>(size),
        cached->second.data(),
        &bitmap_info,
        DIB_RGB_COLORS);
}

int StudioWorkspaceRenderer::get_text_width(
    HDC device_context,
    AntialiasedFont& font,
    std::string_view text) const
{
    return font.getTextWidth(device_context, std::string{text});
}

} // namespace Zenvra::Platform::Win32::Components
