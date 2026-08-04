#include "Platform/Win32/Components/TextEditor.h"

#include "Platform/Win32/Components/StudioWorkspaceRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
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

bool is_utf8_continuation(char character)
{
    return (static_cast<unsigned char>(character) & 0xC0U) == 0x80U;
}

std::size_t next_character_column(std::string_view line, std::size_t column)
{
    column = std::min(column + 1, line.size());
    while (column < line.size() && is_utf8_continuation(line[column]))
    {
        ++column;
    }
    return column;
}

bool has_gutter_marker(std::string_view line)
{
    return line.find("namespace ") != std::string_view::npos ||
        (line.find("::") != std::string_view::npos && line.find('(') != std::string_view::npos);
}

} // namespace

bool TextEditor::open_file(const std::filesystem::path& path)
{
    const bool opened = m_controller.open_file(path);
    if (opened)
    {
        m_scrollbar.reset();
        m_reveal_caret_pending = true;
        m_caret_blink.reset();
    }
    return opened;
}

std::size_t TextEditor::open_dropped_paths(
    std::span<const std::filesystem::path> dropped_paths)
{
    const std::size_t opened_count = m_controller.open_dropped_paths(dropped_paths);
    if (opened_count > 0)
    {
        m_scrollbar.reset();
        m_reveal_caret_pending = true;
        m_focused = true;
        m_caret_blink.reset();
    }
    return opened_count;
}

bool TextEditor::create_buffer()
{
    const bool created = m_controller.create_buffer();
    if (created)
    {
        m_scrollbar.reset();
        m_reveal_caret_pending = true;
        m_focused = true;
        m_caret_blink.reset();
    }
    return created;
}

