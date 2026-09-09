#pragma once

#include "UI/Geometry.h"
#include "UI/Theme/StudioTheme.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace Zenvra::UI::Components {

struct DropdownItem {
  std::string id;
  std::string label;
  std::string description;
  std::vector<Theme::Color> preview_colors;
  std::string preview_font_family;

  bool operator==(const DropdownItem &other) const = default;
};

struct DropdownItemLayout {
  DropdownItem item;
  Rect bounds;
  bool is_hovered = false;
  bool is_selected = false;
};

class Dropdown {
public:
  Dropdown() = default;

  void set_items(std::vector<DropdownItem> items);
  [[nodiscard]] const std::vector<DropdownItem> &get_items() const noexcept {
    return m_items;
  }

  void set_selected_id(std::string id) { m_selected_id = std::move(id); }
  [[nodiscard]] const std::string &get_selected_id() const noexcept {
    return m_selected_id;
  }

  void set_owner_id(std::string owner_id) { m_owner_id = std::move(owner_id); }
  [[nodiscard]] const std::string &get_owner_id() const noexcept {
    return m_owner_id;
  }

  void set_on_select(std::function<void(const DropdownItem &)> callback) {
    m_on_select = std::move(callback);
  }

  void open(const Rect &anchor_bounds, const Rect &container_bounds,
            float dpi_scale);
  void close() noexcept;
  void toggle(const Rect &anchor_bounds, const Rect &container_bounds,
              float dpi_scale);
  [[nodiscard]] bool is_open() const noexcept { return m_open; }

  void calculate_layout(const Rect &anchor_bounds, const Rect &container_bounds,
                        float dpi_scale);

  [[nodiscard]] const Rect &get_bounds() const noexcept { return m_bounds; }
  [[nodiscard]] const Rect &get_scrollbar_track() const noexcept {
    return m_scrollbar_track;
  }
  [[nodiscard]] const Rect &get_scrollbar_thumb() const noexcept {
    return m_scrollbar_thumb;
  }
  [[nodiscard]] const std::vector<DropdownItemLayout> &
  get_layout_items() const noexcept {
    return m_layout_items;
  }
  [[nodiscard]] float get_scroll_offset() const noexcept {
    return m_scroll_offset;
  }
  [[nodiscard]] float get_max_scroll() const noexcept { return m_max_scroll; }

  [[nodiscard]] bool is_point_inside(float x, float y) const noexcept;
  [[nodiscard]] bool handle_pointer_press(float x, float y,
                                         float dpi_scale) noexcept;
  [[nodiscard]] bool handle_pointer_move(float x, float y,
                                        float dpi_scale) noexcept;
  [[nodiscard]] bool handle_pointer_release(float x, float y) noexcept;
  [[nodiscard]] bool handle_scroll(float delta_y, float mouse_x, float mouse_y,
                                   float dpi_scale) noexcept;
  [[nodiscard]] bool handle_key(std::uintptr_t key, float dpi_scale) noexcept;

  [[nodiscard]] bool is_dragging_scrollbar() const noexcept {
    return m_is_dragging_scrollbar;
  }
  [[nodiscard]] bool is_scrollbar_thumb_hovered() const noexcept {
    return m_scrollbar_thumb_hovered;
  }

#if defined(_WIN32)
  void render(HDC hdc, const Theme::StudioTheme &theme, float dpi_scale,
              HFONT regular_font, HFONT semibold_font, HFONT small_font) const;
#endif

private:
  bool m_open = false;
  std::string m_owner_id;
  std::string m_selected_id;
  std::string m_hovered_id;
  std::vector<DropdownItem> m_items;
  std::function<void(const DropdownItem &)> m_on_select;

  Rect m_anchor_bounds;
  Rect m_container_bounds;
  Rect m_bounds;
  Rect m_scrollbar_track;
  Rect m_scrollbar_thumb;
  std::vector<DropdownItemLayout> m_layout_items;

  float m_scroll_offset = 0.0F;
  float m_max_scroll = 0.0F;
  bool m_is_dragging_scrollbar = false;
  bool m_scrollbar_thumb_hovered = false;
  float m_drag_start_y = 0.0F;
  float m_drag_start_scroll = 0.0F;
};

} // namespace Zenvra::UI::Components
