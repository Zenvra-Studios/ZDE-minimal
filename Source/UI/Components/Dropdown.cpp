#include "UI/Components/Dropdown.h"
#include "Utility/TextEncoding.h"

#include <algorithm>
#include <cmath>

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

namespace {

#if defined(_WIN32)
COLORREF to_color_ref(const Theme::Color &c) noexcept {
  return RGB(c.red, c.green, c.blue);
}

RECT to_native_rect(const Rect &r) noexcept {
  return RECT{static_cast<LONG>(r.x), static_cast<LONG>(r.y),
              static_cast<LONG>(r.right()), static_cast<LONG>(r.bottom())};
}

void draw_rect_solid(HDC hdc, const Rect &r, COLORREF color) {
  if (r.is_empty())
    return;
  const RECT rc = to_native_rect(r);
  HBRUSH brush = CreateSolidBrush(color);
  FillRect(hdc, &rc, brush);
  DeleteObject(brush);
}

void draw_rounded_rect(HDC hdc, const Rect &r, COLORREF bg_col,
                       COLORREF border_col, float radius) {
  if (r.is_empty())
    return;
  HBRUSH br = CreateSolidBrush(bg_col);
  HPEN pen = CreatePen(PS_SOLID, 1, border_col);
  HGDIOBJ old_br = SelectObject(hdc, br);
  HGDIOBJ old_pen = SelectObject(hdc, pen);
  const int rd = static_cast<int>(radius * 2.0F);
  RoundRect(hdc, static_cast<int>(r.x), static_cast<int>(r.y),
            static_cast<int>(r.right()), static_cast<int>(r.bottom()), rd, rd);
  SelectObject(hdc, old_pen);
  SelectObject(hdc, old_br);
  DeleteObject(pen);
  DeleteObject(br);
}
#endif

} // namespace

void Dropdown::set_items(std::vector<DropdownItem> items) {
  m_items = std::move(items);
  if (m_open) {
    calculate_layout(m_anchor_bounds, m_container_bounds, 1.0F);
  }
}

void Dropdown::open(const Rect &anchor_bounds, const Rect &container_bounds,
                    float dpi_scale) {
  m_anchor_bounds = anchor_bounds;
  m_container_bounds = container_bounds;
  m_open = true;
  m_scroll_offset = 0.0F;
  m_is_dragging_scrollbar = false;
  m_scrollbar_thumb_hovered = false;
  calculate_layout(anchor_bounds, container_bounds, dpi_scale);
}

void Dropdown::close() noexcept {
  m_open = false;
  m_is_dragging_scrollbar = false;
  m_scrollbar_thumb_hovered = false;
  m_layout_items.clear();
  m_bounds = Rect{};
  m_scrollbar_track = Rect{};
  m_scrollbar_thumb = Rect{};
}

void Dropdown::toggle(const Rect &anchor_bounds, const Rect &container_bounds,
                      float dpi_scale) {
  if (m_open) {
    close();
  } else {
    open(anchor_bounds, container_bounds, dpi_scale);
  }
}