bool TextEditor::handle_pointer_press(
    const StudioWorkspaceRenderer& surface,
    HDC device_context,
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_x,
    float point_y,
    bool extend_selection)
{
    if (layout.tab_bar_bounds.contains(point_x, point_y))
    {
        const float action_width =
            UI::Editor::StudioEditorMetrics::editor_tab_action_width * surface.m_dpi_scale;
        const UI::Rect create_bounds{
            layout.tab_bar_bounds.right() - action_width * 2.0F,
            layout.tab_bar_bounds.y,
            action_width,
            layout.tab_bar_bounds.height};
        const UI::Rect delete_bounds{
            layout.tab_bar_bounds.right() - action_width,
            layout.tab_bar_bounds.y,
            action_width,
            layout.tab_bar_bounds.height};
        if (create_bounds.contains(point_x, point_y))
        {
            m_focused = true;
            return handle_action(UI::Editor::EditorAction::CreateDocument);
        }
        if (delete_bounds.contains(point_x, point_y))
        {
            return handle_action(UI::Editor::EditorAction::RemoveDocument);
        }

        float tab_x = layout.tab_bar_bounds.x;
        const float right_limit = create_bounds.x;
        const std::span<const UI::Editor::EditorSessionDocument> documents =
            m_controller.get_documents();
        for (std::size_t index = 0; index < documents.size(); ++index)
        {
            const float width = UI::Editor::calculate_editor_tab_width(
                static_cast<float>(surface.get_text_width(
                    device_context,
                    *surface.m_ui_font,
                    documents[index].text.get_file_name())),
                surface.m_dpi_scale);
            if (tab_x + width > right_limit)
            {
                break;
            }
            const UI::Rect bounds{
                tab_x, layout.tab_bar_bounds.y, width, layout.tab_bar_bounds.height};
            if (bounds.contains(point_x, point_y))
            {
                const UI::Rect close_bounds{
                    bounds.right() -
                        UI::Editor::StudioEditorMetrics::editor_tab_close_width *
                            surface.m_dpi_scale,
                    bounds.y,
                    UI::Editor::StudioEditorMetrics::editor_tab_close_width *
                        surface.m_dpi_scale,
                    bounds.height};
                m_focused = true;
                if (close_bounds.contains(point_x, point_y))
                {
                    const bool closed = m_controller.close_file(index);
                    if (closed)
                    {
                        m_scrollbar.reset();
                        m_reveal_caret_pending = true;
                        m_caret_blink.reset();
                    }
                    return closed;
                }
                if (m_controller.activate_file(index))
                {
                    m_scrollbar.reset();
                    m_reveal_caret_pending = true;
                    m_caret_blink.reset();
                }
                return true;
            }
            tab_x += width +
                UI::Editor::StudioEditorMetrics::editor_tab_gap * surface.m_dpi_scale;
        }
        return true;
    }

    UI::Editor::TextDocumentModel* document = m_controller.get_active_document();
    if (document != nullptr && m_minimap.is_point(layout, point_x, point_y))
    {
        const float line_height = 20.0F * surface.m_dpi_scale;
        const std::size_t visible_count = static_cast<std::size_t>(std::max(
            static_cast<int>(layout.editor_bounds.height / line_height), 1));
        m_scrollbar.synchronize(document->get_line_count(), visible_count);
        const std::optional<std::size_t> target = m_minimap.handle_pointer_press(
            layout,
            point_x,
            point_y,
            document->get_line_count(),
            visible_count,
            m_scrollbar.get_first_visible_line());
        if (target)
        {
            static_cast<void>(m_scrollbar.scroll_to(*target));
        }
        m_focused = true;
        m_pointer_selecting = false;
        m_reveal_caret_pending = false;
        m_caret_blink.reset();
        return true;
    }
    if (document != nullptr && m_scrollbar.is_point(layout, point_x, point_y))
    {
        const float line_height = 20.0F * surface.m_dpi_scale;
        const std::size_t visible_count = static_cast<std::size_t>(std::max(
            static_cast<int>(layout.editor_bounds.height / line_height), 1));
        m_scrollbar.synchronize(document->get_line_count(), visible_count);
        m_focused = true;
        m_pointer_selecting = false;
        m_reveal_caret_pending = false;
        m_caret_blink.reset();
        return m_scrollbar.handle_pointer_press(layout, point_x, point_y);
    }

    if ((!layout.gutter_bounds.contains(point_x, point_y) &&
         !layout.editor_bounds.contains(point_x, point_y)) ||
        document == nullptr)
    {
        return false;
    }
    m_focused = true;
    m_pointer_selecting = true;
    const UI::Editor::TextPosition position = position_from_point(
        surface, device_context, layout, point_x, point_y);
    static_cast<void>(document->set_caret(
        position.line, position.column, extend_selection));
    m_reveal_caret_pending = true;
    m_caret_blink.reset();
    return true;
}

bool TextEditor::handle_pointer_move(
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_x,
    float point_y) noexcept
{
    return m_scrollbar.set_hovered(layout, point_x, point_y);
}

bool TextEditor::handle_pointer_drag(
    const StudioWorkspaceRenderer& surface,
    HDC device_context,
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_x,
    float point_y)
{
    UI::Editor::TextDocumentModel* document = m_controller.get_active_document();
    if (document != nullptr)
    {
        const float line_height = 20.0F * surface.m_dpi_scale;
        const std::size_t visible_count = static_cast<std::size_t>(std::max(
            static_cast<int>(layout.editor_bounds.height / line_height), 1));
        const std::optional<std::size_t> target = m_minimap.handle_pointer_drag(
            layout,
            point_y,
            document->get_line_count(),
            visible_count,
            m_scrollbar.get_first_visible_line());
        if (target)
        {
            static_cast<void>(m_scrollbar.scroll_to(*target));
            m_reveal_caret_pending = false;
            return true;
        }
    }
    if (m_scrollbar.handle_pointer_drag(layout, point_y))
    {
        m_reveal_caret_pending = false;
        return true;
    }
    if (!m_pointer_selecting || document == nullptr)
    {
        return false;
    }
    const UI::Editor::TextPosition position = position_from_point(
        surface, device_context, layout, point_x, point_y);
    const bool changed = document->set_caret(position.line, position.column, true);
    if (changed)
    {
        m_reveal_caret_pending = true;
        m_caret_blink.reset();
    }
    return changed;
}

