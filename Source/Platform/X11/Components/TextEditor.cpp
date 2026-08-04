#include "Platform/X11/Components/TextEditor.h"

#include "Platform/X11/Components/StudioWorkspaceRenderer.h"
#include "UI/Editor/FileIconModel.h"
#include "Utility/Fonts.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace Zenvra::Platform::X11::Components
{

namespace
{

int round_to_int(float value)
{
    return static_cast<int>(std::lround(value));
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
        m_hovered_tab_index.reset();
        m_hovered_tab_close_index.reset();
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
        m_hovered_tab_index.reset();
        m_hovered_tab_close_index.reset();
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
        m_hovered_tab_index.reset();
        m_hovered_tab_close_index.reset();
    }
    return created;
}

bool TextEditor::is_tab_interactive_point(
    const StudioWorkspaceRenderer& surface,
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_x,
    float point_y) const noexcept
{
    if (!layout.tab_bar_bounds.contains(point_x, point_y))
    {
        return false;
    }

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
    if (create_bounds.contains(point_x, point_y) || delete_bounds.contains(point_x, point_y))
    {
        return true;
    }

    float tab_x = layout.tab_bar_bounds.x;
    const float right_limit = create_bounds.x;
    const std::span<const UI::Editor::EditorSessionDocument> documents =
        m_controller.get_documents();
    for (const UI::Editor::EditorSessionDocument& document : documents)
    {
        const float width = UI::Editor::calculate_editor_tab_width(
            static_cast<float>(surface.m_ui_font->getTextWidth(
                std::string{document.text.get_file_name()})),
            surface.m_dpi_scale);
        if (tab_x + width > right_limit)
        {
            break;
        }
        const UI::Rect bounds{
            tab_x, layout.tab_bar_bounds.y, width, layout.tab_bar_bounds.height};
        if (bounds.contains(point_x, point_y))
        {
            return true;
        }
        tab_x += width +
            UI::Editor::StudioEditorMetrics::editor_tab_gap * surface.m_dpi_scale;
    }
    return false;
}

bool TextEditor::handle_pointer_press(
    const StudioWorkspaceRenderer& surface,
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
                static_cast<float>(surface.m_ui_font->getTextWidth(
                    std::string{documents[index].text.get_file_name()})),
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
                        m_hovered_tab_index.reset();
                        m_hovered_tab_close_index.reset();
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
        // Only consume the titlebar when a real tab/action was hit. Empty
        // space is intentionally left for the native window drag region.
        return false;
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
        surface, layout, point_x, point_y);
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
    const bool scrollbar_changed = m_scrollbar.set_hovered(layout, point_x, point_y);
    std::optional<std::size_t> hovered_tab;
    std::optional<std::size_t> hovered_close;
    const float close_width =
        UI::Editor::StudioEditorMetrics::editor_tab_close_width * layout.dpi_scale;
    for (std::size_t index = 0; index < m_tab_count; ++index)
    {
        const UI::Rect& tab_bounds = m_tab_bounds[index];
        const UI::Rect close_bounds{
            tab_bounds.right() - close_width,
            tab_bounds.y,
            close_width,
            tab_bounds.height};
        if (tab_bounds.contains(point_x, point_y))
        {
            hovered_tab = index;
            if (close_bounds.contains(point_x, point_y))
            {
                hovered_close = index;
            }
            break;
        }
    }
    if (hovered_tab != m_hovered_tab_index || hovered_close != m_hovered_tab_close_index)
    {
        m_hovered_tab_index = hovered_tab;
        m_hovered_tab_close_index = hovered_close;
        return true;
    }
    return scrollbar_changed;
}

