#include "Platform/X11/Components/ToolSidebar.h"

#include "Platform/X11/Components/StudioWorkspaceRenderer.h"
#include "UI/Editor/FileIconModel.h"
#include "Utility/Flex.h"
#include "Utility/Fonts.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>

namespace Zenvra::Platform::X11::Components {

namespace {

int round_to_int(float value) { return static_cast<int>(std::lround(value)); }

std::string ellipsize(AntialiasedFont &font, std::string text,
                      int maximum_width) {
  if (font.getTextWidth(text) <= maximum_width) {
    return text;
  }
  constexpr std::string_view suffix = "...";
  while (!text.empty() &&
         font.getTextWidth(text + std::string{suffix}) > maximum_width) {
    text.pop_back();
    while (!text.empty() &&
           (static_cast<unsigned char>(text.back()) & 0xC0U) == 0x80U) {
      text.pop_back();
    }
    if (!text.empty() && static_cast<unsigned char>(text.back()) >= 0x80U) {
      text.pop_back();
    }
  }
  return text + std::string{suffix};
}

} // namespace

bool ToolSidebar::initialize() {
  m_hovered_row.reset();
  m_hovered_icon.reset();
  m_hovered_scrollbar = false;
  return m_model.initialize();
}

bool ToolSidebar::set_workspace_root(const std::filesystem::path &root) {
  m_hovered_row.reset();
  m_hovered_scrollbar = false;
  return m_model.initialize(root);
}

bool ToolSidebar::activate(UI::Editor::SidebarIcon icon) noexcept {
  m_hovered_row.reset();
  m_hovered_scrollbar = false;
  return m_model.activate(icon);
}

bool ToolSidebar::handle_pointer_press(
    const UI::Editor::StudioEditorLayoutResult &layout, float point_x,
    float point_y, std::optional<std::filesystem::path> &file_to_open) {
  if (is_resize_handle_point(layout, point_x, point_y)) {
    m_resizing = true;
    m_drag_start_x = point_x;
    m_drag_start_width = m_width;
    return true;
  }
  if (!contains(layout, point_x, point_y)) {
    return false;
  }
  if (m_model.get_active_icon() == UI::Editor::SidebarIcon::Project &&
      m_model.get_project_items().size() > viewport_row_count(layout) &&
      scrollbar_bounds(layout).contains(point_x, point_y)) {
    return true;
  }

  if (m_model.get_active_icon() == UI::Editor::SidebarIcon::Project &&
      m_model.get_project_items().empty()) {
    const float scale = layout.dpi_scale;
    const UI::Rect panel = layout.tool_sidebar_bounds;
    const float tree_top = panel.y + header_height * scale;
    const float content_y = tree_top + 22.0F * scale;

    float btn_y = content_y + 14.0F * scale;
    const float btn_w = std::max(panel.width - 28.0F * scale, 0.0F);
    const float btn_h = 28.0F * scale;
    const float btn_x = panel.x + 14.0F * scale;

    m_empty_state_open_btn.set_bounds(UI::Rect{btn_x, btn_y, btn_w, btn_h});
    btn_y += btn_h + 20.0F * scale + 14.0F * scale;
    m_empty_state_clone_btn.set_bounds(UI::Rect{btn_x, btn_y, btn_w, btn_h});

    if (m_empty_state_open_btn.handle_pointer_press(point_x, point_y)) {
      file_to_open = "::OPEN_FOLDER::";
      return true;
    }
    if (m_empty_state_clone_btn.handle_pointer_press(point_x, point_y)) {
      return true;
    }
  }
  const float scale = layout.dpi_scale;
  if (m_model.get_active_icon() == UI::Editor::SidebarIcon::Project) {
      if (m_explorer_header.handle_pointer_press(layout, point_x, point_y, m_model, file_to_open)) {
          return true;
      }
  }
  const float tree_top = layout.tool_sidebar_bounds.y + header_height * scale;
  const auto sticky = get_sticky_items();
  const float sticky_height =
      static_cast<float>(sticky.size()) * row_height * scale;

  if (m_model.get_active_icon() == UI::Editor::SidebarIcon::Project &&
      point_y >= tree_top && point_y < tree_top + sticky_height) {
    std::size_t sticky_index = static_cast<std::size_t>(
        (point_y - tree_top) / (row_height * scale));
    if (sticky_index < sticky.size()) {
      m_model.set_scroll_offset(sticky[sticky_index]);
      return true;
    }
  }

  const std::optional<std::size_t> row = row_from_point(layout, point_y);
  if (row && m_model.get_active_icon() == UI::Editor::SidebarIcon::Project) {
    const UI::Editor::ActivityPanelAction action =
        m_model.activate_project_row(*row);
    file_to_open = action.file_to_open;
    return action.handled;
  }
  return true;
}

bool ToolSidebar::handle_pointer_move(
    const UI::Editor::StudioEditorLayoutResult &layout, float point_x,
    float point_y) noexcept {
  std::optional<UI::Editor::SidebarIcon> next_icon;
  if (const std::optional<std::size_t> sidebar_index =
          UI::Editor::hit_test_studio_sidebar(layout, point_x, point_y)) {
    next_icon = UI::Editor::get_studio_sidebar_items()[*sidebar_index].icon;
  }
  std::optional<std::size_t> next_sticky_hover;
  std::optional<std::size_t> next_row;
  bool next_scrollbar = false;

  if (m_model.get_active_icon() == UI::Editor::SidebarIcon::Project &&
      m_model.get_project_items().empty()) {
    bool changed = false;
    changed |= m_empty_state_open_btn.handle_pointer_move(point_x, point_y);
    changed |= m_empty_state_clone_btn.handle_pointer_move(point_x, point_y);
    if (changed)
      return true;
  }

  if (contains(layout, point_x, point_y) &&
      m_model.get_active_icon() == UI::Editor::SidebarIcon::Project) {
    const float scale = layout.dpi_scale;
    const float tree_top = layout.tool_sidebar_bounds.y + header_height * scale;
    const auto sticky = get_sticky_items();
    const float sticky_height =
        static_cast<float>(sticky.size()) * row_height * scale;

    if (point_y >= tree_top && point_y < tree_top + sticky_height) {
      std::size_t sticky_index = static_cast<std::size_t>(
          (point_y - tree_top) / (row_height * scale));
      if (sticky_index < sticky.size()) {
        next_sticky_hover = sticky[sticky_index];
      }
    } else {
      next_row = row_from_point(layout, point_y);
    }
    next_scrollbar =
        scrollbar_bounds(layout).contains(point_x, point_y) &&
        m_model.get_project_items().size() > viewport_row_count(layout);
  }

  const bool next_resize_hovered =
      is_resize_handle_point(layout, point_x, point_y);

  bool header_changed = false;
  if (m_model.get_active_icon() == UI::Editor::SidebarIcon::Project) {
      header_changed = m_explorer_header.handle_pointer_move(layout, point_x, point_y);
  }

  const bool changed = next_row != m_hovered_row ||
                       next_sticky_hover != m_hovered_sticky_index ||
                       next_icon != m_hovered_icon ||
                       next_scrollbar != m_hovered_scrollbar ||
                       next_resize_hovered != m_resize_hovered ||
                       header_changed;
  m_hovered_row = next_row;
  m_hovered_sticky_index = next_sticky_hover;
  m_hovered_icon = next_icon;
  m_hovered_scrollbar = next_scrollbar;
  m_resize_hovered = next_resize_hovered;
  return changed;
}

bool ToolSidebar::handle_scroll(
    const UI::Editor::StudioEditorLayoutResult &layout,
    std::ptrdiff_t line_delta) noexcept {
  m_hovered_row.reset();
  m_hovered_sticky_index.reset();
  return m_model.scroll(line_delta, viewport_row_count(layout));
}

bool ToolSidebar::is_visible() const noexcept { return m_model.is_visible(); }
bool ToolSidebar::is_active(UI::Editor::SidebarIcon icon) const noexcept {
  return m_model.is_active(icon);
}
bool ToolSidebar::is_hovered(UI::Editor::SidebarIcon icon) const noexcept {
  return m_hovered_icon && *m_hovered_icon == icon;
}

bool ToolSidebar::contains(const UI::Editor::StudioEditorLayoutResult &layout,
                           float point_x, float point_y) const noexcept {
  return is_visible() && layout.tool_sidebar_bounds.contains(point_x, point_y);
}

bool ToolSidebar::is_resize_handle_point(
    const UI::Editor::StudioEditorLayoutResult &layout, float point_x,
    float point_y) const noexcept {
  const float scale = layout.dpi_scale;
  const UI::Rect handle_bounds{
      layout.tool_sidebar_bounds.right() - 3.0F * scale,
      layout.tool_sidebar_bounds.y, 6.0F * scale,
      layout.tool_sidebar_bounds.height};
  return handle_bounds.contains(point_x, point_y);
}

bool ToolSidebar::is_resizing() const noexcept { return m_resizing; }

float ToolSidebar::get_width() const noexcept { return m_width; }

bool ToolSidebar::handle_pointer_drag(
    const UI::Editor::StudioEditorLayoutResult &layout,
    float point_x) noexcept {
  if (!m_resizing) {
    return false;
  }

  const float delta = point_x - m_drag_start_x;
  float new_width = m_drag_start_width + delta / layout.dpi_scale;

  if (!m_model.is_visible()) {
    if (new_width >= 100.0F) {
      m_model.set_visible(true);
      m_width = new_width;
    } else {
      m_width = 0.0F;
    }
  } else {
    if (new_width < 100.0F) {
      m_model.set_visible(false);
      m_width = 0.0F;
    } else {
      m_width = new_width;
    }
  }

  return true;
}

bool ToolSidebar::handle_pointer_release() noexcept {
  if (m_resizing) {
    m_resizing = false;
    return true;
  }
  return false;
}

void ToolSidebar::render(
    const StudioWorkspaceRenderer &surface, Drawable drawable,
    const UI::Editor::StudioEditorLayoutResult &layout) const {
  const UI::Rect panel = layout.tool_sidebar_bounds;
  const float scale = layout.dpi_scale;
  if (!is_visible() || panel.is_empty()) {
    if (m_resize_hovered || m_resizing) {
      surface.draw_line(drawable, round_to_int(panel.right() - scale),
                        round_to_int(panel.y), round_to_int(panel.right() - scale),
                        round_to_int(panel.bottom()), surface.m_pixels.accent);
      surface.fill_rectangle(drawable,
          UI::Rect{
              panel.right() - scale - scale,
              panel.y,
              std::max(2.0F * scale, 2.0F),
              panel.height},
          surface.m_pixels.accent);
    }
    return;
  }
  surface.fill_rectangle(drawable, panel, surface.m_pixels.sidebar_background);
  const unsigned long border_color =
      (m_resize_hovered || m_resizing) ? surface.m_pixels.accent
                                       : surface.m_pixels.border;
  surface.draw_line(drawable, round_to_int(panel.right() - scale),
                    round_to_int(panel.y), round_to_int(panel.right() - scale),
                    round_to_int(panel.bottom()), border_color);
  if (m_resize_hovered || m_resizing) {
    surface.fill_rectangle(drawable,
        UI::Rect{
            panel.right() - scale - scale,
            panel.y,
            std::max(2.0F * scale, 2.0F),
            panel.height},
        surface.m_pixels.accent);
  }
  if (m_model.get_active_icon() == UI::Editor::SidebarIcon::Project) {
    m_explorer_header.render(surface, drawable, layout, std::string{m_model.get_title()});
  } else {
    surface.draw_text(drawable, *surface.m_ui_font, m_model.get_title(),
                      panel.x + 14.0F * scale,
                      panel.y + header_height * 0.5F * scale,
                      surface.m_text.primary);

    const int more_center_x = round_to_int(panel.right() - 17.0F * scale);
    const int header_center_y = round_to_int(panel.y + header_height * 0.5F * scale);
    surface.draw_svg_icon(drawable, "Assets/icons/ellipsis.svg", more_center_x,
                          header_center_y,
                          std::max(round_to_int(15.0F * scale), 11),
                          surface.m_palette.text_muted,
                          surface.m_palette.sidebar_background);
  }
  surface.draw_line(drawable, round_to_int(panel.x),
                    round_to_int(panel.y + header_height * scale),
                    round_to_int(panel.right()),
                    round_to_int(panel.y + header_height * scale),
                    surface.m_pixels.border);

  if (m_model.get_active_icon() != UI::Editor::SidebarIcon::Project) {
    const float content_y = panel.y + (header_height + 22.0F) * scale;
    if (m_model.get_active_icon() == UI::Editor::SidebarIcon::Search) {
      const UI::Rect search_bounds{
          panel.x + 12.0F * scale,
          content_y,
          std::max(panel.width - 24.0F * scale, 0.0F),
          28.0F * scale,
      };
      surface.fill_rectangle(drawable, search_bounds,
                             surface.m_pixels.editor_background);
      surface.draw_rectangle(drawable, search_bounds, surface.m_pixels.border);
      surface.draw_svg_icon(drawable, "Assets/icons/search.svg",
                            round_to_int(search_bounds.x + 16.0F * scale),
                            round_to_int(search_bounds.y +
                                         search_bounds.height * 0.5F),
                            std::max(round_to_int(13.0F * scale), 11),
                            surface.m_palette.text_muted,
                            surface.m_palette.editor_background);
      surface.draw_text(drawable, *surface.m_small_font, "Search files...",
                        search_bounds.x + 28.0F * scale,
                        search_bounds.y + search_bounds.height * 0.5F,
                        surface.m_text.muted);
    }
    const float message_y =
        content_y +
        (m_model.get_active_icon() == UI::Editor::SidebarIcon::Search ? 50.0F
                                                                      : 0.0F) *
            scale;
    surface.draw_text(drawable, *surface.m_ui_font,
                      m_model.get_content_heading(), panel.x + 14.0F * scale,
                      message_y, surface.m_text.primary);
    const std::string detail = ellipsize(
        *surface.m_small_font, std::string{m_model.get_content_detail()},
        std::max(round_to_int(panel.width - 28.0F * scale), 1));
    surface.draw_text(drawable, *surface.m_small_font, detail,
                      panel.x + 14.0F * scale, message_y + 24.0F * scale,
                      surface.m_text.muted);
    return;
  }

  const std::span<const UI::Editor::ProjectTreeItem> items =
      m_model.get_project_items();
  const std::size_t first = m_model.get_scroll_offset();
  const std::size_t row_count = viewport_row_count(layout);
  const std::size_t end = std::min(items.size(), first + row_count);
  const float tree_top = panel.y + header_height * scale;

  if (items.empty()) {
    const float content_y = tree_top + 22.0F * scale;

    surface.draw_text(
        drawable, *surface.m_small_font, "You have not yet opened a folder.",
        panel.x + 14.0F * scale, content_y, surface.m_text.primary);

    float btn_y = content_y + 14.0F * scale;
    const float btn_w = std::max(panel.width - 28.0F * scale, 0.0F);
    const float btn_h = 28.0F * scale;
    const float btn_x = panel.x + 14.0F * scale;

    // Open Folder Button
    m_empty_state_open_btn.set_bounds(UI::Rect{btn_x, btn_y, btn_w, btn_h});
    const auto &open_state = m_empty_state_open_btn.get_state();
    unsigned long open_bg =
        open_state.pressed ? surface.m_pixels.accent : surface.m_pixels.accent;
    surface.fill_rounded_rectangle(
        drawable, m_empty_state_open_btn.get_bounds(), open_bg, 4.0F * scale);

    int text_w = surface.m_small_font->getTextWidth("Open Folder");
    surface.draw_text(drawable, *surface.m_small_font, "Open Folder",
                      btn_x + btn_w * 0.5F - static_cast<float>(text_w) * 0.5F,
                      btn_y + btn_h * 0.5F, surface.m_text.primary);

    btn_y += btn_h + 20.0F * scale;

    surface.draw_text(drawable, *surface.m_small_font,
                      "You can clone a repository locally.",
                      panel.x + 14.0F * scale, btn_y, surface.m_text.primary);

    btn_y += 14.0F * scale;

    // Clone Repository Button
    m_empty_state_clone_btn.set_bounds(UI::Rect{btn_x, btn_y, btn_w, btn_h});
    const auto &clone_state = m_empty_state_clone_btn.get_state();
    unsigned long clone_bg =
        clone_state.pressed ? surface.m_pixels.accent : surface.m_pixels.accent;
    surface.fill_rounded_rectangle(
        drawable, m_empty_state_clone_btn.get_bounds(), clone_bg, 4.0F * scale);

    int clone_text_w = surface.m_small_font->getTextWidth("Clone Repository");
    surface.draw_text(drawable, *surface.m_small_font, "Clone Repository",
                      btn_x + btn_w * 0.5F -
                          static_cast<float>(clone_text_w) * 0.5F,
                      btn_y + btn_h * 0.5F, surface.m_text.primary);

    text_w = surface.m_small_font->getTextWidth("Clone Repository");
    surface.draw_text(drawable, *surface.m_small_font, "Clone Repository",
                      btn_x + btn_w * 0.5F - static_cast<float>(text_w) * 0.5F,
                      btn_y + btn_h * 0.5F, surface.m_text.primary);

    return;
  }

  for (std::size_t item_index = first; item_index < end; ++item_index) {
    const std::size_t visible_row = item_index - first;
    const UI::Editor::ProjectTreeItem &item = items[item_index];
    const UI::Rect row_bounds{
        panel.x,
        tree_top + static_cast<float>(visible_row) * row_height * scale,
        panel.width,
        row_height * scale,
    };
    if (m_hovered_row && *m_hovered_row == visible_row) {
      surface.fill_rectangle(drawable, row_bounds,
                             surface.m_pixels.tab_active_background);
    }
    const float indent_x =
        panel.x + (10.0F + static_cast<float>(item.depth) * 16.0F) * scale;
    const int guide_y = round_to_int(row_bounds.y + row_bounds.height * 0.5F);
    for (std::size_t level = 0; level < item.depth; ++level) {
      const int guide_x = round_to_int(
          panel.x + (17.0F + static_cast<float>(level) * 16.0F) * scale);

      bool line_active = false;
      for (std::size_t next = item_index + 1; next < items.size(); ++next) {
        if (items[next].depth <= level + 1) {
          line_active = (items[next].depth == level + 1);
          break;
        }
      }

      if (level == item.depth - 1) {
        surface.draw_line(
            drawable, guide_x, round_to_int(row_bounds.y), guide_x,
            line_active ? round_to_int(row_bounds.bottom()) : guide_y,
            surface.m_pixels.border);
      } else if (line_active) {
        surface.draw_line(drawable, guide_x, round_to_int(row_bounds.y),
                          guide_x, round_to_int(row_bounds.bottom()),
                          surface.m_pixels.border);
      }
    }
    if (item.depth > 0) {
      const int parent_x = round_to_int(
          panel.x +
          (17.0F + static_cast<float>(item.depth - 1) * 16.0F) * scale);
      const int child_x = round_to_int(indent_x + 3.0F * scale);
      surface.draw_line(drawable, parent_x, guide_y, child_x, guide_y,
                        surface.m_pixels.border);
    }
    if (item.directory) {
      const int arrow_x = round_to_int(indent_x + 3.0F * scale);
      const int arrow_y = round_to_int(row_bounds.y + row_bounds.height * 0.5F);
      if (arrow_x + 8.0F * scale < panel.right()) {
        const std::string chevron_path = item.expanded
                                             ? "Assets/icons/chevron-down.svg"
                                             : "Assets/icons/chevron-right.svg";
        surface.draw_svg_icon(
            drawable, chevron_path, arrow_x, arrow_y,
            std::max(round_to_int(8.0F * scale), 7),
            surface.m_palette.text_muted,
            m_hovered_row && *m_hovered_row == visible_row
                ? surface.m_palette.tab_active_background
                : surface.m_palette.sidebar_background);
      }
      const int folder_x = round_to_int(indent_x + 19.0F * scale);
      if (folder_x + 16.0F * scale < panel.right()) {
        const std::string folder_path = item.expanded
                                            ? "Assets/icons/folder-open.svg"
                                            : "Assets/icons/folder.svg";
        const int folder_size = item.expanded 
                                    ? std::max(round_to_int(12.0F * scale), 10)
                                    : std::max(round_to_int(16.0F * scale), 13);
        surface.draw_svg_icon(
            drawable, folder_path, folder_x, arrow_y,
            folder_size,
            surface.m_palette.text_muted,
            m_hovered_row && *m_hovered_row == visible_row
                ? surface.m_palette.tab_active_background
                : surface.m_palette.sidebar_background);
      }
    } else {
      const int icon_x = round_to_int(indent_x + 19.0F * scale);
      const int icon_y = round_to_int(row_bounds.y + row_bounds.height * 0.5F);
      if (icon_x + 14.0F * scale < panel.right()) {
        const std::string icon_asset =
            UI::Editor::file_icon_asset_for_path(item.path);
        surface.draw_svg_icon(
            drawable, icon_asset, icon_x, icon_y,
            std::max(round_to_int(14.0F * scale), 11),
            surface.m_palette.text_muted,
            m_hovered_row && *m_hovered_row == visible_row
                ? surface.m_palette.tab_active_background
                : surface.m_palette.sidebar_background);
      }
    }
    const float label_x = indent_x + (item.directory ? 30.0F : 36.0F) * scale;
    if (label_x < panel.right()) {
      const float available_width = panel.right() - label_x - 10.0F * scale;
      if (available_width > 0.0F) {
        const std::string label = ellipsize(
            *surface.m_small_font, item.label,
            round_to_int(available_width));
        surface.draw_text(drawable, *surface.m_small_font, label, label_x,
                          row_bounds.y + row_bounds.height * 0.5F,
                          surface.m_text.primary);
      }
    }
  }

  // Draw Sticky Items
  const auto sticky = get_sticky_items();
  for (std::size_t i = 0; i < sticky.size(); ++i) {
    const std::size_t absolute_index = sticky[i];
    const UI::Editor::ProjectTreeItem &item = items[absolute_index];
    const UI::Rect row_bounds{
        panel.x,
        tree_top + static_cast<float>(i) * row_height * scale,
        panel.width,
        row_height * scale,
    };

    // Background to overlay standard items
    surface.fill_rectangle(drawable, row_bounds,
                           surface.m_pixels.sidebar_background);

    if (m_hovered_sticky_index &&
        *m_hovered_sticky_index == absolute_index) {
      surface.fill_rectangle(drawable, row_bounds,
                             surface.m_pixels.hover_background);
    }

    const float chevron_x =
        panel.x + 18.0F * scale + static_cast<float>(item.depth) * 12.0F * scale;
    const float content_y = row_bounds.y + row_bounds.height * 0.5F;
    const int icon_size = std::max(round_to_int(12.0F * scale), 10);

    surface.draw_svg_icon(
        drawable, "Assets/icons/chevron-down.svg", round_to_int(chevron_x),
        round_to_int(content_y), icon_size, surface.m_palette.text_muted,
        (m_hovered_sticky_index && *m_hovered_sticky_index == absolute_index)
            ? surface.m_palette.hover_background
            : surface.m_palette.sidebar_background);

    surface.draw_svg_icon(
        drawable, "Assets/icons/folder-open.svg",
        round_to_int(chevron_x + 16.0F * scale), round_to_int(content_y),
        icon_size, surface.m_palette.text_muted,
        (m_hovered_sticky_index && *m_hovered_sticky_index == absolute_index)
            ? surface.m_palette.hover_background
            : surface.m_palette.sidebar_background);

    const float text_x = chevron_x + 28.0F * scale;
    surface.draw_text(drawable, *surface.m_ui_font, item.label, text_x,
                      content_y, surface.m_text.primary);

    if (i == sticky.size() - 1) {
      surface.draw_line(drawable, round_to_int(row_bounds.x),
                        round_to_int(row_bounds.bottom()),
                        round_to_int(row_bounds.right()),
                        round_to_int(row_bounds.bottom()),
                        surface.m_pixels.border);
    }
  }

  if (items.size() > row_count) {
    const UI::Rect track = scrollbar_bounds(layout);
    const float visible_fraction =
        static_cast<float>(row_count) / static_cast<float>(items.size());
    const float thumb_height =
        std::max(track.height * visible_fraction, 18.0F * scale);
    const std::size_t maximum_offset = items.size() - row_count;
    const float progress =
        maximum_offset == 0
            ? 0.0F
            : static_cast<float>(first) / static_cast<float>(maximum_offset);
    const UI::Rect thumb{
        track.x + track.width * 0.25F,
        track.y + progress * std::max(track.height - thumb_height, 0.0F),
        std::max(track.width * 0.5F, 1.0F),
        std::min(thumb_height, track.height),
    };
    surface.fill_rectangle(drawable, thumb,
                           m_hovered_scrollbar ? surface.m_pixels.accent
                                               : surface.m_pixels.text_muted);
  }
}

std::size_t ToolSidebar::viewport_row_count(
    const UI::Editor::StudioEditorLayoutResult &layout) const noexcept {
  const float available = std::max(layout.tool_sidebar_bounds.height -
                                       header_height * layout.dpi_scale,
                                   0.0F);
  return static_cast<std::size_t>(
      std::max(std::floor(available / (row_height * layout.dpi_scale)), 0.0F));
}

std::optional<std::size_t>
ToolSidebar::row_from_point(const UI::Editor::StudioEditorLayoutResult &layout,
                            float point_y) const noexcept {
  const float tree_top =
      layout.tool_sidebar_bounds.y + header_height * layout.dpi_scale;
  if (point_y < tree_top || point_y >= layout.tool_sidebar_bounds.bottom()) {
    return std::nullopt;
  }
  const std::size_t row = static_cast<std::size_t>(
      (point_y - tree_top) / (row_height * layout.dpi_scale));
  return row < viewport_row_count(layout) &&
                 m_model.get_scroll_offset() + row <
                     m_model.get_project_items().size()
             ? std::optional<std::size_t>{row}
             : std::nullopt;
}

UI::Rect ToolSidebar::scrollbar_bounds(
    const UI::Editor::StudioEditorLayoutResult &layout) const noexcept {
  const float scale = layout.dpi_scale;
  const float tree_top = layout.tool_sidebar_bounds.y + header_height * scale;
  return UI::Rect{
      layout.tool_sidebar_bounds.right() - 8.0F * scale,
      tree_top,
      8.0F * scale,
      std::max(layout.tool_sidebar_bounds.bottom() - tree_top, 0.0F),
  };
}

std::vector<std::size_t> ToolSidebar::get_sticky_items() const {
  std::vector<std::size_t> sticky;
  if (m_model.get_active_icon() != UI::Editor::SidebarIcon::Project) {
    return sticky;
  }
  const auto items = m_model.get_project_items();
  const std::size_t first = m_model.get_scroll_offset();
  if (first > 0 && first < items.size()) {
    std::size_t current_depth = items[first].depth;
    std::size_t search = first - 1;
    while (current_depth > 0) {
      if (items[search].depth < current_depth) {
        sticky.push_back(search);
        current_depth = items[search].depth;
      }
      if (search == 0)
        break;
      --search;
    }
    std::reverse(sticky.begin(), sticky.end());
  }
  return sticky;
}

} // namespace Zenvra::Platform::X11::Components