bool TextEditor::handle_pointer_release() noexcept
{
    const bool was_selecting = m_pointer_selecting;
    m_pointer_selecting = false;
    const bool minimap_was_dragging = m_minimap.handle_pointer_release();
    const bool scrollbar_was_dragging = m_scrollbar.handle_pointer_release();
    return minimap_was_dragging || scrollbar_was_dragging || was_selecting;
}

bool TextEditor::handle_scroll(
    const StudioWorkspaceRenderer& surface,
    const UI::Editor::StudioEditorLayoutResult& layout,
    std::ptrdiff_t line_delta) noexcept
{
    const UI::Editor::TextDocumentModel* document = m_controller.get_active_document();
    if (document == nullptr)
    {
        return false;
    }
    const float line_height = 20.0F * surface.m_dpi_scale;
    const std::size_t visible_count = static_cast<std::size_t>(std::max(
        static_cast<int>(layout.editor_bounds.height / line_height), 1));
    m_scrollbar.synchronize(document->get_line_count(), visible_count);
    m_reveal_caret_pending = false;
    return m_scrollbar.scroll_lines(line_delta);
}

bool TextEditor::handle_input(
    UI::Editor::EditorInputCommand command,
    bool extend_selection)
{
    const bool changed = m_focused && m_controller.execute_input(command, extend_selection);
    if (changed)
    {
        m_reveal_caret_pending = true;
        m_caret_blink.reset();
    }
    return changed;
}

bool TextEditor::handle_action(UI::Editor::EditorAction action)
{
    const bool changed = m_controller.execute_action(action);
    if (changed)
    {
        if (action == UI::Editor::EditorAction::CreateDocument ||
            action == UI::Editor::EditorAction::CloseDocument ||
            action == UI::Editor::EditorAction::RemoveDocument)
        {
            m_scrollbar.reset();
        }
        m_reveal_caret_pending = true;
        m_caret_blink.reset();
    }
    return changed;
}

std::optional<bool> TextEditor::handle_command(std::string_view command_id)
{
    const std::optional<UI::Editor::EditorAction> action =
        UI::Editor::EditorController::action_from_command_id(command_id);
    return action ? std::optional<bool>{handle_action(*action)} : std::nullopt;
}

std::optional<bool> TextEditor::is_command_enabled(
    std::string_view command_id) const noexcept
{
    const std::optional<UI::Editor::EditorAction> action =
        UI::Editor::EditorController::action_from_command_id(command_id);
    return action ? std::optional<bool>{m_controller.can_execute_action(*action)} : std::nullopt;
}

bool TextEditor::handle_text_input(std::string_view utf8_text)
{
    const bool changed = m_focused && m_controller.insert_text(utf8_text);
    if (changed)
    {
        m_reveal_caret_pending = true;
        m_caret_blink.reset();
    }
    return changed;
}

bool TextEditor::is_focused() const noexcept
{
    return m_focused;
}

bool TextEditor::is_scrollbar_point(
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_x,
    float point_y) const noexcept
{
    return m_scrollbar.is_point(layout, point_x, point_y);
}

bool TextEditor::is_minimap_point(
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_x,
    float point_y) const noexcept
{
    return m_minimap.is_point(layout, point_x, point_y);
}

bool TextEditor::tick_caret_blink() noexcept
{
    return m_focused && m_controller.get_active_document() != nullptr &&
        m_caret_blink.tick();
}

const UI::Editor::TextDocumentModel* TextEditor::get_document() const noexcept
{
    return m_controller.get_active_document();
}

