#include "Platform/Win32/Components/ToolSidebar.h"
#include "Platform/Win32/Components/StudioWorkspaceRenderer.h"
#include "UI/Editor/FileIconModel.h"
#include "Utility/Fonts.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <string_view>

namespace Zenvra::Platform::Win32::Components {

namespace {

int round_to_int(float value) { return static_cast<int>(std::lround(value)); }

std::string ellipsize(HDC device_context, AntialiasedFont &font,
                      std::string text, int maximum_width) {
  if (font.getTextWidth(device_context, text) <= maximum_width) {
    return text;
  }
  constexpr std::string_view suffix = "...";
  while (!text.empty() &&
         font.getTextWidth(device_context, text + std::string{suffix}) >
             maximum_width) {
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

SidebarPressResult ToolSidebar::handle_pointer_press(
    const UI::Editor::StudioEditorLayoutResult &layout, float point_x,
    float point_y) {
  if (is_resize_handle_point(layout, point_x, point_y)) {
    m_resizing = true;
    m_drag_start_x = point_x;
    m_drag_start_width = m_width;
    return SidebarPressResult{.handled = true};
  }
  if (!contains(layout, point_x, point_y)) {
    return SidebarPressResult{.handled = false};
  }
  if (m_model.get_active_icon() == UI::Editor::SidebarIcon::Project &&
      m_model.get_project_items().size() > viewport_row_count(layout) &&
      scrollbar_bounds(layout).contains(point_x, point_y)) {
    return SidebarPressResult{.handled = true};
  }
  if (m_model.get_active_icon() == UI::Editor::SidebarIcon::Project) {
    HeaderAction header_act = HeaderAction::None;
    if (m_explorer_header.handle_pointer_press(layout, point_x, point_y, m_model, header_act)) {
      if (header_act == HeaderAction::NewFile) {
        return SidebarPressResult{.handled = true, .action = SidebarActionKind::NewFile, .path = m_model.get_target_directory_for_creation()};
      }
      if (header_act == HeaderAction::NewFolder) {
        return SidebarPressResult{.handled = true, .action = SidebarActionKind::NewFolder, .path = m_model.get_target_directory_for_creation()};
      }
      if (header_act == HeaderAction::Refresh) {
        return SidebarPressResult{.handled = true, .action = SidebarActionKind::Refresh};
      }
      if (header_act == HeaderAction::CollapseAll) {
        return SidebarPressResult{.handled = true, .action = SidebarActionKind::CollapseAll};
      }
      return SidebarPressResult{.handled = true};
    }
  }
  const float scale = layout.dpi_scale;
  const float tree_top = layout.tool_sidebar_bounds.y + header_height * scale;
  const auto sticky = get_sticky_items();
  const float sticky_height = static_cast<float>(sticky.size()) * row_height * scale;
  
  if (m_model.get_active_icon() == UI::Editor::SidebarIcon::Project &&
      point_y >= tree_top && point_y < tree_top + sticky_height) {
      std::size_t sticky_index = static_cast<std::size_t>((point_y - tree_top) / (row_height * scale));
      if (sticky_index < sticky.size()) {
          m_model.set_scroll_offset(sticky[sticky_index]);
          return SidebarPressResult{.handled = true};
      }
  }

  const std::optional<std::size_t> row = row_from_point(layout, point_y);
  if (row && m_model.get_active_icon() == UI::Editor::SidebarIcon::Project) {
    const UI::Editor::ActivityPanelAction action =
        m_model.activate_project_row(*row);
    if (action.file_to_open) {
      return SidebarPressResult{.handled = true, .action = SidebarActionKind::OpenFile, .path = action.file_to_open};
    }
    return SidebarPressResult{.handled = action.handled};
  }
  return SidebarPressResult{.handled = true};
}

std::optional<std::filesystem::path> ToolSidebar::handle_right_click(
    const UI::Editor::StudioEditorLayoutResult &layout, float point_x,
    float point_y) {
  if (!contains(layout, point_x, point_y) || m_model.get_active_icon() != UI::Editor::SidebarIcon::Project) {
    return std::nullopt;
  }
  const std::optional<std::size_t> row = row_from_point(layout, point_y);
  if (row) {
    const std::size_t item_index = m_model.get_scroll_offset() + *row;
    const auto items = m_model.get_project_items();
    if (item_index < items.size()) {
      m_model.set_selected_path(items[item_index].path);
      return items[item_index].path;
    }
  }
  m_model.set_selected_path(m_model.get_workspace_root());
  return m_model.get_workspace_root();
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
    
    if (contains(layout, point_x, point_y) &&
        m_model.get_active_icon() == UI::Editor::SidebarIcon::Project) {
        
        const float scale = layout.dpi_scale;
        const float tree_top = layout.tool_sidebar_bounds.y + header_height * scale;
        const auto sticky = get_sticky_items();
        const float sticky_height = static_cast<float>(sticky.size()) * row_height * scale;

        if (point_y >= tree_top && point_y < tree_top + sticky_height) {
            std::size_t sticky_index = static_cast<std::size_t>((point_y - tree_top) / (row_height * scale));
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
    
    bool next_resize_hovered = is_resize_handle_point(layout, point_x, point_y);
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
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_x,
    float point_y) const noexcept {
    const float scale = layout.dpi_scale;
    const UI::Rect handle_bounds{
        layout.tool_sidebar_bounds.right() - 3.0F * scale,
        layout.tool_sidebar_bounds.y,
        6.0F * scale,
        layout.tool_sidebar_bounds.height
    };
    return handle_bounds.contains(point_x, point_y);
}

bool ToolSidebar::is_resizing() const noexcept { return m_resizing; }

float ToolSidebar::get_width() const noexcept { return m_width; }

bool ToolSidebar::handle_pointer_drag(
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_x) noexcept {
    if (!m_resizing) return false;
    
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
    const StudioWorkspaceRenderer &surface, HDC device_context,
    const UI::Editor::StudioEditorLayoutResult &layout) const {
  const UI::Rect panel = layout.tool_sidebar_bounds;
  const float scale = layout.dpi_scale;

  if (!is_visible() || panel.is_empty()) {
    if (m_resize_hovered || m_resizing) {
      surface.draw_line(device_context, round_to_int(panel.right() - scale),
                        round_to_int(panel.y), round_to_int(panel.right() - scale),
                        round_to_int(panel.bottom()), surface.m_palette.accent);
      surface.fill_rectangle(device_context,
          UI::Rect{
              panel.right() - scale - scale,
              panel.y,
              std::max(2.0F * scale, 2.0F),
              panel.height},
          surface.m_palette.accent);
    }
    return;
  }

  surface.fill_rectangle(device_context, panel,
                         surface.m_palette.sidebar_background);
  
  if (m_model.get_active_icon() == UI::Editor::SidebarIcon::Project) {
    m_explorer_header.render(surface, device_context, layout, std::string{m_model.get_title()});
  } else {
    surface.draw_text(device_context, *surface.m_ui_font, m_model.get_title(),
                      panel.x + 14.0F * scale,
                      panel.y + header_height * 0.5F * scale,
                      surface.m_palette.text_primary);

    const int more_center_x = round_to_int(panel.right() - 17.0F * scale);
    const int header_center_y =
        round_to_int(panel.y + header_height * 0.5F * scale);
    surface.draw_svg_icon(
        device_context, "ellipsis.svg", more_center_x, header_center_y,
        std::max(round_to_int(15.0F * scale), 11), surface.m_palette.text_muted,
        surface.m_palette.sidebar_background);
    surface.draw_line(device_context, round_to_int(panel.x),
                      round_to_int(panel.y + header_height * scale),
                      round_to_int(panel.right()),
                      round_to_int(panel.y + header_height * scale),
                      surface.m_palette.border);
  }

  if (m_model.get_active_icon() != UI::Editor::SidebarIcon::Project) {
    const float content_y = panel.y + (header_height + 22.0F) * scale;
    if (m_model.get_active_icon() == UI::Editor::SidebarIcon::Search) {
      const UI::Rect search_bounds{
          panel.x + 12.0F * scale,
          content_y,
          std::max(panel.width - 24.0F * scale, 0.0F),
          28.0F * scale,
      };
      surface.fill_rectangle(device_context, search_bounds,
                             surface.m_palette.editor_background);
      surface.draw_rectangle(device_context, search_bounds,
                             surface.m_palette.border);
      surface.draw_svg_icon(
          device_context, "search.svg",
          round_to_int(search_bounds.x + 13.0F * scale),
          round_to_int(search_bounds.y + search_bounds.height * 0.5F),
          std::max(round_to_int(13.0F * scale), 10),
          surface.m_palette.text_muted, surface.m_palette.editor_background);
      surface.draw_text(device_context, *surface.m_small_font,
                        "Search files...", search_bounds.x + 25.0F * scale,
                        search_bounds.y + search_bounds.height * 0.5F,
                        surface.m_palette.text_muted);
    }
    const float message_y =
        content_y +
        (m_model.get_active_icon() == UI::Editor::SidebarIcon::Search ? 50.0F
                                                                      : 0.0F) *
            scale;
    surface.draw_text(device_context, *surface.m_ui_font,
                      m_model.get_content_heading(), panel.x + 14.0F * scale,
                      message_y, surface.m_palette.text_primary);
    const std::string detail = ellipsize(
        device_context, *surface.m_small_font, std::string{m_model.get_content_detail()},
        std::max(round_to_int(panel.width - 28.0F * scale), 1));
    surface.draw_text(device_context, *surface.m_small_font, detail,
                      panel.x + 14.0F * scale, message_y + 24.0F * scale,
                      surface.m_palette.text_muted);
    return;
  }

  const std::span<const UI::Editor::ProjectTreeItem> items = m_model.get_project_items();
  const std::size_t first = m_model.get_scroll_offset();
  const std::size_t row_count = viewport_row_count(layout);
  const std::size_t end = std::min(items.size(), first + row_count);
  const float tree_top = panel.y + header_height * scale;
  for (std::size_t item_index = first; item_index < end; ++item_index) {
    const std::size_t visible_row = item_index - first;
    const UI::Editor::ProjectTreeItem &item = items[item_index];
    const UI::Rect row_bounds{
        panel.x,
        tree_top + static_cast<float>(visible_row) * row_height * scale,
        panel.width,
        row_height * scale,
    };
    const bool is_selected = m_model.is_selected(item.path);
    const bool is_hovered = (m_hovered_row && *m_hovered_row == visible_row);
    if (is_selected) {
      surface.fill_rectangle(device_context, row_bounds,
                             UI::Theme::Color{4, 57, 94, 255}); // VS Code Explorer Selection Blue
    } else if (is_hovered) {
      surface.fill_rectangle(device_context, row_bounds,
                             surface.m_palette.tab_active_background);
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
            device_context, guide_x, round_to_int(row_bounds.y), guide_x,
            line_active ? round_to_int(row_bounds.bottom()) : guide_y,
            surface.m_palette.border);
      } else if (line_active) {
        surface.draw_line(device_context, guide_x, round_to_int(row_bounds.y),
                          guide_x, round_to_int(row_bounds.bottom()),
                          surface.m_palette.border);
      }
    }
    if (item.depth > 0) {
      const int parent_x = round_to_int(
          panel.x +
          (17.0F + static_cast<float>(item.depth - 1) * 16.0F) * scale);
      const int child_x = round_to_int(indent_x + 3.0F * scale);
      surface.draw_line(device_context, parent_x, guide_y, child_x, guide_y,
                        surface.m_palette.border);
    }
    const UI::Theme::Color &row_background =
        m_hovered_row && *m_hovered_row == visible_row
            ? surface.m_palette.tab_active_background
            : surface.m_palette.sidebar_background;
    if (item.directory) {
      const int arrow_x = round_to_int(indent_x + 3.0F * scale);
      const int arrow_y = round_to_int(row_bounds.y + row_bounds.height * 0.5F);
      if (arrow_x + 8.0F * scale < panel.right()) {
        surface.draw_svg_icon(
            device_context,
            item.expanded ? "chevron-down.svg" : "chevron-right.svg", arrow_x,
            arrow_y, std::max(round_to_int(8.0F * scale), 7),
            surface.m_palette.text_muted, row_background);
      }
      const int folder_x = round_to_int(indent_x + 19.0F * scale);
      if (folder_x + 16.0F * scale < panel.right()) {
        const int folder_size = item.expanded 
                                    ? std::max(round_to_int(12.0F * scale), 10)
                                    : std::max(round_to_int(16.0F * scale), 13);
        surface.draw_svg_icon(device_context,
                              item.expanded ? "folder-open.svg" : "folder.svg",
                              folder_x, arrow_y,
                              folder_size,
                              surface.m_palette.text_muted, row_background);
      }
    } else {
      const int icon_x = round_to_int(indent_x + 19.0F * scale);
      const int icon_y = round_to_int(row_bounds.y + row_bounds.height * 0.5F);
      if (icon_x + 14.0F * scale < panel.right()) {
        const std::string icon_asset =
            UI::Editor::file_icon_asset_for_path(item.path);
        surface.draw_svg_icon(device_context, icon_asset, icon_x, icon_y,
                              std::max(round_to_int(14.0F * scale), 11),
                              surface.m_palette.text_muted, row_background);
      }
    }
    const float label_x = indent_x + (item.directory ? 30.0F : 36.0F) * scale;
    if (label_x < panel.right()) {
      const float available_width = panel.right() - label_x - 10.0F * scale;
      if (available_width > 0.0F) {
        const std::string label = ellipsize(
            device_context, *surface.m_small_font, item.label,
            round_to_int(available_width));
        surface.draw_text(device_context, *surface.m_small_font, label, label_x,
                          row_bounds.y + row_bounds.height * 0.5F,
                          surface.m_palette.text_primary);
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
          std::max(panel.width - 1.0F, 0.0F),
          row_height * scale,
      };

      // Background to overlay standard items
      surface.fill_rectangle(device_context, row_bounds, surface.m_palette.sidebar_background);

      if (m_hovered_sticky_index && *m_hovered_sticky_index == absolute_index) {
          surface.fill_rectangle(device_context, row_bounds, surface.m_palette.hover_background);
      }

      const float chevron_x = panel.x + 18.0F * scale + static_cast<float>(item.depth) * 12.0F * scale;
      const float content_y = row_bounds.y + row_bounds.height * 0.5F;
      const int icon_size = std::max(round_to_int(12.0F * scale), 10);

      surface.draw_svg_icon(
          device_context, "chevron-down.svg",
          round_to_int(chevron_x), round_to_int(content_y), icon_size,
          surface.m_palette.text_muted,
          (m_hovered_sticky_index && *m_hovered_sticky_index == absolute_index)
              ? surface.m_palette.hover_background
              : surface.m_palette.sidebar_background);

      surface.draw_svg_icon(
          device_context, "folder-open.svg",
          round_to_int(chevron_x + 16.0F * scale), round_to_int(content_y),
          icon_size, surface.m_palette.text_muted,
          (m_hovered_sticky_index && *m_hovered_sticky_index == absolute_index)
              ? surface.m_palette.hover_background
              : surface.m_palette.sidebar_background);

      const float text_x = chevron_x + 28.0F * scale;
      surface.draw_text(device_context, *surface.m_ui_font, item.label, text_x,
                        content_y, surface.m_palette.text_primary);

      if (i == sticky.size() - 1) {
          surface.draw_line(
              device_context,
              round_to_int(row_bounds.x),
              round_to_int(row_bounds.bottom()) - 1,
              round_to_int(panel.right() - 1.0F),
              round_to_int(row_bounds.bottom()) - 1,
              surface.m_palette.border);
      }
  }

  // Draw Scrollbar
  if (m_model.get_project_items().size() > row_count) {
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
    surface.fill_rectangle(device_context, thumb,
                           m_hovered_scrollbar ? surface.m_palette.accent
                                               : surface.m_palette.text_muted);
  }

  // Draw right vertical border on top with correct z-index
  const UI::Theme::Color border_color = (m_resize_hovered || m_resizing)
                                        ? surface.m_palette.accent
                                        : surface.m_palette.border;
  surface.draw_line(device_context, round_to_int(panel.right() - 1.0F),
                    round_to_int(panel.y), round_to_int(panel.right() - 1.0F),
                    round_to_int(panel.bottom()), border_color);
  if (m_resize_hovered || m_resizing) {
    surface.fill_rectangle(device_context,
        UI::Rect{
            panel.right() - std::max(2.0F * scale, 2.0F),
            panel.y,
            std::max(2.0F * scale, 2.0F),
            panel.height},
        surface.m_palette.accent);
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
  if (m_model.get_active_icon() != UI::Editor::SidebarIcon::Project) return sticky;
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
          if (search == 0) break;
          --search;
      }
      std::reverse(sticky.begin(), sticky.end());
  }
  return sticky;
}

} // namespace Zenvra::Platform::Win32::Components