void Dropdown::calculate_layout(const Rect &anchor_bounds,
                               const Rect &container_bounds, float dpi_scale) {
  m_anchor_bounds = anchor_bounds;
  m_container_bounds = container_bounds;

  if (!m_open || m_items.empty()) {
    m_layout_items.clear();
    m_bounds = Rect{};
    m_scrollbar_track = Rect{};
    m_scrollbar_thumb = Rect{};
    m_max_scroll = 0.0F;
    return;
  }

  // Determine if any item has subtitle/tag or custom font (requires 2-line height)
  bool has_descriptions = false;
  for (const auto &item : m_items) {
    if (!item.description.empty() || !item.preview_font_family.empty()) {
      has_descriptions = true;
      break;
    }
  }

  const float dd_w = anchor_bounds.width;
  const float item_h = (has_descriptions ? 38.0F : 30.0F) * dpi_scale;
  const float total_items_h =
      static_cast<float>(m_items.size()) * item_h + 8.0F * dpi_scale;
  const float max_dd_h = std::min(
      total_items_h, (has_descriptions ? 228.0F : 220.0F) * dpi_scale);

  // Available space below and above anchor
  const float space_below =
      (container_bounds.bottom() - 10.0F * dpi_scale) -
      (anchor_bounds.bottom() + 2.0F * dpi_scale);
  const float space_above =
      (anchor_bounds.y - 2.0F * dpi_scale) - (container_bounds.y + 40.0F * dpi_scale);

  float dd_y = anchor_bounds.bottom() + 2.0F * dpi_scale;
  float actual_dd_h = max_dd_h;

  if (space_below < max_dd_h && space_above > space_below) {
    actual_dd_h = std::min(max_dd_h, space_above);
    dd_y = anchor_bounds.y - 2.0F * dpi_scale - actual_dd_h;
  } else if (space_below < max_dd_h) {
    actual_dd_h = std::max(80.0F * dpi_scale, space_below);
  }

  m_bounds = Rect{anchor_bounds.x, dd_y, dd_w, actual_dd_h};
  m_max_scroll = std::max(0.0F, total_items_h - actual_dd_h);
  m_scroll_offset = std::clamp(m_scroll_offset, 0.0F, m_max_scroll);

  const float item_w = (m_max_scroll > 0.0F) ? (dd_w - 18.0F * dpi_scale)
                                              : (dd_w - 8.0F * dpi_scale);

  m_layout_items.clear();
  m_layout_items.reserve(m_items.size());

  float opt_y = m_bounds.y + 4.0F * dpi_scale - m_scroll_offset;
  for (const auto &item : m_items) {
    const Rect item_rc{m_bounds.x + 4.0F * dpi_scale, opt_y, item_w, item_h};
    const bool is_hovered = (m_hovered_id == item.id);
    const bool is_selected = (m_selected_id == item.id);
    m_layout_items.push_back({item, item_rc, is_hovered, is_selected});
    opt_y += item_h;
  }

  // Scrollbar
  if (m_max_scroll > 0.0F) {
    const float s_track_w = 6.0F * dpi_scale;
    m_scrollbar_track = Rect{
        m_bounds.right() - s_track_w - 4.0F * dpi_scale,
        m_bounds.y + 4.0F * dpi_scale, s_track_w,
        m_bounds.height - 8.0F * dpi_scale};
    const float view_ratio = m_bounds.height / total_items_h;
    const float thumb_h =
        std::max(22.0F * dpi_scale, m_scrollbar_track.height * view_ratio);
    const float travel = m_scrollbar_track.height - thumb_h;
    const float thumb_y =
        m_scrollbar_track.y +
        (m_max_scroll > 0.0F ? (m_scroll_offset / m_max_scroll) * travel : 0.0F);
    m_scrollbar_thumb = Rect{m_scrollbar_track.x, thumb_y, s_track_w, thumb_h};
  } else {
    m_scrollbar_track = Rect{};
    m_scrollbar_thumb = Rect{};
  }
}

bool Dropdown::is_point_inside(float x, float y) const noexcept {
  return m_open && m_bounds.contains(x, y);
}