void TextEditor::render(
    const StudioWorkspaceRenderer& surface,
    HDC device_context,
    const UI::Editor::StudioEditorLayoutResult& layout) const
{
    draw_tab_strip(surface, device_context, layout);
    draw_document(surface, device_context, layout);
    if (const UI::Editor::TextDocumentModel* document = m_controller.get_active_document())
    {
        const float line_height = 20.0F * surface.m_dpi_scale;
        const std::size_t visible_count = static_cast<std::size_t>(std::max(
            static_cast<int>(layout.editor_bounds.height / line_height), 1));
        m_minimap.render(
            surface,
            device_context,
            layout,
            *document,
            m_scrollbar.get_first_visible_line(),
            visible_count);
    }
    m_scrollbar.render(surface, device_context, layout);
}

void TextEditor::draw_tab_strip(
    const StudioWorkspaceRenderer& surface,
    HDC device_context,
    const UI::Editor::StudioEditorLayoutResult& layout) const
{
    float tab_x = layout.tab_bar_bounds.x;
    const float action_width =
        UI::Editor::StudioEditorMetrics::editor_tab_action_width * surface.m_dpi_scale;
    const float right_limit = layout.tab_bar_bounds.right() - action_width * 2.0F;
    const std::span<const UI::Editor::EditorSessionDocument> documents =
        m_controller.get_documents();
    const std::optional<std::size_t> active_index = m_controller.get_active_index();
    for (std::size_t index = 0; index < documents.size(); ++index)
    {
        const UI::Editor::TextDocumentModel& document = documents[index].text;
        const bool active = active_index && *active_index == index;
        const float width = UI::Editor::calculate_editor_tab_width(
            static_cast<float>(surface.get_text_width(
                device_context, *surface.m_ui_font, document.get_file_name())),
            surface.m_dpi_scale);
        if (tab_x + width > right_limit)
        {
            break;
        }
        const UI::Rect bounds{tab_x, layout.tab_bar_bounds.y, width, layout.tab_bar_bounds.height};
        if (active)
        {
            surface.fill_rectangle(
                device_context, bounds, surface.m_palette.tab_active_background);
            surface.fill_rectangle(
                device_context,
                UI::Rect{bounds.x, bounds.bottom() - surface.m_dpi_scale,
                    bounds.width, surface.m_dpi_scale},
                surface.m_palette.accent);
        }
        surface.draw_svg_icon(
            device_context,
            "file.svg",
            round_to_int(bounds.x +
                (UI::Editor::StudioEditorMetrics::editor_tab_icon_offset + 4.0F) *
                    surface.m_dpi_scale),
            round_to_int(bounds.y + bounds.height * 0.5F),
            std::max(round_to_int(12.0F * surface.m_dpi_scale), 9),
            surface.m_palette.text_muted,
            active ? surface.m_palette.tab_active_background
                   : surface.m_palette.tab_background);
        surface.draw_text(
            device_context,
            *surface.m_ui_font,
            document.get_file_name(),
            bounds.x + UI::Editor::StudioEditorMetrics::editor_tab_label_offset *
                surface.m_dpi_scale,
            bounds.y + bounds.height * 0.5F,
            active ? surface.m_palette.text_primary : surface.m_palette.text_muted);
        if (document.is_dirty())
        {
            surface.fill_rectangle(
                device_context,
                UI::Rect{bounds.right() - 36.0F * surface.m_dpi_scale,
                    bounds.y + bounds.height * 0.5F - 2.0F * surface.m_dpi_scale,
                    4.0F * surface.m_dpi_scale,
                    4.0F * surface.m_dpi_scale},
                surface.m_palette.warning);
        }
        const int close_center_x = round_to_int(
            bounds.right() -
            UI::Editor::StudioEditorMetrics::editor_tab_close_width *
                0.5F * surface.m_dpi_scale);
        const int close_center_y = round_to_int(bounds.y + bounds.height * 0.5F);
        surface.draw_svg_icon(
            device_context,
            "close.svg",
            close_center_x,
            close_center_y,
            std::max(round_to_int(10.0F * surface.m_dpi_scale), 8),
            surface.m_palette.text_muted,
            active ? surface.m_palette.tab_active_background
                   : surface.m_palette.tab_background);
        surface.draw_line(
            device_context,
            round_to_int(bounds.right()),
            round_to_int(bounds.y + 6.0F * surface.m_dpi_scale),
            round_to_int(bounds.right()),
            round_to_int(bounds.bottom() - 6.0F * surface.m_dpi_scale),
            surface.m_palette.border);
        tab_x += width +
            UI::Editor::StudioEditorMetrics::editor_tab_gap * surface.m_dpi_scale;
    }

    const int action_center_y = round_to_int(
        layout.tab_bar_bounds.y + layout.tab_bar_bounds.height * 0.5F);
    const int add_center_x = round_to_int(layout.tab_bar_bounds.right() - action_width * 1.5F);
    const int action_half = std::max(round_to_int(4.0F * surface.m_dpi_scale), 3);
    surface.draw_line(device_context, add_center_x - action_half, action_center_y,
        add_center_x + action_half, action_center_y, surface.m_palette.text_muted);
    surface.draw_line(device_context, add_center_x, action_center_y - action_half,
        add_center_x, action_center_y + action_half, surface.m_palette.text_muted);

    const int delete_center_x = round_to_int(layout.tab_bar_bounds.right() - action_width * 0.5F);
    const int trash_half = std::max(round_to_int(4.0F * surface.m_dpi_scale), 3);
    surface.draw_rectangle(
        device_context,
        UI::Rect{static_cast<float>(delete_center_x - trash_half),
            static_cast<float>(action_center_y - trash_half + 2),
            static_cast<float>(trash_half * 2),
            static_cast<float>(trash_half * 2)},
        surface.m_palette.text_muted);
    surface.draw_line(device_context, delete_center_x - trash_half - 1,
        action_center_y - trash_half, delete_center_x + trash_half + 1,
        action_center_y - trash_half, surface.m_palette.text_muted);
    surface.draw_line(
        device_context,
        round_to_int(layout.tab_bar_bounds.x),
        round_to_int(layout.tab_bar_bounds.bottom() - 1.0F),
        round_to_int(layout.tab_bar_bounds.right()),
        round_to_int(layout.tab_bar_bounds.bottom() - 1.0F),
        surface.m_palette.border);
}

