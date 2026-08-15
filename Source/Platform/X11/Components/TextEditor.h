#pragma once

#include "Language/Protocol/LspTypes.h"
#include "Platform/X11/Components/EditorMinimap.h"
#include "Platform/X11/Components/EditorScrollbar.h"
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

#include <X11/Xlib.h>

#include <array>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Zenvra::Platform::X11::Components
{

class StudioWorkspaceRenderer;

class TextEditor
{
public:
    [[nodiscard]] bool open_file(const std::filesystem::path& path);
    [[nodiscard]] bool close_file(const std::filesystem::path& path);
    [[nodiscard]] std::size_t open_dropped_paths(
        std::span<const std::filesystem::path> dropped_paths);
    [[nodiscard]] bool create_buffer();
    [[nodiscard]] bool handle_pointer_press(
        const StudioWorkspaceRenderer& surface,
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_x,
        float point_y,
        bool extend_selection,
        int click_count,
        std::string& command_out);
    [[nodiscard]] bool is_tab_interactive_point(
        const StudioWorkspaceRenderer& surface,
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_x,
        float point_y) const noexcept;
    [[nodiscard]] bool handle_pointer_move(
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_x,
        float point_y) noexcept;
    [[nodiscard]] bool handle_pointer_drag(
        const StudioWorkspaceRenderer& surface,
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_x,
        float point_y);
    [[nodiscard]] bool handle_pointer_release() noexcept;
    [[nodiscard]] bool handle_scroll(
        const StudioWorkspaceRenderer& surface,
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_x,
        float point_y,
        std::string& command_out,
        std::ptrdiff_t line_delta,
        bool horizontal) noexcept;
    [[nodiscard]] bool handle_input(
        UI::Editor::EditorInputCommand command,
        bool extend_selection);
    [[nodiscard]] bool handle_action(UI::Editor::EditorAction action);
    [[nodiscard]] std::optional<bool> handle_command(std::string_view command_id);
    [[nodiscard]] std::optional<bool> is_command_enabled(
        std::string_view command_id) const noexcept;
    [[nodiscard]] bool handle_text_input(std::string_view utf8_text);
    [[nodiscard]] bool is_focused() const noexcept;
    [[nodiscard]] bool is_empty_state_interactive_point(
        float point_x,
        float point_y) const noexcept;
    [[nodiscard]] bool is_scrollbar_point(
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_x,
        float point_y) const noexcept;
    [[nodiscard]] bool is_minimap_point(
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_x,
        float point_y) const noexcept;
    [[nodiscard]] bool is_fold_margin_point(
        const StudioWorkspaceRenderer& surface,
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_x,
        float point_y) const noexcept;
    [[nodiscard]] bool tick_animations() noexcept;
    [[nodiscard]] const UI::Editor::TextDocumentModel* get_document() const noexcept;
    [[nodiscard]] bool is_empty_state_button_hovered() const noexcept {
        return m_empty_state_open_btn.get_state().hovered || m_empty_state_clone_btn.get_state().hovered;
    }

    void on_diagnostics_updated(const std::string& uri, std::vector<Language::Protocol::Diagnostic> diags);

    void render(
        const StudioWorkspaceRenderer& surface,
        Drawable drawable,
        const UI::Editor::StudioEditorLayoutResult& layout) const;

private:
    static constexpr std::size_t max_visible_tabs = 128;

    void draw_tab_strip(
        const StudioWorkspaceRenderer& surface,
        Drawable drawable,
        const UI::Editor::StudioEditorLayoutResult& layout) const;
    void draw_document(
        const StudioWorkspaceRenderer& surface,
        Drawable drawable,
        const UI::Editor::StudioEditorLayoutResult& layout) const;
    [[nodiscard]] UI::Editor::TextPosition position_from_point(
        const StudioWorkspaceRenderer& surface,
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_x,
        float point_y) const;
    [[nodiscard]] std::string get_active_document_uri() const;
    [[nodiscard]] std::string get_active_document_filename() const;

    UI::Editor::EditorController m_controller;
    mutable UI::Components::EditorFoldingModel m_folding;
    mutable EditorMinimap m_minimap;
    mutable EditorScrollbar m_scrollbar;
    Utility::DragDropModel m_tab_drag_drop;
    mutable std::unordered_map<const UI::Editor::TextDocumentModel*, float> m_tab_animated_x;
    mutable std::unordered_map<const UI::Editor::TextDocumentModel*, float> m_tab_target_x;
    float m_drag_initial_tab_x = 0.0F;
    mutable float m_tab_scroll_offset = 0.0F;
    mutable float m_max_tab_scroll = 0.0F;
    mutable float m_text_scroll_offset = 0.0F;
    mutable float m_max_text_scroll = 0.0F;
    UI::Editor::CaretBlinkModel m_caret_blink;
    mutable bool m_reveal_caret_pending = true;
    bool m_focused = false;
    bool m_pointer_selecting = false;

    mutable std::array<UI::Rect, max_visible_tabs> m_tab_bounds{};
    mutable std::size_t m_tab_count = 0;
    std::optional<std::size_t> m_hovered_tab_index;
    std::optional<std::size_t> m_hovered_tab_close_index;
    mutable std::optional<std::size_t> m_hovered_fold_line;
    bool m_hovered_tab_scrollbar = false;
    mutable UI::Editor::SelectionAnimationModel m_selection_animation;
    mutable UI::Editor::BraceAnimationModel m_brace_animation;
    mutable UI::Components::Button m_empty_state_open_btn;
    mutable UI::Components::Button m_empty_state_clone_btn;
    mutable UI::Editor::TextPosition m_last_brace_caret;
    mutable unsigned long m_brace_pulse_color = 0;
    mutable bool m_brace_pulse_color_ready = false;
    mutable UI::Components::CompletionPopup m_completion_popup;
    mutable UI::Components::HoverTooltip m_hover_tooltip;
    mutable UI::Components::SignatureHelpWidget m_signature_help;
    mutable std::mutex m_lsp_mutex;
};

} // namespace Zenvra::Platform::X11::Components