bool Dropdown::handle_pointer_press(float x, float y, float dpi_scale) noexcept {
  if (!m_open)
    return false;

  if (!m_bounds.contains(x, y)) {
    return false;
  }

  // Scrollbar thumb click -> start drag
  if (!m_scrollbar_thumb.is_empty() && m_scrollbar_thumb.contains(x, y)) {
    m_is_dragging_scrollbar = true;
    m_drag_start_y = y;
    m_drag_start_scroll = m_scroll_offset;
    return true;
  }

  // Scrollbar track click -> page scroll
  if (!m_scrollbar_track.is_empty() && m_scrollbar_track.contains(x, y)) {
    if (y < m_scrollbar_thumb.y) {
      m_scroll_offset = std::max(0.0F, m_scroll_offset - 80.0F * dpi_scale);
    } else {
      m_scroll_offset =
          std::min(m_max_scroll, m_scroll_offset + 80.0F * dpi_scale);
    }
    calculate_layout(m_anchor_bounds, m_container_bounds, dpi_scale);
    return true;
  }

  // Item click -> select and close
  for (const auto &entry : m_layout_items) {
    if (entry.bounds.bottom() > m_bounds.y && entry.bounds.y < m_bounds.bottom() &&
        entry.bounds.contains(x, y)) {
      m_selected_id = entry.item.id;
      DropdownItem chosen = entry.item;
      close();
      if (m_on_select) {
        m_on_select(chosen);
      }
      return true;
    }
  }

  return true;
}

bool Dropdown::handle_pointer_move(float x, float y, float dpi_scale) noexcept {
  if (!m_open)
    return false;

  bool changed = false;

  // Handle scrollbar drag
  if (m_is_dragging_scrollbar && m_max_scroll > 0.0F) {
    const float track_h = m_scrollbar_track.height;
    const float thumb_h = m_scrollbar_thumb.height;
    const float travel = track_h - thumb_h;
    if (travel > 0.0F) {
      const float delta_y = y - m_drag_start_y;
      const float delta_scroll = (delta_y / travel) * m_max_scroll;
      const float new_scroll =
          std::clamp(m_drag_start_scroll + delta_scroll, 0.0F, m_max_scroll);
      if (std::abs(new_scroll - m_scroll_offset) > 0.01F) {
        m_scroll_offset = new_scroll;
        calculate_layout(m_anchor_bounds, m_container_bounds, dpi_scale);
        changed = true;
      }
    }
    return true;
  }

  // Scrollbar thumb hover
  const bool thumb_h =
      !m_scrollbar_thumb.is_empty() && m_scrollbar_thumb.contains(x, y);
  if (m_scrollbar_thumb_hovered != thumb_h) {
    m_scrollbar_thumb_hovered = thumb_h;
    changed = true;
  }

  // Item hover
  std::string new_hovered;
  if (m_bounds.contains(x, y) && !m_scrollbar_track.contains(x, y)) {
    for (const auto &entry : m_layout_items) {
      if (entry.bounds.bottom() > m_bounds.y && entry.bounds.y < m_bounds.bottom() &&
          entry.bounds.contains(x, y)) {
        new_hovered = entry.item.id;
        break;
      }
    }
  }

  if (m_hovered_id != new_hovered) {
    m_hovered_id = std::move(new_hovered);
    for (auto &entry : m_layout_items) {
      entry.is_hovered = (entry.item.id == m_hovered_id);
    }
    changed = true;
  }

  return changed;
}

bool Dropdown::handle_pointer_release(float, float) noexcept {
  if (m_is_dragging_scrollbar) {
    m_is_dragging_scrollbar = false;
    return true;
  }
  return false;
}

bool Dropdown::handle_scroll(float delta_y, float mouse_x, float mouse_y,
                            float dpi_scale) noexcept {
  if (!m_open || !m_bounds.contains(mouse_x, mouse_y))
    return false;

  if (m_max_scroll > 0.0F) {
    const float step = 38.0F * dpi_scale;
    const float new_scroll =
        std::clamp(m_scroll_offset - delta_y * step, 0.0F, m_max_scroll);
    if (std::abs(new_scroll - m_scroll_offset) > 0.01F) {
      m_scroll_offset = new_scroll;
      calculate_layout(m_anchor_bounds, m_container_bounds, dpi_scale);
      return true;
    }
  }
  return true;
}