void TextEditor::draw_document(
    const StudioWorkspaceRenderer& surface,
    HDC device_context,
    const UI::Editor::StudioEditorLayoutResult& layout) const
{
    const UI::Editor::TextDocumentModel* document = m_controller.get_active_document();
    if (document == nullptr)
    {
        return;
    }
    const float line_height = 20.0F * surface.m_dpi_scale;
    const float first_center_y = layout.editor_bounds.y + line_height * 0.5F;
    const float code_x = layout.editor_bounds.x + 14.0F * surface.m_dpi_scale;
    const std::size_t visible_count = static_cast<std::size_t>(std::max(
        static_cast<int>(layout.editor_bounds.height / line_height), 1));
    m_scrollbar.synchronize(document->get_line_count(), visible_count);
    if (m_reveal_caret_pending)
    {
        static_cast<void>(m_scrollbar.reveal_line(document->get_caret_line()));
        m_reveal_caret_pending = false;
    }
    const std::size_t first_line = m_scrollbar.get_first_visible_line();
    const std::size_t render_count = std::min(
        visible_count, document->get_line_count() - first_line);
    const bool syntax_highlighting =
        UI::Editor::supports_editor_syntax_highlighting(document->get_file_name());
    const auto token_color = [&surface](UI::Editor::EditorTokenKind kind)
        -> const UI::Theme::Color& {
        switch (kind)
        {
        case UI::Editor::EditorTokenKind::Keyword: return surface.m_palette.keyword;
        case UI::Editor::EditorTokenKind::Number: return surface.m_palette.number;
        case UI::Editor::EditorTokenKind::Label: return surface.m_palette.label;
        case UI::Editor::EditorTokenKind::Type: return surface.m_palette.type;
        case UI::Editor::EditorTokenKind::Comment: return surface.m_palette.comment;
        case UI::Editor::EditorTokenKind::String: return surface.m_palette.success;
        case UI::Editor::EditorTokenKind::Plain: return surface.m_palette.text_primary;
        }
        return surface.m_palette.text_primary;
    };
    surface.draw_line(
        device_context,
        round_to_int(layout.gutter_bounds.right() - 1.0F),
        round_to_int(layout.gutter_bounds.y),
        round_to_int(layout.gutter_bounds.right() - 1.0F),
        round_to_int(layout.gutter_bounds.bottom()),
        surface.m_palette.border);

    for (std::size_t row = 0; row < render_count; ++row)
    {
        const std::size_t line_index = first_line + row;
        const std::string_view line = document->get_line(line_index);
        const float center_y = first_center_y + static_cast<float>(row) * line_height;
        const bool active_line = line_index == document->get_caret_line();
        if (active_line)
        {
            surface.fill_rectangle(
                device_context,
                UI::Rect{layout.gutter_bounds.x, center_y - line_height * 0.5F,
                    layout.editor_bounds.right() - layout.gutter_bounds.x, line_height},
                surface.m_palette.active_line_background);
        }
        const std::string number = std::to_string(line_index + 1);
        const float number_x = layout.gutter_bounds.right() - 24.0F * surface.m_dpi_scale -
            static_cast<float>(surface.get_text_width(
                device_context, *surface.m_small_font, number));
        surface.draw_text(
            device_context,
            *surface.m_small_font,
            number,
            number_x,
            center_y,
            active_line ? surface.m_palette.text_primary : surface.m_palette.text_muted);
        if (has_gutter_marker(line))
        {
            const int marker_x = round_to_int(
                layout.gutter_bounds.right() - 13.0F * surface.m_dpi_scale);
            const int marker_y = round_to_int(center_y);
            const int half = std::max(round_to_int(3.0F * surface.m_dpi_scale), 2);
            POINT points[]{
                POINT{marker_x, marker_y - half},
                POINT{marker_x + half, marker_y},
                POINT{marker_x, marker_y + half},
                POINT{marker_x - half, marker_y},
            };
            HPEN pen = CreatePen(PS_SOLID, 1, to_color_ref(surface.m_palette.text_muted));
            HGDIOBJ previous_pen = SelectObject(device_context, pen);
            HGDIOBJ previous_brush = SelectObject(device_context, GetStockObject(HOLLOW_BRUSH));
            Polygon(device_context, points, 4);
            SelectObject(device_context, previous_brush);
            SelectObject(device_context, previous_pen);
            DeleteObject(pen);
        }
        if (document->has_selection())
        {
            const UI::Editor::TextSelection selection = document->get_selection();
            if (line_index >= selection.start.line && line_index <= selection.end.line)
            {
                const std::size_t selection_start = line_index == selection.start.line
                    ? selection.start.column
                    : 0;
                const std::size_t selection_end = line_index == selection.end.line
                    ? selection.end.column
                    : line.size();
                const float selection_x = code_x + static_cast<float>(surface.get_text_width(
                    device_context, *surface.m_editor_font,
                    line.substr(0, selection_start)));
                float selection_width = static_cast<float>(surface.get_text_width(
                    device_context, *surface.m_editor_font,
                    line.substr(selection_start, selection_end - selection_start)));
                if (line_index < selection.end.line)
                {
                    selection_width += 6.0F * surface.m_dpi_scale;
                }
                selection_width = std::min(
                    selection_width, std::max(layout.editor_bounds.right() - selection_x, 0.0F));
                if (selection_width > 0.0F)
                {
                    surface.fill_rectangle(
                        device_context,
                        UI::Rect{selection_x, center_y - line_height * 0.5F,
                            selection_width, line_height},
                        surface.m_palette.selection_background);
                }
            }
        }
        if (syntax_highlighting)
        {
            float token_x = code_x;
            std::size_t rendered_bytes = 0;
            std::array<UI::Editor::EditorToken, UI::Editor::maximum_editor_tokens> tokens{};
            const std::size_t token_count = UI::Editor::tokenize_editor_line(line, tokens);
            for (std::size_t token_index = 0; token_index < token_count; ++token_index)
            {
                const UI::Editor::EditorToken& token = tokens[token_index];
                surface.draw_text(
                    device_context,
                    *surface.m_editor_font,
                    token.text,
                    token_x,
                    center_y,
                    token_color(token.kind));
                token_x += static_cast<float>(surface.get_text_width(
                    device_context, *surface.m_editor_font, token.text));
                rendered_bytes += token.text.size();
            }
            if (rendered_bytes < line.size())
            {
                surface.draw_text(device_context, *surface.m_editor_font,
                    line.substr(rendered_bytes), token_x, center_y,
                    surface.m_palette.text_primary);
            }
        }
        else
        {
            surface.draw_text(
                device_context,
                *surface.m_editor_font,
                line,
                code_x,
                center_y,
                surface.m_palette.text_primary);
        }
        if (active_line && m_focused && m_caret_blink.is_visible())
        {
            const std::string_view prefix = line.substr(0, document->get_caret_column());
            const int caret_x = round_to_int(
                code_x + static_cast<float>(surface.get_text_width(
                    device_context, *surface.m_editor_font, prefix)));
            surface.draw_line(
                device_context,
                caret_x,
                round_to_int(center_y - 8.0F * surface.m_dpi_scale),
                caret_x,
                round_to_int(center_y + 8.0F * surface.m_dpi_scale),
                surface.m_palette.text_primary);
        }
    }
}