bool TextEditor::handle_pointer_drag(
    const StudioWorkspaceRenderer& surface,
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
        surface, layout, point_x, point_y);
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
        if (action == UI::Editor::EditorAction::CreateDocument ||
            action == UI::Editor::EditorAction::CloseDocument ||
            action == UI::Editor::EditorAction::RemoveDocument)
        {
            m_hovered_tab_index.reset();
            m_hovered_tab_close_index.reset();
        }
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
    Drawable drawable,
    const UI::Editor::StudioEditorLayoutResult& layout) const
{
    draw_tab_strip(surface, drawable, layout);
    draw_document(surface, drawable, layout);
    if (const UI::Editor::TextDocumentModel* document = m_controller.get_active_document())
    {
        const float line_height = 20.0F * surface.m_dpi_scale;
        const std::size_t visible_count = static_cast<std::size_t>(std::max(
            static_cast<int>(layout.editor_bounds.height / line_height), 1));
        m_minimap.render(
            surface,
            drawable,
            layout,
            *document,
            m_scrollbar.get_first_visible_line(),
            visible_count);
    }
    m_scrollbar.render(surface, drawable, layout);
}

void TextEditor::draw_tab_strip(
    const StudioWorkspaceRenderer& surface,
    Drawable drawable,
    const UI::Editor::StudioEditorLayoutResult& layout) const
{
    m_tab_count = 0;
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
            static_cast<float>(surface.m_ui_font->getTextWidth(
                std::string{document.get_file_name()})),
            surface.m_dpi_scale);
        if (tab_x + width > right_limit)
        {
            break;
        }
        const UI::Rect bounds{tab_x, layout.tab_bar_bounds.y, width, layout.tab_bar_bounds.height};
        const std::size_t tab_index = m_tab_count;
        if (m_tab_count < max_visible_tabs)
        {
            m_tab_bounds[m_tab_count] = bounds;
            ++m_tab_count;
        }
        const bool close_hovered = m_hovered_tab_close_index &&
            *m_hovered_tab_close_index == tab_index;
        const bool tab_hovered = m_hovered_tab_index &&
            *m_hovered_tab_index == tab_index;
        if (active || tab_hovered)
        {
            surface.fill_rectangle(
                drawable,
                bounds,
                active ? surface.m_pixels.tab_active_background
                       : surface.m_pixels.active_line_background);
        }
        const unsigned long tab_edge_color = surface.m_pixels.text_primary;
        const int tab_left = round_to_int(bounds.x);
        const int tab_right = round_to_int(bounds.right()) - 1;
        const int tab_top = round_to_int(bounds.y);
        const int tab_bottom = round_to_int(bounds.bottom()) - 1;
        // Keep one flush top rule and vertical separators; there is no bottom
        // rule, so the titlebar remains visually open below the labels.
        surface.draw_line(drawable, tab_left, tab_top, tab_right, tab_top,
            tab_edge_color);
        surface.draw_line(drawable, tab_left, tab_top, tab_left, tab_bottom,
            tab_edge_color);
        surface.draw_line(drawable, tab_right, tab_top, tab_right, tab_bottom,
            tab_edge_color);
        const std::string icon_asset = UI::Editor::file_icon_asset_for_path(
            std::filesystem::path{std::string{document.get_file_name()}});
        surface.draw_svg_icon(drawable, icon_asset,
            round_to_int(bounds.x +
                (UI::Editor::StudioEditorMetrics::editor_tab_icon_offset + 4.0F) *
                    surface.m_dpi_scale),
            round_to_int(bounds.y + bounds.height * 0.5F),
            std::max(round_to_int(14.0F * surface.m_dpi_scale), 10),
            surface.m_palette.text_primary,
            surface.m_pixels.tab_background,
            false);
        surface.draw_text(drawable, *surface.m_ui_font, document.get_file_name(),
            bounds.x + UI::Editor::StudioEditorMetrics::editor_tab_label_offset *
                surface.m_dpi_scale,
            bounds.y + bounds.height * 0.5F,
            active ? surface.m_text.primary : surface.m_text.muted);
        if (close_hovered)
        {
            surface.draw_svg_icon(
                drawable,
                "close-minimal.svg",
                round_to_int(bounds.right() -
                    UI::Editor::StudioEditorMetrics::editor_tab_close_width *
                        0.5F * surface.m_dpi_scale),
                round_to_int(bounds.y + bounds.height * 0.5F),
                std::max(round_to_int(11.0F * surface.m_dpi_scale), 9),
                active ? surface.m_palette.text_primary : surface.m_palette.text_muted,
                surface.m_pixels.tab_background);
        }
        else if (document.is_dirty())
        {
            surface.draw_svg_icon(
                drawable,
                "dirty.svg",
                round_to_int(bounds.right() -
                    UI::Editor::StudioEditorMetrics::editor_tab_close_width *
                        0.5F * surface.m_dpi_scale),
                round_to_int(bounds.y + bounds.height * 0.5F),
                std::max(round_to_int(10.0F * surface.m_dpi_scale), 8),
                surface.m_palette.warning,
                surface.m_pixels.tab_background);
        }
        tab_x += width +
            UI::Editor::StudioEditorMetrics::editor_tab_gap * surface.m_dpi_scale;
    }

    const int action_center_y = round_to_int(
        layout.tab_bar_bounds.y + layout.tab_bar_bounds.height * 0.5F);
    const int add_center_x = round_to_int(layout.tab_bar_bounds.right() - action_width * 1.5F);
    const int action_half = std::max(round_to_int(4.0F * surface.m_dpi_scale), 3);
    surface.draw_line(drawable, add_center_x - action_half, action_center_y,
        add_center_x + action_half, action_center_y, surface.m_pixels.text_muted);
    surface.draw_line(drawable, add_center_x, action_center_y - action_half,
        add_center_x, action_center_y + action_half, surface.m_pixels.text_muted);

    const int delete_center_x = round_to_int(layout.tab_bar_bounds.right() - action_width * 0.5F);
    const int trash_half = std::max(round_to_int(4.0F * surface.m_dpi_scale), 3);
    surface.draw_rectangle(drawable,
        UI::Rect{static_cast<float>(delete_center_x - trash_half),
            static_cast<float>(action_center_y - trash_half + 2),
            static_cast<float>(trash_half * 2),
            static_cast<float>(trash_half * 2)},
        surface.m_pixels.text_muted);
    surface.draw_line(drawable, delete_center_x - trash_half - 1,
        action_center_y - trash_half, delete_center_x + trash_half + 1,
        action_center_y - trash_half, surface.m_pixels.text_muted);
}

