#pragma once

#include "Platform/X11/Components/EditorMinimap.h"
#include "Platform/X11/Components/EditorScrollbar.h"
#include "UI/Editor/CaretBlinkModel.h"
#include "UI/Editor/EditorController.h"
#include "UI/Editor/StudioEditorModel.h"

#include <X11/Xlib.h>

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace Zenvra::Platform::X11::Components
{

class StudioWorkspaceRenderer;

class TextEditor
{
public:
    [[nodiscard]] bool open_file(const std::filesystem::path& path);
    [[nodiscard]] std::size_t open_dropped_paths(
        std::span<const std::filesystem::path> dropped_paths);
    [[nodiscard]] bool create_buffer();
    [[nodiscard]] bool handle_pointer_press(
        const StudioWorkspaceRenderer& surface,
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_x,
        float point_y,
        bool extend_selection);
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
        std::ptrdiff_t line_delta) noexcept;
    [[nodiscard]] bool handle_input(
        UI::Editor::EditorInputCommand command,
        bool extend_selection);
    [[nodiscard]] bool handle_action(UI::Editor::EditorAction action);
    [[nodiscard]] std::optional<bool> handle_command(std::string_view command_id);
    [[nodiscard]] std::optional<bool> is_command_enabled(
        std::string_view command_id) const noexcept;
    [[nodiscard]] bool handle_text_input(std::string_view utf8_text);
    [[nodiscard]] bool is_focused() const noexcept;
    [[nodiscard]] bool is_scrollbar_point(
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_x,
        float point_y) const noexcept;
    [[nodiscard]] bool is_minimap_point(
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_x,
        float point_y) const noexcept;
    [[nodiscard]] bool tick_caret_blink() noexcept;
    [[nodiscard]] const UI::Editor::TextDocumentModel* get_document() const noexcept;

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

    UI::Editor::EditorController m_controller;
    mutable EditorMinimap m_minimap;
    mutable EditorScrollbar m_scrollbar;
    UI::Editor::CaretBlinkModel m_caret_blink;
    mutable bool m_reveal_caret_pending = true;
    bool m_focused = false;
    bool m_pointer_selecting = false;
    mutable std::array<UI::Rect, max_visible_tabs> m_tab_bounds{};
    mutable std::size_t m_tab_count = 0;
    std::optional<std::size_t> m_hovered_tab_index;
    std::optional<std::size_t> m_hovered_tab_close_index;
};

} // namespace Zenvra::Platform::X11::Components
