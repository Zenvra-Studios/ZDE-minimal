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

/**
 * 
 * 
 **/
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
    m_pointer_dragging = true;
    return handle_pointer_drag(layout, point_y, total_lines, visible_lines, first_visible_line);
}

std::optional<std::size_t> EditorMinimap::handle_pointer_drag(
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_y,
    std::size_t total_lines,
    std::size_t visible_lines,
    std::size_t /*first_visible_line*/) noexcept
{
    if (!m_pointer_dragging || total_lines <= 1)
    {
        return std::nullopt;
    }
    const UI::Rect bounds = layout.minimap_bounds;
    if (bounds.height <= 0.0F)
    {
        return std::nullopt;
    }
    const std::size_t max_first_line = (total_lines > visible_lines) ? (total_lines - visible_lines) : 0;
    if (max_first_line == 0)
    {
        return 0;
    }
    const float font_height = 3.0F * layout.dpi_scale;
    const float row_height = std::max(font_height, 2.0F * layout.dpi_scale);
    const float total_content_height = static_cast<float>(total_lines) * row_height;

    if (total_content_height <= bounds.height)
    {
        const float vp_h = std::clamp(
            static_cast<float>(visible_lines) * row_height,
            14.0F * layout.dpi_scale,
            bounds.height);
        const float click_y = point_y - bounds.y - vp_h * 0.5F;
        const float line_float = click_y / row_height;
        const std::size_t requested_first = static_cast<std::size_t>(
            std::clamp(std::lround(line_float), std::int64_t{0}, static_cast<std::int64_t>(max_first_line)));
        return requested_first;
    }
    else
    {
        const float vp_h = std::clamp(
            (static_cast<float>(visible_lines) / static_cast<float>(total_lines)) * bounds.height,
            18.0F * layout.dpi_scale,
            bounds.height);
        const float max_travel = std::max(bounds.height - vp_h, 1.0F);
        const float click_offset = point_y - bounds.y - vp_h * 0.5F;
        const float ratio = std::clamp(click_offset / max_travel, 0.0F, 1.0F);
        const std::size_t requested_first = static_cast<std::size_t>(
            std::lround(ratio * static_cast<float>(max_first_line)));
        return std::min(requested_first, max_first_line);
    }
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

    const std::size_t total_lines = document.get_line_count();
    m_model.synchronize(total_lines, visible_lines, first_visible_line);

    const float font_height = static_cast<float>(surface.m_minimap_font->getHeight());
    const float row_height = std::max(
        font_height * 0.55F,
        2.0F * layout.dpi_scale);
    const float total_content_height = static_cast<float>(total_lines) * row_height;

    float vp_h = 0.0F;
    float minimap_scroll_y = 0.0F;
    float slider_y = bounds.y;

    if (total_content_height <= bounds.height)
    {
        minimap_scroll_y = 0.0F;
        vp_h = std::clamp(
            static_cast<float>(visible_lines) * row_height,
            14.0F * layout.dpi_scale,
            bounds.height);
        slider_y = bounds.y + static_cast<float>(first_visible_line) * row_height;
    }
    else
    {
        vp_h = std::clamp(
            (static_cast<float>(visible_lines) / static_cast<float>(std::max(total_lines, std::size_t{1}))) * bounds.height,
            18.0F * layout.dpi_scale,
            bounds.height);

        const float max_doc_scroll = static_cast<float>(std::max(total_lines > visible_lines ? total_lines - visible_lines : std::size_t{0}, std::size_t{1}));
        const float scroll_progress = std::clamp(static_cast<float>(first_visible_line) / max_doc_scroll, 0.0F, 1.0F);

        minimap_scroll_y = scroll_progress * (total_content_height - bounds.height);
        const float max_slider_travel = std::max(bounds.height - vp_h, 0.0F);
        slider_y = bounds.y + scroll_progress * max_slider_travel;
    }

    // Viewport slider bounds with horizontal inset
    const float inset_x = 2.0F * layout.dpi_scale;
    const UI::Rect viewport{
        bounds.x + inset_x,
        slider_y,
        std::max(bounds.width - 2.0F * inset_x, 0.0F),
        vp_h,
    };

    surface.fill_rectangle(drawable, viewport, surface.m_pixels.active_line_background);

    surface.push_clip(bounds);

    const std::size_t start_line = static_cast<std::size_t>(
        std::max(0, static_cast<int>(std::floor(minimap_scroll_y / row_height))));
    const std::size_t end_line = std::min(
        total_lines,
        static_cast<std::size_t>(std::ceil((minimap_scroll_y + bounds.height) / row_height) + 1));

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

    for (std::size_t line_index = start_line; line_index < end_line; ++line_index)
    {
        const std::string_view line = document.get_line(line_index);
        const float center_y = bounds.y + (static_cast<float>(line_index) + 0.5F) * row_height - minimap_scroll_y;
        if (center_y < bounds.y - row_height || center_y > bounds.bottom() + row_height)
        {
            continue;
        }

        float token_x = bounds.x + left_padding;
        std::array<UI::Editor::EditorToken, UI::Editor::maximum_editor_tokens> tokens{};
        auto line_state = document.get_line_state(line_index);
        const std::size_t token_count = UI::Editor::tokenize_editor_line(line, tokens, document.get_file_name(), &line_state);
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

    // Render diagnostic tick marks on right border of minimap (VS Code style)
    if (total_lines > 0)
    {
        const auto all_diags = document.get_diagnostics();
        for (const auto& d : all_diags)
        {
            const std::size_t line_index = d.range.start.line;
            if (line_index >= total_lines) continue;
            const float center_y = bounds.y + (static_cast<float>(line_index) + 0.5F) * row_height - minimap_scroll_y;
            if (center_y < bounds.y - row_height || center_y > bounds.bottom() + row_height)
            {
                continue;
            }
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

    surface.pop_clip();

    surface.draw_rectangle(drawable, viewport,
        m_pointer_dragging ? surface.m_pixels.accent : surface.m_pixels.text_muted);
}

} // namespace Zenvra::Platform::X11::Components