UI::Editor::TextPosition TextEditor::position_from_point(
    const StudioWorkspaceRenderer& surface,
    HDC device_context,
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_x,
    float point_y) const
{
    const UI::Editor::TextDocumentModel* document = m_controller.get_active_document();
    if (document == nullptr)
    {
        return {};
    }
    const float line_height = 20.0F * surface.m_dpi_scale;
    const std::size_t visible_count = static_cast<std::size_t>(std::max(
        static_cast<int>(layout.editor_bounds.height / line_height), 1));
    m_scrollbar.synchronize(document->get_line_count(), visible_count);
    const std::size_t first_line = m_scrollbar.get_first_visible_line();
    const float clamped_y = std::clamp(
        point_y, layout.editor_bounds.y, std::max(layout.editor_bounds.bottom() - 1.0F, layout.editor_bounds.y));
    const std::size_t clicked_row = static_cast<std::size_t>(std::max(
        static_cast<int>((clamped_y - layout.editor_bounds.y) / line_height), 0));
    const std::size_t line_index = std::min(
        first_line + clicked_row, document->get_line_count() - 1);
    const std::string_view line = document->get_line(line_index);
    const float code_x = layout.editor_bounds.x + 14.0F * surface.m_dpi_scale;
    const float target_x = std::max(point_x - code_x, 0.0F);
    std::size_t column = 0;
    int previous_width = 0;
    while (column < line.size())
    {
        const std::size_t next_column = next_character_column(line, column);
        const int next_width = surface.get_text_width(
            device_context, *surface.m_editor_font, line.substr(0, next_column));
        if (target_x < static_cast<float>(previous_width + next_width) * 0.5F)
        {
            break;
        }
        column = next_column;
        previous_width = next_width;
    }
    return {line_index, column};
}

} // namespace Zenvra::Platform::Win32::Components
