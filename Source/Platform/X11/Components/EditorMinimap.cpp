#include "Platform/X11/Components/EditorMinimap.h"

#include "Platform/X11/Components/StudioWorkspaceRenderer.h"
#include "Language/Protocol/LspTypes.h"
#include "Utility/Fonts.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <string_view>

namespace Zenvra::Platform::X11::Components
{

namespace
{

std::string normalize_minimap_text(std::string_view text)
{
    std::string result;
    result.reserve(text.size());
    for (const char character : text)
    {
        if (character == '\t')
        {
            result.append(4, ' ');
        }
        else
        {
            result.push_back(character);
        }
    }
    return result;
}

} // namespace

bool EditorMinimap::is_point(
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_x,
    float point_y) const noexcept
{
    return layout.minimap_bounds.contains(point_x, point_y);
}

std::optional<std::size_t> EditorMinimap::handle_pointer_press(
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_x,
    float point_y,
    std::size_t total_lines,
    std::size_t visible_lines,
    std::size_t first_visible_line) noexcept
{
    if (!is_point(layout, point_x, point_y))
    {
        return std::nullopt;
    }
    m_model.synchronize(total_lines, visible_lines, first_visible_line);
    m_pointer_dragging = true;
    const float row_height = 2.0F * layout.dpi_scale;
    UI::Rect content_bounds = layout.minimap_bounds;
    content_bounds.height = std::min(layout.minimap_bounds.height, static_cast<float>(total_lines) * row_height);
    if (content_bounds.height <= 0.0F) content_bounds = layout.minimap_bounds;
    return m_model.get_first_visible_line_for_point(point_y, content_bounds);
}

std::optional<std::size_t> EditorMinimap::handle_pointer_drag(
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_y,
    std::size_t total_lines,
    std::size_t visible_lines,
    std::size_t first_visible_line) noexcept
{
    if (!m_pointer_dragging)
    {
        return std::nullopt;
    }
    m_model.synchronize(total_lines, visible_lines, first_visible_line);
    const float row_height = 2.0F * layout.dpi_scale;
    UI::Rect content_bounds = layout.minimap_bounds;
    content_bounds.height = std::min(layout.minimap_bounds.height, static_cast<float>(total_lines) * row_height);
    if (content_bounds.height <= 0.0F) content_bounds = layout.minimap_bounds;
    return m_model.get_first_visible_line_for_point(point_y, content_bounds);
}

bool EditorMinimap::handle_pointer_release() noexcept
{
    const bool was_dragging = m_pointer_dragging;
    m_pointer_dragging = false;
    return was_dragging;
}

void EditorMinimap::render(
    const StudioWorkspaceRenderer& surface,
    Drawable drawable,
    const UI::Editor::StudioEditorLayoutResult& layout,
    const UI::Editor::TextDocumentModel& document,
    std::size_t first_visible_line,
    std::size_t visible_lines) const
{
    const UI::Rect bounds = layout.minimap_bounds;
    if (bounds.is_empty())
    {
        return;
    }

    surface.fill_rectangle(drawable, bounds, surface.m_pixels.editor_background);
    surface.draw_line(
        drawable,
        static_cast<int>(std::lround(bounds.x)),
        static_cast<int>(std::lround(bounds.y)),
        static_cast<int>(std::lround(bounds.x)),
        static_cast<int>(std::lround(bounds.bottom())),
        surface.m_pixels.border);

    m_model.synchronize(document.get_line_count(), visible_lines, first_visible_line);

    const float font_height = static_cast<float>(surface.m_minimap_font->getHeight());
    const float row_height = std::max(
        font_height * 0.55F,
        2.0F * layout.dpi_scale);

    UI::Rect content_bounds = bounds;
    content_bounds.height = std::min(bounds.height, static_cast<float>(document.get_line_count()) * row_height);

    const UI::Rect viewport = m_model.calculate_viewport_bounds(
        content_bounds, 18.0F * layout.dpi_scale);
    surface.fill_rectangle(drawable, viewport, surface.m_pixels.active_line_background);

    const UI::Rect text_bounds{
        bounds.x,
        bounds.y + std::max((font_height - row_height) * 0.5F, 0.0F),
        bounds.width,
        std::max(bounds.height - std::max(font_height - row_height, 0.0F), 0.0F),
    };
    const std::size_t sample_count = m_model.calculate_sample_count(
        text_bounds, row_height);
    const float left_padding = 4.0F * layout.dpi_scale;
    const float right_padding = 4.0F * layout.dpi_scale;
    const auto token_color = [&surface](UI::Editor::EditorTokenKind kind)
        -> const std::string& {
        switch (kind)
        {
        case UI::Editor::EditorTokenKind::Keyword:
            return surface.m_text.keyword;
        case UI::Editor::EditorTokenKind::Number:
            return surface.m_text.number;
        case UI::Editor::EditorTokenKind::Label:
            return surface.m_text.label;
        case UI::Editor::EditorTokenKind::Type:
            return surface.m_text.type;
        case UI::Editor::EditorTokenKind::Comment:
            return surface.m_text.comment;
        case UI::Editor::EditorTokenKind::String:
            return surface.m_text.success;
        case UI::Editor::EditorTokenKind::Plain:
            return surface.m_text.primary;
        }
        return surface.m_text.primary;
    };
    for (std::size_t sample = 0; sample < sample_count; ++sample)
    {
        const std::size_t line_index = m_model.get_line_for_sample(sample, sample_count);
        const std::string_view line = document.get_line(line_index);
        const float center_y = text_bounds.y +
            (static_cast<float>(sample) + 0.5F) * row_height;
        float token_x = bounds.x + left_padding;
        std::array<UI::Editor::EditorToken, UI::Editor::maximum_editor_tokens> tokens{};
        const std::size_t token_count = UI::Editor::tokenize_editor_line(line, tokens, document.get_file_name());
        for (std::size_t token_index = 0; token_index < token_count; ++token_index)
        {
            if (token_x >= bounds.right() - right_padding)
            {
                break;
            }
            const UI::Editor::EditorToken& token = tokens[token_index];
            const std::string text = normalize_minimap_text(token.text);
            const int width = surface.m_minimap_font->getTextWidth(text);
            if (!text.empty() && text.find_first_not_of(' ') != std::string::npos)
            {
                surface.draw_text(
                    drawable,
                    *surface.m_minimap_font,
                    text,
                    token_x,
                    center_y,
                    token_color(token.kind));
            }
            token_x += static_cast<float>(width);
        }
        if (line_index == document.get_caret_line())
        {
            surface.fill_rectangle(
                drawable,
                UI::Rect{bounds.x + layout.dpi_scale,
                    center_y - row_height * 0.42F,
                    std::max(layout.dpi_scale, 1.0F),
                    row_height * 0.84F},
                surface.m_pixels.accent);
        }
    }

    // Render diagnostic tick marks on right border of minimap (VS Code / Cocoa style)
    if (document.get_line_count() > 0)
    {
        const auto all_diags = document.get_diagnostics();
        for (const auto& d : all_diags)
        {
            const std::size_t line_index = d.range.start.line;
            if (line_index >= document.get_line_count()) continue;
            const float center_y = text_bounds.y +
                (static_cast<float>(line_index) / static_cast<float>(document.get_line_count())) * text_bounds.height;
            const bool has_error = (d.severity == Language::Protocol::DiagnosticSeverity::Error);
            const bool has_warn = (d.severity == Language::Protocol::DiagnosticSeverity::Warning);
            const unsigned long diag_color = has_error
                ? surface.allocate_color(UI::Theme::Color{247, 84, 100, 255})
                : (has_warn ? surface.allocate_color(UI::Theme::Color{240, 167, 50, 255})
                            : surface.allocate_color(UI::Theme::Color{86, 182, 194, 255}));
            surface.fill_rectangle(
                drawable,
                UI::Rect{bounds.right() - 4.0F * layout.dpi_scale,
                         center_y - row_height * 0.45F,
                         3.0F * layout.dpi_scale,
                         std::max(row_height * 0.9F, 2.0F * layout.dpi_scale)},
                diag_color);
        }
    }

    surface.draw_rectangle(drawable, viewport,
        m_pointer_dragging ? surface.m_pixels.accent : surface.m_pixels.text_muted);
}

} // namespace Zenvra::Platform::X11::Components
