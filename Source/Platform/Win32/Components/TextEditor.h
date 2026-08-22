#pragma once

#include "Language/Protocol/LspTypes.h"
#include "Platform/Win32/Components/EditorMinimap.h"
#include "Platform/Win32/Components/EditorScrollbar.h"
#include "Platform/Win32/Event/ScrollEvent.h"
#include "UI/Components/BreadcrumbBar.h"
#include "UI/Components/Button.h"
#include "UI/Components/CompletionPopup.h"
#include "UI/Components/EditorFolding.h"
#include "UI/Components/HoverTooltip.h"
#include "UI/Components/SignatureHelpWidget.h"
#include "UI/Editor/BraceAnimationModel.h"
#include "UI/Editor/CaretBlinkModel.h"
#include "UI/Editor/EditorController.h"
#include "UI/Editor/SelectionAnimationModel.h"
#include "UI/Editor/StudioEditorModel.h"
#include "Utility/DragDropModel.h"

#include <windows.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Zenvra::Platform::Win32::Components {

class StudioWorkspaceRenderer;

class TextEditor {
public:
  [[nodiscard]] bool open_file(const std::filesystem::path &path);
  [[nodiscard]] bool open_file_at_location(const std::filesystem::path &path,
                                           std::size_t line,
                                           std::size_t column);
  void sync_lsp_active_document();
  [[nodiscard]] bool close_file(const std::filesystem::path &path);
  [[nodiscard]] bool close_all_files();
  [[nodiscard]] std::size_t
  open_dropped_paths(std::span<const std::filesystem::path> dropped_paths);
  [[nodiscard]] bool create_buffer();
  [[nodiscard]] bool handle_pointer_press(
      const StudioWorkspaceRenderer &surface, HDC device_context,
      const UI::Editor::StudioEditorLayoutResult &layout, float point_x,
      float point_y, bool extend_selection, std::string &command_out);
  [[nodiscard]] bool
  is_tab_interactive_point(const StudioWorkspaceRenderer &surface,
                           HDC device_context,
                           const UI::Editor::StudioEditorLayoutResult &layout,
                           float point_x, float point_y) const noexcept;
  [[nodiscard]] bool
  is_empty_state_interactive_point(float point_x, float point_y) const noexcept;
  [[nodiscard]] bool
  handle_pointer_move(const UI::Editor::StudioEditorLayoutResult &layout,
                      float point_x, float point_y) noexcept;
  [[nodiscard]] bool
  handle_pointer_drag(const StudioWorkspaceRenderer &surface,
                      HDC device_context,
                      const UI::Editor::StudioEditorLayoutResult &layout,
                      float point_x, float point_y);
  [[nodiscard]] bool handle_pointer_release() noexcept;
  [[nodiscard]] bool
  handle_scroll(const StudioWorkspaceRenderer &surface,
                const UI::Editor::StudioEditorLayoutResult &layout,
                const Event::ScrollEvent &event) noexcept;
  [[nodiscard]] bool handle_input(UI::Editor::EditorInputCommand command,
                                  bool extend_selection);
  [[nodiscard]] bool handle_action(UI::Editor::EditorAction action);
  [[nodiscard]] std::optional<bool> handle_command(std::string_view command_id);
  [[nodiscard]] std::optional<bool>
  is_command_enabled(std::string_view command_id) const noexcept;
  [[nodiscard]] bool handle_text_input(std::string_view utf8_text);
  [[nodiscard]] bool trigger_completion();
  [[nodiscard]] bool is_focused() const noexcept;
  [[nodiscard]] bool
  is_scrollbar_point(const UI::Editor::StudioEditorLayoutResult &layout,
                     float point_x, float point_y) const noexcept;
  [[nodiscard]] bool
  is_minimap_point(const UI::Editor::StudioEditorLayoutResult &layout,
                   float point_x, float point_y) const noexcept;
  [[nodiscard]] bool is_split_resize_handle_point(
      const UI::Editor::StudioEditorLayoutResult &layout, float point_x,
      float point_y) const noexcept;
  [[nodiscard]] bool is_split_resizing() const noexcept {
    return m_is_resizing_split;
  }
  [[nodiscard]] bool is_split_active() const noexcept {
    return m_is_split && m_split_document_index.has_value();
  }
  [[nodiscard]] std::size_t get_active_document_line_count() const noexcept {
    const auto* doc = m_controller.get_active_document();
    return doc != nullptr ? doc->get_line_count() : 1;
  }
  void reset_split() noexcept;
  [[nodiscard]] bool
  is_fold_margin_point(const UI::Editor::StudioEditorLayoutResult &layout,
                       float point_x, float point_y) const noexcept;
  enum class SplitPaneFocus { Left, Right };

