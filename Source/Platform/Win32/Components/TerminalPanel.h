#pragma once

#include "Platform/Win32/Event/ScrollEvent.h"
#include "Terminal/TerminalPanelModel.h"
#include "Terminal/TerminalResizeModel.h"
#include "UI/Editor/CaretBlinkModel.h"
#include "UI/Editor/StudioEditorModel.h"

#include <windows.h>

#include <cstdbool>
#include <cstddef>
#include <filesystem>
#include <string_view>
#include <unordered_map>

namespace Zenvra::Platform::Win32::Components {

class StudioWorkspaceRenderer;

class TerminalPanel {
public:
  [[nodiscard]] bool toggle();
  [[nodiscard]] bool execute_command(std::string_view command,
                                     const std::filesystem::path &working_directory = {});
  [[nodiscard]] bool
  handle_pointer_press(const UI::Editor::StudioEditorLayoutResult &layout,
                       float point_x, float point_y);
  [[nodiscard]] bool
  handle_double_click(const UI::Editor::StudioEditorLayoutResult &layout,
                      float point_x, float point_y) noexcept;
  [[nodiscard]] bool
  handle_pointer_move(const UI::Editor::StudioEditorLayoutResult &layout,
                      float point_x, float point_y) noexcept;
  [[nodiscard]] bool
  handle_pointer_drag(const UI::Editor::StudioEditorLayoutResult &layout,
                      float point_x, float point_y) noexcept;
  [[nodiscard]] bool
  handle_pointer_drag(const UI::Editor::StudioEditorLayoutResult &layout,
                      float point_y) noexcept;
  [[nodiscard]] bool handle_pointer_release() noexcept;
  [[nodiscard]] bool handle_text_input(std::string_view text);
  [[nodiscard]] bool handle_key(Terminal::TerminalInputKey key);
  [[nodiscard]] bool handle_control(char letter);
  [[nodiscard]] bool handle_scroll(const Event::ScrollEvent &event) noexcept;
  [[nodiscard]] bool poll();
  [[nodiscard]] bool tick_animations() noexcept;
  void shutdown() noexcept;

  [[nodiscard]] bool is_visible() const noexcept;
  [[nodiscard]] bool is_focused() const noexcept;
  [[nodiscard]] bool is_resizing() const noexcept;
  [[nodiscard]] bool is_maximized() const noexcept;
  [[nodiscard]] float get_height() const noexcept;
  bool set_resize_hovered(bool hovered) noexcept { return m_resize_model.set_hovered(hovered); }
  bool begin_resize() noexcept { return m_resize_model.begin_resize(); }
  void set_focused(bool focused) noexcept;
  void set_working_directory(const std::filesystem::path &directory) noexcept;

  enum class PanelChannel { Terminal, Output };
  [[nodiscard]] PanelChannel get_active_channel() const noexcept {
    return m_active_channel;
  }
  void set_active_channel(PanelChannel channel) noexcept {
    m_active_channel = channel;
  }
  [[nodiscard]] bool is_dragging_tab_scrollbar() const noexcept {
    return m_dragging_tab_scrollbar;
  }
  [[nodiscard]] bool is_selecting_text() const noexcept {
    return m_selecting_text;
  }
  [[nodiscard]] float get_tab_scroll_offset() const noexcept {
    return m_tab_scroll_offset;
  }

  [[nodiscard]] bool
  contains(const UI::Editor::StudioEditorLayoutResult &layout, float point_x,
           float point_y) const noexcept;
  [[nodiscard]] bool
  is_resize_handle_point(const UI::Editor::StudioEditorLayoutResult &layout,
                         float point_x, float point_y) const noexcept;

  [[nodiscard]] bool
  is_interactive_point(const UI::Editor::StudioEditorLayoutResult &layout,
                       float point_x, float point_y) const noexcept;

  void render(const StudioWorkspaceRenderer &surface, HDC device_context,
              const UI::Editor::StudioEditorLayoutResult &layout);

private:
  [[nodiscard]] float tab_strip_start_x(
      const UI::Editor::StudioEditorLayoutResult &layout) const noexcept;
  [[nodiscard]] float tab_strip_end_x(
      const UI::Editor::StudioEditorLayoutResult &layout) const noexcept;
  [[nodiscard]] UI::Rect tab_viewport_bounds(
      const UI::Editor::StudioEditorLayoutResult &layout) const noexcept;
  [[nodiscard]] UI::Rect scroll_left_button_bounds(
      const UI::Editor::StudioEditorLayoutResult &layout) const noexcept;
  [[nodiscard]] UI::Rect scroll_right_button_bounds(
      const UI::Editor::StudioEditorLayoutResult &layout) const noexcept;
  [[nodiscard]] UI::Rect tab_scrollbar_track_bounds(
      const UI::Editor::StudioEditorLayoutResult &layout) const noexcept;
  [[nodiscard]] UI::Rect tab_scrollbar_thumb_bounds(
      const UI::Editor::StudioEditorLayoutResult &layout) const noexcept;
  void ensure_tab_visible(
      const UI::Editor::StudioEditorLayoutResult &layout,
      std::size_t index) noexcept;

  [[nodiscard]] UI::Rect
  session_tab_bounds(const UI::Editor::StudioEditorLayoutResult &layout,
                     std::size_t index) const noexcept;
  [[nodiscard]] UI::Rect terminal_channel_tab_bounds(
      const UI::Editor::StudioEditorLayoutResult &layout) const noexcept;
  [[nodiscard]] UI::Rect output_channel_tab_bounds(
      const UI::Editor::StudioEditorLayoutResult &layout) const noexcept;
  [[nodiscard]] UI::Rect clear_output_button_bounds(
      const UI::Editor::StudioEditorLayoutResult &layout) const noexcept;
  [[nodiscard]] UI::Rect add_button_bounds(
      const UI::Editor::StudioEditorLayoutResult &layout) const noexcept;
  [[nodiscard]] UI::Rect close_button_bounds(
      const UI::Editor::StudioEditorLayoutResult &layout) const noexcept;
  [[nodiscard]] UI::Rect resize_handle_bounds(
      const UI::Editor::StudioEditorLayoutResult &layout) const noexcept;

  Terminal::TerminalPanelModel m_model;
  Terminal::TerminalResizeModel m_resize_model;
  UI::Editor::CaretBlinkModel m_caret_blink;
  std::filesystem::path m_working_directory;
  float m_cached_line_height = 0.0F;
  float m_cached_char_width = 0.0F;
  std::size_t m_last_total_rows = 0;
  std::size_t m_last_visible_rows = 0;
  bool m_selecting_text = false;
  float m_tab_scroll_offset = 0.0F;
  mutable float m_max_tab_scroll = 0.0F;
  bool m_hovered_tab_scrollbar = false;
  bool m_dragging_tab_scrollbar = false;
  float m_tab_scroll_drag_start_x = 0.0F;
  float m_tab_scroll_drag_initial_offset = 0.0F;
  bool m_hovered_scroll_left = false;
  bool m_hovered_scroll_right = false;
  mutable UI::Rect m_last_header_bounds;
  mutable float m_last_scale = 1.0F;
  mutable std::unordered_map<std::size_t, float> m_tab_animated_offset_x;
  PanelChannel m_active_channel = PanelChannel::Terminal;
};

} // namespace Zenvra::Platform::Win32::Components
