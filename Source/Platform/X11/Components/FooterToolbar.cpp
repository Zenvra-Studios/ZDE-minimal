#include "Platform/X11/Components/FooterToolbar.h"

#include "Platform/X11/Components/StudioWorkspaceRenderer.h"
#include "UI/Editor/FileIconModel.h"
#include "Utility/Fonts.h"
#include "Utility/MathUtil.h"

#include <cmath>
#include <string>

namespace Zenvra::Platform::X11::Components {

using Zenvra::Utility::round_to_int;

static std::string breadcrumb_icon_svg(UI::Editor::BreadcrumbIconKind kind,
                                       const std::string &file_name) {
  switch (kind) {
  case UI::Editor::BreadcrumbIconKind::Folder:
    return "Assets/icons/folder.svg";
  case UI::Editor::BreadcrumbIconKind::Namespace:
    return "Assets/icons/material-icon-theme/folder-interface.svg";
  case UI::Editor::BreadcrumbIconKind::Class:
  case UI::Editor::BreadcrumbIconKind::Struct:
    return "Assets/icons/material-icon-theme/folder-class.svg";
  case UI::Editor::BreadcrumbIconKind::Function:
    return "Assets/icons/material-icon-theme/folder-functions.svg";
  case UI::Editor::BreadcrumbIconKind::File: {
    const std::string asset =
        UI::Editor::file_icon_asset_for_path(std::filesystem::path{file_name});
    return "Assets/icons/" + asset;
  }
  }
  return "Assets/icons/file.svg";
}

void FooterToolbar::render(
    const StudioWorkspaceRenderer &surface, Drawable drawable,
    const UI::Editor::StudioEditorLayoutResult &layout,
    std::span<const UI::Editor::BreadcrumbItem> breadcrumbs,
    const UI::Editor::FooterEditorStatus &status) const {
  surface.fill_rectangle(drawable, layout.status_bar_bounds,
                         surface.m_pixels.status_background);
  surface.draw_line(drawable, 0, round_to_int(layout.status_bar_bounds.y),
                    round_to_int(layout.status_bar_bounds.right()),
                    round_to_int(layout.status_bar_bounds.y),
                    surface.m_pixels.border);
  const float scale = surface.m_dpi_scale;
  const float center_y =
      layout.status_bar_bounds.y + layout.status_bar_bounds.height * 0.5F;
  const std::string status_text =
      status.line > 0
          ? ("Ln " + std::to_string(status.line) + ", Col " + std::to_string(status.column) +
             "    " + std::string{status.line_ending} + "    " +
             std::string{status.encoding} + "    " +
             std::to_string(status.indent_width) + " spaces")
          : "UTF-8    Ready";
  const float status_x =
      layout.status_bar_bounds.right() -
      (surface.m_small_font
          ? static_cast<float>(surface.m_small_font->getTextWidth(status_text))
          : static_cast<float>(status_text.size()) * 7.0F * scale) -
      12.0F * scale;
  if (surface.m_small_font) {
    surface.draw_text(drawable, *surface.m_small_font, status_text, status_x,
                      center_y, surface.m_text.muted);
  }

  const float left_start = 12.0F * scale;
  const float available_w = status_x - 16.0F * scale - left_start;
  if (available_w > 0.0F && !breadcrumbs.empty()) {
    m_breadcrumb_bar.set_items(breadcrumbs);
    m_breadcrumb_bar.set_bounds(UI::Rect{
        left_start, layout.status_bar_bounds.y, available_w,
        layout.status_bar_bounds.height});
    auto text_width_fn = [&](std::string_view str) -> float {
      return surface.m_small_font
          ? static_cast<float>(surface.m_small_font->getTextWidth(std::string{str}))
          : static_cast<float>(str.size()) * 7.0F * scale;
    };
    const auto segments =
        m_breadcrumb_bar.calculate_layout(text_width_fn, scale, available_w);
    for (const auto &seg : segments) {
      if (!seg.is_ellipsis && !seg.icon_svg.empty()) {
        const int icon_cx =
            round_to_int(seg.icon_bounds.x + seg.icon_bounds.width * 0.5F);
        const int icon_cy =
            round_to_int(seg.icon_bounds.y + seg.icon_bounds.height * 0.5F);
        const int icon_sz = round_to_int(seg.icon_bounds.width);
        surface.draw_svg_icon(
            drawable, seg.icon_svg, icon_cx, icon_cy, icon_sz,
            surface.m_palette.text_muted, surface.m_palette.status_background);
      }

      if (surface.m_small_font) {
        surface.draw_text(drawable, *surface.m_small_font, seg.text,
                          seg.text_bounds.x, center_y,
                          surface.m_text.muted);
      }

      if (seg.has_chevron) {
        const int chev_cx =
            round_to_int(seg.chevron_bounds.x + seg.chevron_bounds.width * 0.5F);
        const int chev_cy =
            round_to_int(seg.chevron_bounds.y + seg.chevron_bounds.height * 0.5F);
        const int chev_sz = round_to_int(seg.chevron_bounds.width);
        surface.draw_svg_icon(
            drawable, "Assets/icons/chevron-right.svg", chev_cx, chev_cy,
            chev_sz, surface.m_palette.text_muted,
            surface.m_palette.status_background);
      }
    }
  }
}

} // namespace Zenvra::Platform::X11::Components
