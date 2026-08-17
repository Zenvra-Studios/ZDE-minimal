#include "Platform/X11/Components/ToolSidebar.h"

#include "Platform/X11/Components/StudioWorkspaceRenderer.h"
#include "UI/Editor/FileIconModel.h"
#include "Utility/Fonts.h"
#include "Utility/MathUtil.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <string_view>

namespace Zenvra::Platform::X11::Components {

namespace {

using Zenvra::Utility::round_to_int;

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

void ToolSidebar::clear_workspace() noexcept {
  m_hovered_row.reset();
  m_hovered_scrollbar = false;
  m_model.clear_workspace();
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
      return SidebarPressResult{.handled = true, .action = SidebarActionKind::OpenFile, .path = "::OPEN_FOLDER::"};
    }
    if (m_empty_state_clone_btn.handle_pointer_press(point_x, point_y)) {
      return SidebarPressResult{.handled = true};
    }
  }
  if (m_model.get_active_icon() == UI::Editor::SidebarIcon::Project) {
    HeaderAction header_act = HeaderAction::NoneAction;
    if (m_explorer_header.handle_pointer_press(layout, point_x, point_y,
                                               m_model, header_act)) {
      if (header_act == HeaderAction::NewFile) {
        return SidebarPressResult{.handled = true,
                                  .action = SidebarActionKind::NewFile,
                                  .path = m_model.get_target_directory_for_creation()};
      }
      if (header_act == HeaderAction::NewFolder) {
        return SidebarPressResult{.handled = true,
                                  .action = SidebarActionKind::NewFolder,
                                  .path = m_model.get_target_directory_for_creation()};
      }
      if (header_act == HeaderAction::Refresh) {
        return SidebarPressResult{.handled = true, .action = SidebarActionKind::Refresh, .path = std::nullopt};
      }
      if (header_act == HeaderAction::CollapseAll) {
        return SidebarPressResult{.handled = true, .action = SidebarActionKind::CollapseAll, .path = std::nullopt};
      }
      return SidebarPressResult{.handled = true, .action = SidebarActionKind::NoneAction, .path = std::nullopt};
    }
  }
  const float scale = layout.dpi_scale;
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
      return SidebarPressResult{.handled = true};
    }
  }

  const std::optional<std::size_t> row = row_from_point(layout, point_y);
  if (row && m_model.get_active_icon() == UI::Editor::SidebarIcon::Project) {
    m_drag_source_row = *row;
    m_drag_press_x = point_x;
    m_drag_press_y = point_y;
    m_drag_current_x = point_x;
    m_drag_current_y = point_y;
    m_is_dragging_item = false;
    m_drag_target_row.reset();

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
  if (!contains(layout, point_x, point_y) ||
      m_model.get_active_icon() != UI::Editor::SidebarIcon::Project) {
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
    const float tree_top =
        layout.tool_sidebar_bounds.y + header_height * scale;
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

  bool next_resize_hovered = is_resize_handle_point(layout, point_x, point_y);
  bool header_changed = false;
  if (m_model.get_active_icon() == UI::Editor::SidebarIcon::Project) {
    header_changed =
        m_explorer_header.handle_pointer_move(layout, point_x, point_y);
  }

  bool empty_btn_changed = false;
  if (m_model.get_active_icon() == UI::Editor::SidebarIcon::Project &&
      m_model.get_project_items().empty()) {
    empty_btn_changed =
        m_empty_state_open_btn.handle_pointer_move(point_x, point_y) ||
        m_empty_state_clone_btn.handle_pointer_move(point_x, point_y);
  }

  const bool changed = next_row != m_hovered_row ||
                       next_sticky_hover != m_hovered_sticky_index ||
                       next_icon != m_hovered_icon ||
                       next_scrollbar != m_hovered_scrollbar ||
                       next_resize_hovered != m_resize_hovered ||
                       header_changed || empty_btn_changed;
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
      layout.tool_sidebar_bounds.y,
      6.0F * scale,
      layout.tool_sidebar_bounds.height,
  };
  return handle_bounds.contains(point_x, point_y);
}

