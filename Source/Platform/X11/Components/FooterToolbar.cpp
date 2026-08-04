#include "Platform/X11/Components/FooterToolbar.h"

#include "Platform/X11/Components/StudioWorkspaceRenderer.h"
#include "Utility/Fonts.h"

#include <cmath>
#include <string>

namespace Zenvra::Platform::X11::Components
{

static int round_to_int(float value)
{
    return static_cast<int>(std::lround(value));
}

void FooterToolbar::render(
    const StudioWorkspaceRenderer& surface,
    Drawable drawable,
    const UI::Editor::StudioEditorLayoutResult& layout,
    std::span<const std::string> breadcrumbs,
    const UI::Editor::FooterEditorStatus& status) const
{
    surface.draw_line(drawable, 0, round_to_int(layout.status_bar_bounds.y),
        round_to_int(layout.status_bar_bounds.right()), round_to_int(layout.status_bar_bounds.y),
        surface.m_pixels.border);
    const float center_y = layout.status_bar_bounds.y + layout.status_bar_bounds.height * 0.5F;
    const std::string status_text = std::to_string(status.line) + ":" + std::to_string(status.column) +
        "   " + std::string{status.line_ending} + "   " + std::string{status.encoding} +
        "   " + std::to_string(status.indent_width) + " spaces";
    const float status_x = layout.status_bar_bounds.right() -
        static_cast<float>(surface.m_small_font->getTextWidth(status_text)) -
        12.0F * surface.m_dpi_scale;
    surface.draw_text(drawable, *surface.m_small_font, status_text, status_x,
        center_y, surface.m_text.muted);

    float breadcrumb_x = 11.0F * surface.m_dpi_scale;
    surface.fill_rectangle(
        drawable,
        UI::Rect{breadcrumb_x, center_y - 4.5F * surface.m_dpi_scale,
            9.0F * surface.m_dpi_scale, 9.0F * surface.m_dpi_scale},
        surface.m_pixels.accent);
    breadcrumb_x += 16.0F * surface.m_dpi_scale;

    bool first = true;
    for (const std::string& breadcrumb : breadcrumbs)
    {
        const float separator_width = first ? 0.0F : 13.0F * surface.m_dpi_scale;
        const float breadcrumb_width = static_cast<float>(
            surface.m_small_font->getTextWidth(breadcrumb));
        if (breadcrumb_x + separator_width + breadcrumb_width >
            status_x - 18.0F * surface.m_dpi_scale)
        {
            surface.draw_text(drawable, *surface.m_small_font, "...", breadcrumb_x,
                center_y, surface.m_text.muted);
            break;
        }
        if (!first)
        {
            surface.draw_text(drawable, *surface.m_small_font, ">", breadcrumb_x,
                center_y, surface.m_text.muted);
            breadcrumb_x += separator_width;
        }
        surface.draw_text(drawable, *surface.m_small_font, breadcrumb, breadcrumb_x,
            center_y, surface.m_text.muted);
        breadcrumb_x += static_cast<float>(surface.m_small_font->getTextWidth(std::string{breadcrumb})) +
            8.0F * surface.m_dpi_scale;
        first = false;
    }

}

} // namespace Zenvra::Platform::X11::Components
