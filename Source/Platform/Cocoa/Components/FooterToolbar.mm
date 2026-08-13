#include "Platform/Cocoa/Components/FooterToolbar.h"
#include "Platform/Cocoa/Components/StudioWorkspaceRenderer.h"
#include "UI/Editor/FileIconModel.h"
#include "Utility/Fonts.h"

#include <cmath>
#include <string>

namespace Zenvra::Platform::Cocoa::Components {

static int round_to_int(float value) {
  return static_cast<int>(std::lround(value));
}

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
    const StudioWorkspaceRenderer &surface, CGContextRef context,
    const UI::Editor::StudioEditorLayoutResult &layout,
    std::span<const UI::Editor::BreadcrumbItem> breadcrumbs,
    const UI::Editor::FooterEditorStatus &status) const {

  const float center_y =
      layout.status_bar_bounds.y + layout.status_bar_bounds.height * 0.5F;
  const std::string status_text =
      std::to_string(status.line) + ":" + std::to_string(status.column) +
      "   " + std::string{status.line_ending} + "   " +
      std::string{status.encoding} + "   " +
      std::to_string(status.indent_width) + " spaces";
  const float status_x =
      layout.status_bar_bounds.right() -
      static_cast<float>(surface.m_small_font->getTextWidth(status_text)) -
      12.0F * surface.m_dpi_scale;
  surface.draw_text(context, *surface.m_small_font, status_text, status_x,
                    center_y, surface.m_text.muted);

  float breadcrumb_x = 11.0F * surface.m_dpi_scale;
  const int icon_size = std::max(round_to_int(11.0F * surface.m_dpi_scale), 8);
  const float icon_advance =
      static_cast<float>(icon_size) + 4.0F * surface.m_dpi_scale;

  bool first = true;
  for (const UI::Editor::BreadcrumbItem &item : breadcrumbs) {
    const float separator_width = first ? 0.0F : 13.0F * surface.m_dpi_scale;
    const float text_width =
        static_cast<float>(surface.m_small_font->getTextWidth(item.text));
    const float total_width = icon_advance + text_width;
    if (breadcrumb_x + separator_width + total_width >
        status_x - 18.0F * surface.m_dpi_scale) {
      surface.draw_text(context, *surface.m_small_font, "...", breadcrumb_x,
                        center_y, surface.m_text.muted);
      break;
    }
    if (!first) {
      const int chevron_size =
          std::max(round_to_int(12.0F * surface.m_dpi_scale), 10);
      surface.draw_svg_icon(context, "Assets/icons/chevron-right.svg",
                            round_to_int(breadcrumb_x + separator_width * 0.5F),
                            round_to_int(center_y), chevron_size,
                            surface.m_palette.text_muted,
                            surface.m_palette.status_background);
      breadcrumb_x += separator_width;
    }

    const std::string svg_path = breadcrumb_icon_svg(item.icon, item.text);
    surface.draw_svg_icon(
        context, svg_path,
        round_to_int(breadcrumb_x + static_cast<float>(icon_size) * 0.5F),
        round_to_int(center_y), icon_size, surface.m_palette.text_muted,
        surface.m_palette.status_background);
    breadcrumb_x += icon_advance;

    surface.draw_text(context, *surface.m_small_font, item.text, breadcrumb_x,
                      center_y, surface.m_text.muted);
    breadcrumb_x +=
        static_cast<float>(surface.m_small_font->getTextWidth(item.text)) +
        8.0F * surface.m_dpi_scale;
    first = false;
  }
}

} // namespace Zenvra::Platform::Cocoa::Components