bool ToolSidebar::is_resizing() const noexcept { return m_resizing; }

float ToolSidebar::get_width() const noexcept { return m_width; }

bool ToolSidebar::handle_pointer_drag(
    const UI::Editor::StudioEditorLayoutResult &layout, float point_x,
    float point_y) noexcept {
  if (m_resizing) {
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

  if (m_drag_source_row.has_value() &&
      m_model.get_active_icon() == UI::Editor::SidebarIcon::Project) {
    const float dist =
        std::hypot(point_x - m_drag_press_x, point_y - m_drag_press_y);
    if (dist > 18.0F) {
      m_is_dragging_item = true;
      m_drag_current_x = point_x;
      m_drag_current_y = point_y;
      m_drag_target_row = row_from_point(layout, point_y);
      return true;
    }
  }

  return false;
}

bool ToolSidebar::handle_pointer_release() noexcept {
  const bool was_resizing = m_resizing;
  const bool was_dragging = m_is_dragging_item;
  m_resizing = false;

  if (m_is_dragging_item && m_drag_source_row.has_value()) {
    const auto items = m_model.get_project_items();
    const std::size_t source_idx =
        m_model.get_scroll_offset() + *m_drag_source_row;
    if (source_idx < items.size()) {
      const auto source_path = items[source_idx].path;
      std::filesystem::path target_dir;
      if (m_drag_target_row.has_value()) {
        const std::size_t target_idx =
            m_model.get_scroll_offset() + *m_drag_target_row;
        if (target_idx < items.size()) {
          const auto &target_item = items[target_idx];
          if (target_item.directory) {
            target_dir = target_item.path;
          } else {
            target_dir = target_item.path.parent_path();
          }
        }
      } else {
        target_dir = m_model.get_workspace_root();
      }

      if (!target_dir.empty() && target_dir != source_path) {
        std::filesystem::path out_p;
        static_cast<void>(m_model.move_item(source_path, target_dir, out_p));
      }
    }
  }
  m_drag_source_row.reset();
  m_drag_target_row.reset();
  m_is_dragging_item = false;
  return was_resizing || was_dragging;
}

bool ToolSidebar::tick_animations() noexcept {
  if (!is_visible() || m_model.get_workspace_root().empty()) {
    return false;
  }
  const auto now = std::chrono::steady_clock::now();
  if (std::chrono::duration_cast<std::chrono::milliseconds>(now -
                                                            m_last_refresh_time)
          .count() >= 1000) {
    m_last_refresh_time = now;
    return m_model.refresh();
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
                        round_to_int(panel.y),
                        round_to_int(panel.right() - scale),
                        round_to_int(panel.bottom()), surface.m_pixels.accent);
      surface.fill_rectangle(
          drawable,
          UI::Rect{panel.right() - scale - scale, panel.y,
                   std::max(2.0F * scale, 2.0F), panel.height},
          surface.m_pixels.accent);
    }
    return;
  }

  surface.fill_rectangle(drawable, panel, surface.m_pixels.sidebar_background);

  if (m_model.get_active_icon() == UI::Editor::SidebarIcon::Project) {
    m_explorer_header.render(surface, drawable, layout,
                             std::string{m_model.get_title()});
  } else {
    surface.draw_text(drawable, *surface.m_ui_font, m_model.get_title(),
                      panel.x + 14.0F * scale,
                      panel.y + header_height * 0.5F * scale,
                      surface.m_text.primary);

    const int more_center_x = round_to_int(panel.right() - 17.0F * scale);
    const int header_center_y =
        round_to_int(panel.y + header_height * 0.5F * scale);
    surface.draw_svg_icon(
        drawable, "Assets/icons/ellipsis.svg", more_center_x, header_center_y,
        std::max(round_to_int(15.0F * scale), 11), surface.m_palette.text_muted,
        surface.m_palette.sidebar_background);
    surface.draw_line(drawable, round_to_int(panel.x),
                      round_to_int(panel.y + header_height * scale),
                      round_to_int(panel.right()),
                      round_to_int(panel.y + header_height * scale),
                      surface.m_pixels.border);
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
      surface.fill_rectangle(drawable, search_bounds,
                             surface.m_pixels.editor_background);
      surface.draw_rectangle(drawable, search_bounds, surface.m_pixels.border);
      surface.draw_svg_icon(
          drawable, "Assets/icons/search.svg",
          round_to_int(search_bounds.x + 13.0F * scale),
          round_to_int(search_bounds.y + search_bounds.height * 0.5F),
          std::max(round_to_int(13.0F * scale), 10),
          surface.m_palette.text_muted, surface.m_palette.editor_background);
      surface.draw_text(drawable, *surface.m_small_font, "Search files...",
                        search_bounds.x + 25.0F * scale,
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
    const std::string detail =
        ellipsize(*surface.m_small_font,
                  std::string{m_model.get_content_detail()},
                  std::max(round_to_int(panel.width - 28.0F * scale), 1));
    surface.draw_text(drawable, *surface.m_small_font, detail,
                      panel.x + 14.0F * scale, message_y + 24.0F * scale,
                      surface.m_text.muted);
    return;
  }

  const std::span<const UI::Editor::ProjectTreeItem> items =
      m_model.get_project_items();
  if (items.empty()) {
    const float msg_y = panel.y + (header_height + 22.0F) * scale;
    surface.draw_text(drawable, *surface.m_ui_font, "No Folder Opened",
                      panel.x + 14.0F * scale, msg_y, surface.m_text.primary);
    surface.draw_text(drawable, *surface.m_small_font,
                      "You have not yet opened a folder.",
                      panel.x + 14.0F * scale, msg_y + 20.0F * scale,
                      surface.m_text.muted);

    float btn_y = msg_y + 36.0F * scale;
    const float btn_w = std::max(panel.width - 28.0F * scale, 0.0F);
    const float btn_h = 28.0F * scale;
    const float btn_x = panel.x + 14.0F * scale;

    m_empty_state_open_btn.set_bounds(UI::Rect{btn_x, btn_y, btn_w, btn_h});
    surface.fill_rounded_rectangle(
        drawable, m_empty_state_open_btn.get_bounds(),
        m_empty_state_open_btn.get_state().hovered
            ? surface.allocate_color(UI::Theme::Color{17, 119, 187, 255})
            : surface.allocate_color(UI::Theme::Color{14, 99, 156, 255}),
        4.0F * scale, surface.m_pixels.sidebar_background);
    surface.draw_text(
        drawable, *surface.m_small_font, "Open Folder",
        btn_x + btn_w * 0.5F - 36.0F * scale, btn_y + btn_h * 0.5F,
        UI::Theme::Color{255, 255, 255, 255});

    btn_y += btn_h + 20.0F * scale;
    surface.draw_text(drawable, *surface.m_small_font,
                      "Clone from a remote repository.",
                      panel.x + 14.0F * scale, btn_y, surface.m_text.muted);

    btn_y += 14.0F * scale;
    m_empty_state_clone_btn.set_bounds(UI::Rect{btn_x, btn_y, btn_w, btn_h});
    surface.fill_rounded_rectangle(
        drawable, m_empty_state_clone_btn.get_bounds(),
        m_empty_state_clone_btn.get_state().hovered
            ? surface.allocate_color(UI::Theme::Color{58, 62, 72, 255})
            : surface.allocate_color(UI::Theme::Color{44, 48, 56, 255}),
        4.0F * scale, surface.m_pixels.sidebar_background);
    surface.draw_text(
        drawable, *surface.m_small_font, "Clone Repository",
        btn_x + btn_w * 0.5F - 46.0F * scale, btn_y + btn_h * 0.5F,
        UI::Theme::Color{215, 220, 228, 255});
    return;
  }

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
    const bool is_hovered = m_hovered_row && *m_hovered_row == visible_row;
    const bool is_drag_target =
        m_is_dragging_item && m_drag_target_row &&
        *m_drag_target_row == visible_row;

    const UI::Rect highlight_rect{
        panel.x + 4.0F * scale,
        row_bounds.y + 1.0F * scale,
        panel.width - 8.0F * scale,
        row_height * scale - 2.0F * scale,
    };

    if (is_drag_target) {
      surface.fill_rounded_rectangle(
          drawable, highlight_rect, surface.m_pixels.tab_active_background,
          4.0F * scale, surface.m_pixels.sidebar_background);
      surface.draw_rectangle(drawable, highlight_rect, surface.m_pixels.accent);
    } else if (is_selected) {
      surface.fill_rounded_rectangle(
          drawable, highlight_rect, surface.m_pixels.tab_active_background,
          4.0F * scale, surface.m_pixels.sidebar_background);
      const UI::Rect left_bar{
          highlight_rect.x, highlight_rect.y + 2.0F * scale,
          2.5F * scale, highlight_rect.height - 4.0F * scale,
      };
      surface.fill_rounded_rectangle(
          drawable, left_bar, surface.m_pixels.text_primary,
          1.25F * scale, surface.m_pixels.tab_active_background);
    } else if (is_hovered) {
      surface.fill_rounded_rectangle(
          drawable, highlight_rect, surface.m_pixels.hover_background,
          4.0F * scale, surface.m_pixels.sidebar_background);
    }

    const float indent_x =
        panel.x + (4.0F + static_cast<float>(item.depth) * 13.0F) * scale;
    const int guide_y = round_to_int(row_bounds.y + row_bounds.height * 0.5F);
    for (std::size_t level = 0; level < item.depth; ++level) {
      const int guide_x = round_to_int(
          panel.x + (10.0F + static_cast<float>(level) * 13.0F) * scale);

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
          (10.0F + static_cast<float>(item.depth - 1) * 13.0F) * scale);
      const int child_x = round_to_int(indent_x + 4.0F * scale);
      surface.draw_line(drawable, parent_x, guide_y, child_x, guide_y,
                        surface.m_pixels.border);
    }

    const UI::Theme::Color &row_background =
        is_hovered ? surface.m_palette.tab_active_background
                   : surface.m_palette.sidebar_background;

    if (item.directory) {
      const int arrow_x = round_to_int(indent_x + 4.0F * scale);
      const int arrow_y = round_to_int(row_bounds.y + row_bounds.height * 0.5F);
      if (arrow_x + 8.0F * scale < panel.right()) {
        surface.draw_svg_icon(
            drawable,
            item.expanded ? "Assets/icons/chevron-down.svg" : "Assets/icons/chevron-right.svg",
            arrow_x, arrow_y, std::max(round_to_int(8.0F * scale), 7),
            is_selected ? UI::Theme::Color{255, 255, 255, 255} : surface.m_palette.text_muted,
            row_background);
      }
      const int folder_x = round_to_int(indent_x + 16.0F * scale);
      if (folder_x + 16.0F * scale < panel.right()) {
        const int folder_size = item.expanded
                                    ? std::max(round_to_int(12.0F * scale), 10)
                                    : std::max(round_to_int(16.0F * scale), 13);
        surface.draw_svg_icon(
            drawable,
            item.expanded ? "Assets/icons/folder-open.svg" : "Assets/icons/folder.svg",
            folder_x, arrow_y, folder_size,
            is_selected ? UI::Theme::Color{255, 255, 255, 255} : surface.m_palette.text_muted,
            row_background);
      }
    } else {
      const int icon_x = round_to_int(indent_x + 14.0F * scale);
      const int icon_y = round_to_int(row_bounds.y + row_bounds.height * 0.5F);
      if (icon_x + 14.0F * scale < panel.right()) {
        const std::string icon_asset =
            UI::Editor::file_icon_asset_for_path(item.path);
        surface.draw_svg_icon(
            drawable, "Assets/icons/" + icon_asset, icon_x, icon_y,
            std::max(round_to_int(14.0F * scale), 11),
            surface.m_palette.text_muted, row_background, true);
      }
    }

    const float label_x = indent_x + (item.directory ? 26.0F : 25.0F) * scale;
    if (label_x < panel.right()) {
      const float available_width = panel.right() - label_x - 10.0F * scale;
      if (available_width > 0.0F) {
        const std::string label = ellipsize(
            *surface.m_small_font, item.label, round_to_int(available_width));
        surface.draw_text(
            drawable, *surface.m_small_font, label, label_x,
            row_bounds.y + row_bounds.height * 0.5F,
            is_selected ? UI::Theme::Color{255, 255, 255, 255} : surface.m_palette.text_primary);
      }
    }
  }

  // Draw Sticky Items
  const auto sticky = get_sticky_items();
  for (std::size_t i = 0; i < sticky.size(); ++i) {
    const std::size_t absolute_index = sticky[i];
    if (absolute_index >= items.size()) {
      continue;
    }
    const UI::Editor::ProjectTreeItem &item = items[absolute_index];
    const UI::Rect row_bounds{
        panel.x,
        tree_top + static_cast<float>(i) * row_height * scale,
        std::max(panel.width - 1.0F, 0.0F),
        row_height * scale,
    };

    // Background to overlay standard items
    surface.fill_rectangle(drawable, row_bounds,
                           surface.m_pixels.sidebar_background);

    if (m_hovered_sticky_index && *m_hovered_sticky_index == absolute_index) {
      surface.fill_rectangle(drawable, row_bounds,
                             surface.m_pixels.hover_background);
    }

    const float chevron_x =
        panel.x + 10.0F * scale + static_cast<float>(item.depth) * 12.0F * scale;
    const float content_y = row_bounds.y + row_bounds.height * 0.5F;
    const int icon_size = std::max(round_to_int(12.0F * scale), 10);

    surface.draw_svg_icon(
        drawable, "Assets/icons/chevron-down.svg",
        round_to_int(chevron_x), round_to_int(content_y), icon_size,
        surface.m_palette.text_muted,
        (m_hovered_sticky_index && *m_hovered_sticky_index == absolute_index)
            ? surface.m_palette.hover_background
            : surface.m_palette.sidebar_background);

    surface.draw_svg_icon(
        drawable, "Assets/icons/folder-open.svg",
        round_to_int(chevron_x + 14.0F * scale), round_to_int(content_y),
        icon_size, surface.m_palette.text_muted,
        (m_hovered_sticky_index && *m_hovered_sticky_index == absolute_index)
            ? surface.m_palette.hover_background
            : surface.m_palette.sidebar_background);

    const float text_x = chevron_x + 25.0F * scale;
    surface.draw_text(drawable, *surface.m_ui_font, item.label, text_x,
                      content_y, surface.m_palette.text_primary);

    if (i == sticky.size() - 1) {
      surface.draw_line(
          drawable,
          round_to_int(row_bounds.x),
          round_to_int(row_bounds.bottom()) - 1,
          round_to_int(panel.right() - 1.0F),
          round_to_int(row_bounds.bottom()) - 1,
          surface.m_pixels.border);
    }
  }

  // Draw scrollbar if needed
  if (items.size() > row_count) {
    const UI::Rect sb = scrollbar_bounds(layout);
    surface.fill_rectangle(drawable, sb, surface.m_pixels.sidebar_background);
    const float content_h = static_cast<float>(items.size()) * row_height * scale;
    const float view_h = sb.height;
    const float thumb_h = std::max(view_h * (view_h / content_h), 20.0F * scale);
    const float max_scroll = static_cast<float>(items.size() - row_count);
    const float scroll_pct =
        max_scroll > 0.0F ? static_cast<float>(first) / max_scroll : 0.0F;
    const float thumb_y = sb.y + scroll_pct * (view_h - thumb_h);
    const UI::Rect thumb{sb.x + 2.0F * scale, thumb_y,
                         sb.width - 4.0F * scale, thumb_h};
    surface.fill_rounded_rectangle(
        drawable, thumb,
        m_hovered_scrollbar ? surface.m_pixels.accent
                            : surface.m_pixels.border,
        2.0F * scale, surface.m_pixels.sidebar_background);
  }

  // Draw right border
  surface.draw_line(
      drawable, round_to_int(panel.right() - 1.0F), round_to_int(panel.y),
      round_to_int(panel.right() - 1.0F), round_to_int(panel.bottom()),
      surface.m_pixels.border);

  // Draw Dragged item ghost under cursor
  if (m_is_dragging_item && m_drag_source_row.has_value()) {
    const std::size_t src_idx = first + *m_drag_source_row;
    if (src_idx < items.size()) {
      const auto &drag_item = items[src_idx];
      const float ghost_w = 160.0F * scale;
      const float ghost_h = 24.0F * scale;
      const UI::Rect ghost_rect{
          m_drag_current_x - 12.0F * scale,
          m_drag_current_y - 12.0F * scale,
          ghost_w,
          ghost_h,
      };
      surface.fill_rounded_rectangle(
          drawable, ghost_rect, surface.m_pixels.tab_active_background,
          4.0F * scale, surface.m_pixels.sidebar_background);
      surface.draw_rectangle(drawable, ghost_rect, surface.m_pixels.accent);
      surface.draw_text(
          drawable, *surface.m_small_font, drag_item.label,
          ghost_rect.x + 8.0F * scale,
          ghost_rect.y + ghost_rect.height * 0.5F, surface.m_text.primary);
    }
  }
}

