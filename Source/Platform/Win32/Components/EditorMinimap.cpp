#include "Platform/Win32/Components/EditorMinimap.h"

#include "Platform/Win32/Components/StudioWorkspaceRenderer.h"
#include "Utility/Fonts.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <string_view>

namespace Zenvra::Platform::Win32::Components {

namespace {

std::string normalize_minimap_text(std::string_view text) {
  std::string result;
  result.reserve(text.size());
  for (const char character : text) {
    if (character == '\t') {
      result.append(4, ' ');
    } else {
      result.push_back(character);
    }
  }
  return result;
}

} // namespace

bool EditorMinimap::is_point(const UI::Editor::StudioEditorLayoutResult &layout,
                             float point_x, float point_y) const noexcept {
  return layout.minimap_bounds.contains(point_x, point_y);
}

std::optional<std::size_t> EditorMinimap::handle_pointer_press(
    const UI::Editor::StudioEditorLayoutResult &layout, float point_x,
    float point_y, std::size_t total_lines, std::size_t visible_lines,
    std::size_t first_visible_line) noexcept {
  if (!is_point(layout, point_x, point_y)) {
    return std::nullopt;
  }
  m_pointer_dragging = true;
  return handle_pointer_drag(layout, point_y, total_lines, visible_lines,
                             first_visible_line);
}

std::optional<std::size_t> EditorMinimap::handle_pointer_drag(
    const UI::Editor::StudioEditorLayoutResult &layout, float point_y,
    std::size_t total_lines, std::size_t visible_lines,
    std::size_t /*first_visible_line*/) noexcept {
  if (!m_pointer_dragging || total_lines <= 1) {
    return std::nullopt;
  }
  const UI::Rect bounds = layout.minimap_bounds;
  if (bounds.height <= 0.0F) {
    return std::nullopt;
  }
  const std::size_t max_first_line = total_lines - 1;
  const float font_height = 3.0F * layout.dpi_scale;
  const float row_height = std::max(font_height, 2.0F * layout.dpi_scale);
  const float total_content_height = static_cast<float>(total_lines) * row_height;
  const float vp_h = std::clamp(
      static_cast<float>(visible_lines) * row_height,
      14.0F * layout.dpi_scale,
      bounds.height);

  float max_travel = 0.0F;
  if (total_content_height <= bounds.height) {
    max_travel = std::max(total_content_height - vp_h, 1.0F);
  } else {
    max_travel = std::max(bounds.height - vp_h, 1.0F);
  }

  const float click_offset = point_y - bounds.y - vp_h * 0.5F;
  const float ratio = std::clamp(click_offset / max_travel, 0.0F, 1.0F);
  const std::size_t requested_first = static_cast<std::size_t>(
      std::lround(ratio * static_cast<float>(max_first_line)));
  return std::min(requested_first, max_first_line);
}

bool EditorMinimap::handle_pointer_release() noexcept {
  const bool was_dragging = m_pointer_dragging;
  m_pointer_dragging = false;
  return was_dragging;
}

void EditorMinimap::render(const StudioWorkspaceRenderer &surface,
                           HDC device_context,
                           const UI::Editor::StudioEditorLayoutResult &layout,
                           const UI::Editor::TextDocumentModel &document,
                           std::size_t first_visible_line,
                           std::size_t visible_lines) const {
  const UI::Rect bounds = layout.minimap_bounds;
  if (bounds.is_empty()) {
    return;
  }

  // Hardware GDI clip region to prevent any pixel bleeding
  const int clip_left = static_cast<int>(std::floor(bounds.x));
  const int clip_top = static_cast<int>(std::floor(bounds.y));
  const int clip_right = static_cast<int>(std::ceil(bounds.right()));
  const int clip_bottom = static_cast<int>(std::ceil(bounds.bottom()));
  HRGN clip_region = CreateRectRgn(clip_left, clip_top, clip_right, clip_bottom);
  SaveDC(device_context);
  SelectClipRgn(device_context, clip_region);

  surface.fill_rectangle(device_context, bounds,
                         surface.m_palette.editor_background);
  surface.draw_line(device_context, clip_left, clip_top, clip_left, clip_bottom,
                    surface.m_palette.border);

  const std::size_t total_lines =
      std::max<std::size_t>(document.get_line_count(), 1);
  const float font_height =
      static_cast<float>(surface.m_minimap_font->getHeight(device_context));
  const float row_height = std::max(font_height, 2.0F * layout.dpi_scale);

  // Measure average char width ONCE per frame to avoid thousands of GDI measure calls
  const float char_width = std::max(
      static_cast<float>(surface.get_text_width(device_context, *surface.m_minimap_font, "X")),
      1.5F * layout.dpi_scale);

  const float total_content_height =
      static_cast<float>(total_lines) * row_height;
  const std::size_t max_first_line =
      (total_lines > 1) ? (total_lines - 1) : 0;

  const float scroll_progress = (max_first_line > 0)
      ? std::clamp(static_cast<float>(first_visible_line) / static_cast<float>(max_first_line), 0.0F, 1.0F)
      : 0.0F;

  float vp_h = 0.0F;
  float minimap_scroll_y = 0.0F;
  float slider_y = bounds.y;

  if (total_content_height <= bounds.height) {
    minimap_scroll_y = 0.0F;
    vp_h = std::clamp(
        static_cast<float>(visible_lines) * row_height,
        14.0F * layout.dpi_scale,
        bounds.height);
    slider_y = bounds.y + static_cast<float>(first_visible_line) * row_height;
  } else {
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

  // 2. Viewport slider bounds (reaches all the way to bottom)
  const float inset_x = 2.0F * layout.dpi_scale;
  const UI::Rect viewport{
      bounds.x + inset_x,
      slider_y,
      std::max(bounds.width - 2.0F * inset_x, 0.0F),
      vp_h
  };

  // Draw active viewport background
  surface.fill_rectangle(device_context, viewport,
                         surface.m_palette.active_line_background);

  // 3. Render visible lines in minimap
  const std::size_t start_line = static_cast<std::size_t>(
      std::max(0, static_cast<int>(std::floor(minimap_scroll_y / row_height))));
  const std::size_t end_line =
      std::min(total_lines,
               static_cast<std::size_t>(
                   std::ceil((minimap_scroll_y + bounds.height) / row_height) + 1));

  const float left_padding = 4.0F * layout.dpi_scale;
  const float right_padding = 4.0F * layout.dpi_scale;

  const auto token_color =
      [&surface](UI::Editor::EditorTokenKind kind) -> const UI::Theme::Color & {
    switch (kind) {
    case UI::Editor::EditorTokenKind::Keyword:
      return surface.m_palette.keyword;
    case UI::Editor::EditorTokenKind::Number:
      return surface.m_palette.number;
    case UI::Editor::EditorTokenKind::Label:
      return surface.m_palette.label;
    case UI::Editor::EditorTokenKind::Type:
      return surface.m_palette.type;
    case UI::Editor::EditorTokenKind::Comment:
      return surface.m_palette.comment;
    case UI::Editor::EditorTokenKind::String:
      return surface.m_palette.success;
    case UI::Editor::EditorTokenKind::Plain:
      return surface.m_palette.text_primary;
    }
    return surface.m_palette.text_primary;
  };

  for (std::size_t line_index = start_line; line_index < end_line;
       ++line_index) {
    const std::string_view line = document.get_line(line_index);
    const float center_y =
        bounds.y + (static_cast<float>(line_index) + 0.5F) * row_height -
        minimap_scroll_y;
    if (center_y < bounds.y - row_height ||
        center_y > bounds.bottom() + row_height) {
      continue;
    }

    float token_x = bounds.x + left_padding;
    std::array<UI::Editor::EditorToken, UI::Editor::maximum_editor_tokens>
        tokens{};
    auto line_state = document.get_line_state(line_index);
    const std::size_t token_count = UI::Editor::tokenize_editor_line(
        line, tokens, document.get_file_name(), &line_state);

    for (std::size_t token_index = 0; token_index < token_count;
         ++token_index) {
      if (token_x >= bounds.right() - right_padding) {
        break;
      }
      const UI::Editor::EditorToken &token = tokens[token_index];
      if (token.text.empty()) {
        continue;
      }

      // Fast-path whitespace without string allocations or GDI draw
      bool is_all_whitespace = true;
      for (char c : token.text) {
        if (c != ' ' && c != '\t') {
          is_all_whitespace = false;
          break;
        }
      }
      if (is_all_whitespace) {
        std::size_t space_count = 0;
        for (char c : token.text) {
          space_count += (c == '\t' ? 4 : 1);
        }
        token_x += static_cast<float>(space_count) * char_width;
        continue;
      }

      const std::string text = normalize_minimap_text(token.text);
      const float token_width = static_cast<float>(text.size()) * char_width;
      surface.draw_text(device_context, *surface.m_minimap_font, text,
                        token_x, center_y, token_color(token.kind));
      token_x += token_width;
    }

    if (line_index == document.get_caret_line()) {
      surface.fill_rectangle(
          device_context,
          UI::Rect{bounds.x + layout.dpi_scale, center_y - row_height * 0.42F,
                   std::max(layout.dpi_scale, 1.0F), row_height * 0.84F},
          surface.m_palette.accent);
    }
  }

  // Draw viewport slider outline
  surface.draw_rectangle(device_context, viewport,
                         m_pointer_dragging ? surface.m_palette.accent
                                            : surface.m_palette.text_muted);

  RestoreDC(device_context, -1);
  DeleteObject(clip_region);
}

} // namespace Zenvra::Platform::Win32::Components