void TextEditor::draw_document(
    const StudioWorkspaceRenderer& surface,
    Drawable drawable,
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
        -> const std::string& {
        switch (kind)
        {
        case UI::Editor::EditorTokenKind::Keyword: return surface.m_text.keyword;
        case UI::Editor::EditorTokenKind::Number: return surface.m_text.number;
        case UI::Editor::EditorTokenKind::Label: return surface.m_text.label;
        case UI::Editor::EditorTokenKind::Type: return surface.m_text.type;
        case UI::Editor::EditorTokenKind::Comment: return surface.m_text.comment;
        case UI::Editor::EditorTokenKind::String: return surface.m_text.success;
        case UI::Editor::EditorTokenKind::Plain: return surface.m_text.primary;
        }
        return surface.m_text.primary;
    };
    surface.draw_line(drawable, round_to_int(layout.gutter_bounds.right() - 1.0F),
        round_to_int(layout.gutter_bounds.y), round_to_int(layout.gutter_bounds.right() - 1.0F),
        round_to_int(layout.gutter_bounds.bottom()), surface.m_pixels.border);

    for (std::size_t row = 0; row < render_count; ++row)
    {
        const std::size_t line_index = first_line + row;
        const std::string_view line = document->get_line(line_index);
        const float center_y = first_center_y + static_cast<float>(row) * line_height;
        const bool active_line = line_index == document->get_caret_line();
        if (active_line)
        {
            surface.fill_rectangle(drawable,
                UI::Rect{layout.gutter_bounds.x, center_y - line_height * 0.5F,
                    layout.editor_bounds.right() - layout.gutter_bounds.x, line_height},
                surface.m_pixels.active_line_background);
        }
        const std::string number = std::to_string(line_index + 1);
        const float number_x = layout.gutter_bounds.right() - 24.0F * surface.m_dpi_scale -
            static_cast<float>(surface.m_small_font->getTextWidth(number));
        surface.draw_text(drawable, *surface.m_small_font, number, number_x, center_y,
            active_line ? surface.m_text.primary : surface.m_text.muted);
        if (has_gutter_marker(line))
        {
            const int marker_x = round_to_int(layout.gutter_bounds.right() - 13.0F * surface.m_dpi_scale);
            const int marker_y = round_to_int(center_y);
            const int half = std::max(round_to_int(3.0F * surface.m_dpi_scale), 2);
            XPoint points[]{
                XPoint{static_cast<short>(marker_x), static_cast<short>(marker_y - half)},
                XPoint{static_cast<short>(marker_x + half), static_cast<short>(marker_y)},
                XPoint{static_cast<short>(marker_x), static_cast<short>(marker_y + half)},
                XPoint{static_cast<short>(marker_x - half), static_cast<short>(marker_y)},
                XPoint{static_cast<short>(marker_x), static_cast<short>(marker_y - half)},
            };
            XSetForeground(surface.m_display, surface.m_graphics_context, surface.m_pixels.text_muted);
            XDrawLines(surface.m_display, drawable, surface.m_graphics_context, points, 5, CoordModeOrigin);
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
                const float selection_x = code_x + static_cast<float>(
                    surface.m_editor_font->getTextWidth(
                        std::string{line.substr(0, selection_start)}));
                float selection_width = static_cast<float>(surface.m_editor_font->getTextWidth(
                    std::string{line.substr(selection_start, selection_end - selection_start)}));
                if (line_index < selection.end.line)
                {
                    selection_width += 6.0F * surface.m_dpi_scale;
                }
                selection_width = std::min(
                    selection_width, std::max(layout.editor_bounds.right() - selection_x, 0.0F));
                if (selection_width > 0.0F)
                {
                    surface.fill_rectangle(drawable,
                        UI::Rect{selection_x, center_y - line_height * 0.5F,
                            selection_width, line_height},
                        surface.m_pixels.selection_background);
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
                const std::string text{token.text};
                surface.draw_text(drawable, *surface.m_editor_font, text, token_x, center_y,
                    token_color(token.kind));
                token_x += static_cast<float>(surface.m_editor_font->getTextWidth(text));
                rendered_bytes += token.text.size();
            }
            if (rendered_bytes < line.size())
            {
                surface.draw_text(drawable, *surface.m_editor_font,
                    line.substr(rendered_bytes), token_x, center_y, surface.m_text.primary);
            }
        }
        else
        {
            surface.draw_text(drawable, *surface.m_editor_font, line, code_x, center_y,
                surface.m_text.primary);
        }
        if (active_line && m_focused && m_caret_blink.is_visible())
        {
            const std::string_view prefix = line.substr(0, document->get_caret_column());
            const int caret_x = round_to_int(code_x +
                static_cast<float>(surface.m_editor_font->getTextWidth(std::string{prefix})));
            surface.draw_line(drawable, caret_x,
                round_to_int(center_y - 8.0F * surface.m_dpi_scale), caret_x,
                round_to_int(center_y + 8.0F * surface.m_dpi_scale), surface.m_pixels.text_primary);
        }
    }
}

UI::Editor::TextPosition TextEditor::position_from_point(
    const StudioWorkspaceRenderer& surface,
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
        const int next_width = surface.m_editor_font->getTextWidth(
            std::string{line.substr(0, next_column)});
        if (target_x < static_cast<float>(previous_width + next_width) * 0.5F)
        {
            break;
        }
        column = next_column;
        previous_width = next_width;
    }
    return {line_index, column};
}

} // namespace Zenvra::Platform::X11::Components