  [[nodiscard]] UI::Editor::TextDocumentModel *get_focused_document() noexcept;
  [[nodiscard]] const UI::Editor::TextDocumentModel *
  get_focused_document() const noexcept;
  [[nodiscard]] SplitPaneFocus get_focused_pane() const noexcept {
    return m_focused_pane;
  }
  void set_focused_pane(SplitPaneFocus pane) noexcept { m_focused_pane = pane; }

  [[nodiscard]] bool tick_animations() noexcept;
  [[nodiscard]] bool check_external_file_changes();
  [[nodiscard]] const UI::Editor::TextDocumentModel *
  get_document() const noexcept;

  void set_window_handle(HWND hwnd) noexcept { m_window_handle = hwnd; }
  [[nodiscard]] HWND get_window_handle() const noexcept {
    return m_window_handle;
  }
  void
  on_diagnostics_updated(const std::string &uri,
                         std::vector<Language::Protocol::Diagnostic> diags);

  [[nodiscard]] bool go_to_definition();
  [[nodiscard]] bool select_all_occurrences();

  void render(const StudioWorkspaceRenderer &surface, HDC device_context,
              const UI::Editor::StudioEditorLayoutResult &layout) const;
  void
  render_overlays(const StudioWorkspaceRenderer &surface, HDC device_context,
                  const UI::Editor::StudioEditorLayoutResult &layout) const;

private:
  HWND m_window_handle = nullptr;
  static constexpr std::size_t max_visible_tabs = 128;

  struct TabActionMenuItem {
    std::string label;
    std::string shortcut;
    bool is_separator = false;
    bool is_checked = false;
    bool has_checkbox = false;
  };

  struct TabActionPopupMenu {
    bool visible = false;
    UI::Rect bounds{};
    std::vector<TabActionMenuItem> items;
    std::vector<UI::Rect> item_bounds;
    std::optional<std::size_t> hovered_index;
  };

  enum class SplitDropZone { None, Left, Right, Top, Bottom, Center };

  struct HoveredDiagnosticInfo {
    Language::Protocol::Diagnostic diagnostic;
    std::string line_text;
    std::string symbol_name;
    float anchor_x = 0.0F;
    float anchor_y = 0.0F;
  };

  void draw_tab_strip(const StudioWorkspaceRenderer &surface,
                      HDC device_context,
                      const UI::Editor::StudioEditorLayoutResult &layout) const;
  void
  draw_editor_header(const StudioWorkspaceRenderer &surface, HDC device_context,
                     const UI::Editor::StudioEditorLayoutResult &layout) const;
  void draw_tab_action_menu(
      const StudioWorkspaceRenderer &surface, HDC device_context,
      const UI::Editor::StudioEditorLayoutResult &layout) const;
  void draw_split_drop_overlay(
      const StudioWorkspaceRenderer &surface, HDC device_context,
      const UI::Editor::StudioEditorLayoutResult &layout) const;
  void draw_diagnostic_hover_overlay(
      const StudioWorkspaceRenderer &surface, HDC device_context,
      const UI::Editor::StudioEditorLayoutResult &layout) const;
  void
  draw_hover_tooltip(const StudioWorkspaceRenderer &surface, HDC device_context,
                     const UI::Editor::StudioEditorLayoutResult &layout) const;
  void show_tab_action_menu(const UI::Editor::StudioEditorLayoutResult &layout);
  void close_all_documents();
  void close_saved_documents();
  void draw_document(const StudioWorkspaceRenderer &surface, HDC device_context,
                     const UI::Editor::StudioEditorLayoutResult &layout) const;
  [[nodiscard]] UI::Editor::TextPosition
  position_from_point(const StudioWorkspaceRenderer &surface,
                      HDC device_context,
                      const UI::Editor::StudioEditorLayoutResult &layout,
                      float point_x, float point_y) const;
  [[nodiscard]] std::string get_active_document_uri() const;
  [[nodiscard]] std::string get_active_document_filename() const;

