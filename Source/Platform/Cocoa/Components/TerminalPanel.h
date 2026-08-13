#pragma once

#include "Terminal/TerminalPanelModel.h"
#include "Terminal/TerminalResizeModel.h"
#include "UI/Editor/CaretBlinkModel.h"
#include "UI/Editor/StudioEditorModel.h"

#include <CoreGraphics/CoreGraphics.h>

#include <cstddef>
#include <filesystem>
#include <string_view>
#include <unordered_map>

namespace Zenvra::Platform::Cocoa::Components
{

class StudioWorkspaceRenderer;

class TerminalPanel
{
public:
    [[nodiscard]] bool toggle();
    [[nodiscard]] bool handle_pointer_press(
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_x, float point_y, double event_time);
    [[nodiscard]] bool handle_pointer_move(
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_x, float point_y) noexcept;
    [[nodiscard]] bool handle_pointer_drag(
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_y) noexcept;
    [[nodiscard]] bool handle_pointer_release() noexcept;
    [[nodiscard]] bool handle_text_input(std::string_view text);
    [[nodiscard]] bool handle_key(Terminal::TerminalInputKey key);
    [[nodiscard]] bool handle_control(char letter);
    [[nodiscard]] bool handle_scroll(std::ptrdiff_t line_delta, bool horizontal) noexcept;
    [[nodiscard]] bool poll();
    [[nodiscard]] bool tick_animations() noexcept;
    void shutdown() noexcept;

    [[nodiscard]] bool is_visible() const noexcept;
    [[nodiscard]] bool is_focused() const noexcept;
    [[nodiscard]] bool is_resizing() const noexcept;
    [[nodiscard]] bool is_maximized() const noexcept;
    [[nodiscard]] float get_height() const noexcept;
    void set_focused(bool focused) noexcept;
    void set_working_directory(const std::filesystem::path& directory) noexcept;
    [[nodiscard]] bool contains(
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_x, float point_y) const noexcept;
    [[nodiscard]] bool is_resize_handle_point(
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_x, float point_y) const noexcept;
    [[nodiscard]] bool is_interactive_point(
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_x, float point_y) const noexcept;

    void render(
        const StudioWorkspaceRenderer& surface,
        CGContextRef context,
        const UI::Editor::StudioEditorLayoutResult& layout);

private:
    [[nodiscard]] UI::Rect session_tab_bounds(
        const UI::Editor::StudioEditorLayoutResult& layout,
        std::size_t index) const noexcept;
    [[nodiscard]] UI::Rect add_button_bounds(
        const UI::Editor::StudioEditorLayoutResult& layout) const noexcept;
    [[nodiscard]] UI::Rect close_button_bounds(
        const UI::Editor::StudioEditorLayoutResult& layout) const noexcept;
    [[nodiscard]] UI::Rect resize_handle_bounds(
        const UI::Editor::StudioEditorLayoutResult& layout) const noexcept;

    Terminal::TerminalPanelModel m_model;
    Terminal::TerminalResizeModel m_resize_model;
    mutable UI::Editor::CaretBlinkModel m_cursor_blink;
    std::filesystem::path m_working_directory;
    bool m_force_horizontal_scroll_to_cursor = false;
    std::size_t m_last_total_rows = 0;
    std::size_t m_last_visible_rows = 0;
    std::size_t m_horizontal_scroll_offset = 0;
    double m_last_resize_click_time = 0.0;
    float m_last_resize_click_x = 0.0F;
    float m_last_resize_click_y = 0.0F;
    mutable std::unordered_map<std::size_t, float> m_tab_animated_x;
    mutable std::unordered_map<std::size_t, float> m_tab_target_x;
};

} // namespace Zenvra::Platform::Cocoa::Components