std::size_t ToolSidebar::viewport_row_count(
    const UI::Editor::StudioEditorLayoutResult &layout) const noexcept {
  const float scale = layout.dpi_scale;
  const float available_h =
      layout.tool_sidebar_bounds.height - header_height * scale;
  return static_cast<std::size_t>(
      std::max(0, static_cast<int>(available_h / (row_height * scale))));
}

std::optional<std::size_t> ToolSidebar::row_from_point(
    const UI::Editor::StudioEditorLayoutResult &layout,
    float point_y) const noexcept {
  const float scale = layout.dpi_scale;
  const float tree_top = layout.tool_sidebar_bounds.y + header_height * scale;
  if (point_y < tree_top || point_y > layout.tool_sidebar_bounds.bottom()) {
    return std::nullopt;
  }
  const int row = static_cast<int>((point_y - tree_top) / (row_height * scale));
  if (row < 0 || static_cast<std::size_t>(row) >= viewport_row_count(layout)) {
    return std::nullopt;
  }
  const std::size_t total_items = m_model.get_project_items().size();
  if (m_model.get_scroll_offset() + static_cast<std::size_t>(row) >=
      total_items) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(row);
}

UI::Rect ToolSidebar::scrollbar_bounds(
    const UI::Editor::StudioEditorLayoutResult &layout) const noexcept {
  const float scale = layout.dpi_scale;
  const float sb_w = 8.0F * scale;
  return UI::Rect{
      layout.tool_sidebar_bounds.right() - sb_w,
      layout.tool_sidebar_bounds.y + header_height * scale,
      sb_w,
      layout.tool_sidebar_bounds.height - header_height * scale,
  };
}

std::vector<std::size_t> ToolSidebar::get_sticky_items() const {
  const auto items = m_model.get_project_items();
  const std::size_t scroll_offset = m_model.get_scroll_offset();
  if (scroll_offset == 0 || items.empty()) {
    return {};
  }

  std::vector<std::size_t> sticky_indices;
  std::size_t current_idx = std::min(scroll_offset, items.size() - 1);
  int current_depth = static_cast<int>(items[current_idx].depth);

  for (int idx = static_cast<int>(current_idx); idx >= 0; --idx) {
    const auto &item = items[static_cast<std::size_t>(idx)];
    if (item.directory && static_cast<int>(item.depth) < current_depth) {
      sticky_indices.push_back(static_cast<std::size_t>(idx));
      current_depth = static_cast<int>(item.depth);
      if (current_depth == 0)
        break;
    }
  }

  std::reverse(sticky_indices.begin(), sticky_indices.end());
  return sticky_indices;
}

} // namespace Zenvra::Platform::X11::Components