bool Dropdown::handle_key(std::uintptr_t key, float dpi_scale) noexcept {
  if (!m_open)
    return false;

#if defined(_WIN32)
  if (key == VK_ESCAPE) {
    close();
    return true;
  }
  if (key == VK_RETURN) {
    if (!m_hovered_id.empty()) {
      for (const auto &entry : m_layout_items) {
        if (entry.item.id == m_hovered_id) {
          m_selected_id = entry.item.id;
          DropdownItem chosen = entry.item;
          close();
          if (m_on_select) {
            m_on_select(chosen);
          }
          return true;
        }
      }
    }
    close();
    return true;
  }
  if (key == VK_UP) {
    if (m_max_scroll > 0.0F) {
      m_scroll_offset = std::max(0.0F, m_scroll_offset - 30.0F * dpi_scale);
      calculate_layout(m_anchor_bounds, m_container_bounds, dpi_scale);
      return true;
    }
  }
  if (key == VK_DOWN) {
    if (m_max_scroll > 0.0F) {
      m_scroll_offset =
          std::min(m_max_scroll, m_scroll_offset + 30.0F * dpi_scale);
      calculate_layout(m_anchor_bounds, m_container_bounds, dpi_scale);
      return true;
    }
  }
#endif

  return false;
}

#if defined(_WIN32)
void Dropdown::render(HDC hdc, const Theme::StudioTheme &theme, float dpi_scale,
                      HFONT regular_font, HFONT semibold_font,
                      HFONT small_font) const {
  if (!m_open || m_layout_items.empty())
    return;

  // 1. Dropdown container box with VS Code popover styling
  draw_rounded_rect(hdc, m_bounds, to_color_ref(theme.command_center_background),
                    to_color_ref(theme.accent), 3.0F * dpi_scale);

  // 2. Clip items strictly within rounded dropdown box
  const int saved_dc = SaveDC(hdc);
  HRGN clip_rgn = CreateRoundRectRgn(
      static_cast<int>(m_bounds.x + 1.0F * dpi_scale),
      static_cast<int>(m_bounds.y + 1.0F * dpi_scale),
      static_cast<int>(m_bounds.right() - 1.0F * dpi_scale),
      static_cast<int>(m_bounds.bottom() - 1.0F * dpi_scale),
      static_cast<int>(3.0F * dpi_scale), static_cast<int>(3.0F * dpi_scale));
  SelectClipRgn(hdc, clip_rgn);

  for (const auto &entry : m_layout_items) {
    if (entry.bounds.bottom() <= m_bounds.y || entry.bounds.y >= m_bounds.bottom()) {
      continue;
    }

    const auto &item = entry.item;
    const bool is_hovered = entry.is_hovered;
    const bool is_selected = (item.id == m_selected_id);

    // Hover background
    if (is_hovered) {
      draw_rounded_rect(hdc, entry.bounds, to_color_ref(theme.hover),
                        to_color_ref(theme.hover), 2.0F * dpi_scale);
    }

    // Active indicator / checkmark pip on left
    if (is_selected) {
      Rect pip{entry.bounds.x + 4.0F * dpi_scale,
               entry.bounds.y + 7.0F * dpi_scale, 3.0F * dpi_scale,
               entry.bounds.height - 14.0F * dpi_scale};
      draw_rect_solid(hdc, pip, to_color_ref(theme.accent));
    }

    // Color swatches (e.g. for Color Themes)
    float chips_reserved_w = 0.0F;
    if (!item.preview_colors.empty()) {
      const float chip_w = 12.0F * dpi_scale;
      const float chip_h = 12.0F * dpi_scale;
      const float chip_spacing = 3.0F * dpi_scale;
      const std::size_t num_chips = item.preview_colors.size();
      const float total_chips_w =
          static_cast<float>(num_chips) * chip_w +
          static_cast<float>(num_chips > 0 ? num_chips - 1 : 0) * chip_spacing;
      chips_reserved_w = total_chips_w + 10.0F * dpi_scale;

      const float chips_x =
          entry.bounds.right() - total_chips_w -
          (m_max_scroll > 0.0F ? 16.0F * dpi_scale : 8.0F * dpi_scale);
      const float chips_y =
          entry.bounds.y + (entry.bounds.height - chip_h) * 0.5F;

      for (std::size_t c_idx = 0; c_idx < num_chips; ++c_idx) {
        Rect chip_rc{chips_x + static_cast<float>(c_idx) * (chip_w + chip_spacing),
                     chips_y, chip_w, chip_h};
        draw_rounded_rect(hdc, chip_rc, to_color_ref(item.preview_colors[c_idx]),
                          to_color_ref(theme.command_center_border),
                          2.0F * dpi_scale);
      }
    }

    // Custom Font preview face (e.g. for Font Family)
    HFONT custom_face_font = nullptr;
    if (!item.preview_font_family.empty()) {
      const std::wstring w_face =
          Utility::utf8_to_wide(item.preview_font_family).value_or(L"");
      if (!w_face.empty()) {
        custom_face_font = CreateFontW(
            -static_cast<int>(13.5F * dpi_scale), 0, 0, 0,
            is_selected ? FW_SEMIBOLD : FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, w_face.c_str());
      }
    }

    HGDIOBJ prev_font = SelectObject(
        hdc, custom_face_font
                 ? custom_face_font
                 : (is_selected ? semibold_font : regular_font));
    SetTextColor(hdc, is_selected ? RGB(255, 255, 255)
                                  : to_color_ref(theme.text_primary));

    if (!item.description.empty()) {
      // 2-line layout: Title at top, description below
      Rect name_rc_box{
          entry.bounds.x + 14.0F * dpi_scale, entry.bounds.y + 3.0F * dpi_scale,
          entry.bounds.width - 20.0F * dpi_scale - chips_reserved_w,
          16.0F * dpi_scale};
      RECT n_rc = to_native_rect(name_rc_box);
      const std::wstring w_name =
          Utility::utf8_to_wide(item.label).value_or(L"");
      DrawTextW(hdc, w_name.c_str(), -1, &n_rc,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

      // Description
      SelectObject(hdc, small_font);
      SetTextColor(hdc, to_color_ref(theme.text_secondary));
      Rect desc_rc_box{
          entry.bounds.x + 14.0F * dpi_scale, entry.bounds.y + 19.0F * dpi_scale,
          entry.bounds.width - 20.0F * dpi_scale - chips_reserved_w,
          14.0F * dpi_scale};
      RECT d_rc = to_native_rect(desc_rc_box);
      const std::wstring w_desc =
          Utility::utf8_to_wide(item.description).value_or(L"");
      DrawTextW(hdc, w_desc.c_str(), -1, &d_rc,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    } else {
      // Single-line centered label
      Rect name_rc_box{
          entry.bounds.x + 14.0F * dpi_scale, entry.bounds.y,
          entry.bounds.width - 18.0F * dpi_scale - chips_reserved_w,
          entry.bounds.height};
      RECT n_rc = to_native_rect(name_rc_box);
      const std::wstring w_name =
          Utility::utf8_to_wide(item.label).value_or(L"");
      DrawTextW(hdc, w_name.c_str(), -1, &n_rc,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    SelectObject(hdc, prev_font);
    if (custom_face_font) {
      DeleteObject(custom_face_font);
    }
  }

  // Scrollbar (Minimalist 6px pill, VS Code style)
  if (m_max_scroll > 0.0F && !m_scrollbar_thumb.is_empty()) {
    const bool is_thumb_active =
        m_is_dragging_scrollbar || m_scrollbar_thumb_hovered;
    const COLORREF thumb_col =
        is_thumb_active ? RGB(95, 100, 115) : to_color_ref(theme.hover);
    draw_rounded_rect(hdc, m_scrollbar_thumb, thumb_col, thumb_col,
                      3.0F * dpi_scale);
  }

  RestoreDC(hdc, saved_dc);
  DeleteObject(clip_rgn);
}
#endif

} // namespace Zenvra::UI::Components