  UI::Editor::EditorController m_controller;
  mutable UI::Components::EditorFoldingModel m_folding;
  mutable EditorMinimap m_minimap;
  mutable EditorScrollbar m_scrollbar;
  Utility::DragDropModel m_tab_drag_drop;
  mutable std::unordered_map<std::size_t, float> m_tab_animated_offset_x;
  float m_drag_initial_tab_x = 0.0F;
  UI::Editor::CaretBlinkModel m_caret_blink;
  mutable bool m_reveal_caret_pending = true;
  bool m_focused = false;
  bool m_pointer_selecting = false;
  bool m_is_drag_selecting = false;

public:
  bool is_pointer_selecting() const noexcept {
    return m_pointer_selecting || m_is_drag_selecting;
  }
  bool is_resizing_split() const noexcept { return m_is_resizing_split; }
  bool is_tab_dragging() const noexcept {
    return m_tab_drag_drop.is_dragging();
  }

private:
  mutable std::array<UI::Rect, max_visible_tabs> m_tab_bounds{};
  mutable std::size_t m_tab_count = 0;
  std::optional<std::size_t> m_hovered_tab_index;
  std::optional<std::size_t> m_hovered_tab_close_index;
  mutable std::array<UI::Rect, 4> m_tab_action_bounds{};
  mutable std::optional<std::size_t> m_hovered_tab_action;
  mutable TabActionPopupMenu m_tab_action_menu;
  bool m_preview_editors_enabled = true;
  mutable SplitDropZone m_active_drop_zone = SplitDropZone::None;
  mutable float m_drag_cursor_x = 0.0F;
  mutable float m_drag_cursor_y = 0.0F;
  bool m_is_split = false;
  std::optional<std::size_t> m_split_document_index;
  SplitPaneFocus m_focused_pane = SplitPaneFocus::Left;
  mutable EditorMinimap m_split_minimap;
  mutable EditorScrollbar m_split_scrollbar;
  mutable UI::Components::EditorFoldingModel m_split_folding;
  mutable const UI::Editor::TextDocumentModel *m_last_folded_doc = nullptr;
  mutable std::size_t m_last_folded_line_count = 0;
  mutable const UI::Editor::TextDocumentModel *m_split_last_folded_doc =
      nullptr;
  mutable std::size_t m_split_last_folded_line_count = 0;
  float m_split_ratio = 0.5F;
  bool m_is_resizing_split = false;
  mutable bool m_hovered_split_resize = false;
  mutable UI::Rect m_split_close_btn_bounds{};
  mutable bool m_hovered_split_close = false;
  mutable std::optional<std::size_t> m_hovered_fold_line;
  mutable std::optional<std::size_t> m_hovered_gutter_line;
  bool m_hovered_tab_scrollbar = false;
  bool m_dragging_tab_scrollbar = false;
  float m_tab_scroll_drag_start_x = 0.0F;
  float m_tab_scroll_drag_initial_offset = 0.0F;
  float m_tab_scroll_offset = 0.0F;
  mutable float m_max_tab_scroll = 0.0F;
  float m_text_scroll_offset = 0.0F;
  mutable float m_max_text_scroll = 0.0F;
  mutable UI::Rect m_text_hscrollbar_bounds{};
  mutable bool m_hovered_text_hscrollbar = false;
  bool m_dragging_text_hscrollbar = false;
  float m_text_hscroll_drag_start_x = 0.0F;
  float m_text_hscroll_drag_initial_offset = 0.0F;
  mutable UI::Editor::BraceAnimationModel m_brace_animation;
  mutable UI::Editor::BraceAnimationModel m_split_brace_animation;
  mutable UI::Components::Button m_empty_state_open_btn;
  mutable UI::Components::Button m_empty_state_clone_btn;
  mutable UI::Editor::TextPosition m_last_brace_caret;
  mutable UI::Editor::TextPosition m_split_last_brace_caret;
  mutable UI::Components::CompletionPopup m_completion_popup;
  mutable UI::Components::HoverTooltip m_hover_tooltip;
  mutable std::optional<HoveredDiagnosticInfo> m_hovered_diagnostic;
  mutable UI::Components::SignatureHelpWidget m_signature_help;
  mutable std::mutex m_lsp_mutex;
  mutable std::optional<UI::Editor::TextPosition> m_hovered_token_pos;
  mutable std::string m_last_hovered_word;
  mutable float m_hover_screen_x = 0.0F;
  mutable float m_hover_screen_y = 0.0F;
  mutable std::chrono::steady_clock::time_point m_hover_start_time{};
  mutable bool m_hover_requested = false;
  mutable std::chrono::steady_clock::time_point m_last_file_check_time{};
  mutable bool m_lsp_sync_pending = false;
  mutable std::chrono::steady_clock::time_point m_last_edit_time{};

  struct CtrlHoverTokenInfo {
    std::size_t line = 0;
    std::size_t start_col = 0;
    std::size_t end_col = 0;
    std::string symbol;
  };
  mutable std::optional<CtrlHoverTokenInfo> m_ctrl_hovered_token;
  mutable float m_cached_char_width = 0.0F;

public:
  void queue_lsp_document_sync() noexcept {
    m_lsp_sync_pending = true;
    m_last_edit_time = std::chrono::steady_clock::now();
  }
  void flush_lsp_document_sync_if_needed(bool force = false);
};

} // namespace Zenvra::Platform::Win32::Components
