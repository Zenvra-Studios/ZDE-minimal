#pragma once

#include "Platform/Win32/Components/ActivitySidebar.h"
#include "Platform/Win32/Components/FooterToolbar.h"
#include "Platform/Win32/Components/ShaderSandboxPanel.h"
#include "Platform/Win32/Components/TerminalPanel.h"
#include "Platform/Win32/Components/TextEditor.h"
#include "Platform/Win32/Components/ToolSidebar.h"
#include "Platform/Win32/Event/ScrollEvent.h"
#include "Platform/Win32/Components/Win32PromptDialog.h"
#include "UI/Components/PromptModal.h"
#include "UI/Components/AddNewItemDialog.h"
#include "UI/Editor/StudioEditorModel.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class AntialiasedFont;

namespace Zenvra::Platform::Win32
{
class Win32Window;
}

namespace Zenvra::Platform::Win32::Components
{

class StudioWorkspaceRenderer
{
public:
    StudioWorkspaceRenderer();
    ~StudioWorkspaceRenderer();

    StudioWorkspaceRenderer(const StudioWorkspaceRenderer&) = delete;
    StudioWorkspaceRenderer& operator=(const StudioWorkspaceRenderer&) = delete;

    [[nodiscard]] bool initialize(UINT dpi);
    [[nodiscard]] bool open_file(const std::filesystem::path& path);
    [[nodiscard]] bool set_workspace_root(const std::filesystem::path& root);
    [[nodiscard]] bool close_project();
    [[nodiscard]] std::size_t open_dropped_paths(
        std::span<const std::filesystem::path> dropped_paths);
    [[nodiscard]] bool create_buffer();
    [[nodiscard]] bool handle_pointer_press(
        HDC device_context,
        float point_x,
        float point_y,
        int client_width,
        int client_height,
        float content_top,
        bool extend_selection,
        std::string& command_out);
    [[nodiscard]] bool handle_double_click(
        float point_x,
        float point_y,
        int client_width,
        int client_height,
        float content_top) noexcept;
    [[nodiscard]] bool handle_pointer_move(
        float point_x,
        float point_y,
        int client_width,
        int client_height,
        float content_top) noexcept;
    [[nodiscard]] bool handle_pointer_drag(
        HDC device_context,
        float point_x,
        float point_y,
        int client_width,
        int client_height,
        float content_top);
    [[nodiscard]] bool handle_pointer_release() noexcept;
    [[nodiscard]] bool handle_scroll(
        const Event::ScrollEvent& event,
        int client_width,
        int client_height,
        float content_top) noexcept;
    [[nodiscard]] bool handle_editor_input(
        UI::Editor::EditorInputCommand command,
        bool extend_selection);
    [[nodiscard]] bool handle_editor_action(UI::Editor::EditorAction action);
    [[nodiscard]] std::optional<bool> handle_editor_command(std::string_view command_id);
    [[nodiscard]] std::optional<bool> is_editor_command_enabled(
        std::string_view command_id) const noexcept;
    [[nodiscard]] bool handle_text_input(std::string_view utf8_text);
    [[nodiscard]] bool trigger_editor_autocomplete();
    [[nodiscard]] bool handle_terminal_key(Terminal::TerminalInputKey key);
    [[nodiscard]] bool handle_terminal_control(char letter);
    [[nodiscard]] bool handle_terminal_scroll(const Event::ScrollEvent& event) noexcept;
    [[nodiscard]] bool handle_tool_sidebar_scroll(
        std::ptrdiff_t line_delta,
        int client_width,
        int client_height,
        float content_top) noexcept;
    [[nodiscard]] bool open_file_at_location(const std::filesystem::path& path, std::size_t line, std::size_t column);
    [[nodiscard]] bool is_search_focused() const noexcept;
    [[nodiscard]] bool handle_search_char(char32_t codepoint);
    [[nodiscard]] bool handle_search_key(int vkey, bool ctrl, bool shift, bool alt);
    [[nodiscard]] bool is_editor_focused() const noexcept;
    [[nodiscard]] bool is_terminal_focused() const noexcept;
    [[nodiscard]] bool is_activity_bar_point(
        float point_x,
        float point_y,
        int client_width,
        int client_height,
        float content_top) const noexcept;
    [[nodiscard]] bool is_tab_bar_point(
        float point_x,
        float point_y,
        int client_width,
        int client_height,
        float content_top) const noexcept;
    [[nodiscard]] bool is_editor_point(
        float point_x,
        float point_y,
        int client_width,
        int client_height,
        float content_top) const noexcept;
    [[nodiscard]] bool is_scrollbar_point(
        float point_x,
        float point_y,
        int client_width,
        int client_height,
        float content_top) const noexcept;
    [[nodiscard]] bool is_minimap_point(
        float point_x,
        float point_y,
        int client_width,
        int client_height,
        float content_top) const noexcept;
    [[nodiscard]] bool is_terminal_point(
        float point_x,
        float point_y,
        int client_width,
        int client_height,
        float content_top) const noexcept;
    [[nodiscard]] bool is_tool_sidebar_point(
        float point_x,
        float point_y,
        int client_width,
        int client_height,
        float content_top) const noexcept;
    [[nodiscard]] bool is_tool_sidebar_interactive_point(
        float point_x,
        float point_y,
        int client_width,
        int client_height,
        float content_top) const noexcept;
    [[nodiscard]] bool is_tool_sidebar_text_input_point(
        float point_x,
        float point_y,
        int client_width,
        int client_height,
        float content_top) const noexcept;
    [[nodiscard]] bool is_editor_interactive_point(
        float point_x,
        float point_y) const noexcept;
    [[nodiscard]] bool is_terminal_interactive_point(
        float point_x,
        float point_y,
        int client_width,
        int client_height,
        float content_top) const noexcept;
    [[nodiscard]] bool is_fold_margin_point(
        float point_x,
        float point_y,
        int client_width,
        int client_height,
        float content_top) const noexcept;
    [[nodiscard]] bool is_terminal_resize_handle_point(
        float point_x,
        float point_y,
        int client_width,
        int client_height,
        float content_top) const noexcept;
    [[nodiscard]] bool is_terminal_resizing() const noexcept;
    [[nodiscard]] bool is_sidebar_resize_handle_point(
        float point_x,
        float point_y,
        int client_width,
        int client_height,
        float content_top) const noexcept;
    [[nodiscard]] bool is_sidebar_resizing() const noexcept;
    [[nodiscard]] bool is_sidebar_dragging_item() const noexcept;
    [[nodiscard]] bool is_sidebar_dragging_scrollbar() const noexcept;
    [[nodiscard]] bool is_shader_sandbox_point(
        float point_x,
        float point_y,
        int client_width,
        int client_height,
        float content_top) const noexcept;
    [[nodiscard]] bool is_shader_sandbox_resize_handle(
        float point_x,
        float point_y,
        int client_width,
        int client_height,
        float content_top) const noexcept;
    [[nodiscard]] bool is_shader_sandbox_resizing() const noexcept;
    [[nodiscard]] bool is_editor_split_resize_handle(
        float point_x,
        float point_y,
        int client_width,
        int client_height,
        float content_top) const noexcept;
    [[nodiscard]] bool is_editor_split_resizing() const noexcept;
    [[nodiscard]] bool toggle_shader_sandbox() noexcept;
    [[nodiscard]] bool is_shader_sandbox_visible() const noexcept;
    void sync_shader_sandbox() const;
    [[nodiscard]] bool toggle_terminal() noexcept;
    [[nodiscard]] bool is_terminal_visible() const noexcept;
    void reset_layout() noexcept;
    [[nodiscard]] UI::Editor::StudioEditorLayoutResult calculate_layout(
        int client_width,
        int client_height,
        float content_top) const noexcept;
    [[nodiscard]] bool tick_animations() noexcept;
    void shutdown();
    void render(
        HDC device_context,
        int client_width,
        int client_height,
        float content_top) const;
    void draw_svg_icon(
        HDC device_context,
        std::string_view asset_name,
        int center_x,
        int center_y,
        int size,
        const UI::Theme::Color& color,
        const UI::Theme::Color& background,
        bool preserve_source_colors = true) const;
    void draw_png_icon(
        HDC device_context,
        const std::string& asset_path,
        int center_x,
        int center_y,
        int max_size,
        const UI::Theme::Color& background) const;

    [[nodiscard]] TextEditor& get_text_editor() noexcept { return m_text_editor; }
    [[nodiscard]] const TextEditor& get_text_editor() const noexcept { return m_text_editor; }
    [[nodiscard]] ToolSidebar& get_tool_sidebar() noexcept { return m_tool_sidebar; }
    [[nodiscard]] const ToolSidebar& get_tool_sidebar() const noexcept { return m_tool_sidebar; }
    [[nodiscard]] TerminalPanel& get_terminal_panel() noexcept { return m_terminal_panel; }
    [[nodiscard]] const TerminalPanel& get_terminal_panel() const noexcept { return m_terminal_panel; }

private:
    friend class ActivitySidebar;
    friend class EditorMinimap;
    friend class EditorScrollbar;
    friend class ExplorerHeader;
    friend class FooterToolbar;
    friend class ShaderSandboxPanel;
    friend class TerminalPanel;
    friend class TextEditor;
    friend class ToolSidebar;
    friend class ::Zenvra::Platform::Win32::Win32Window;

    void fill_rectangle(HDC device_context, const UI::Rect& rectangle, const UI::Theme::Color& color) const;
    void fill_rounded_rectangle(HDC device_context, const UI::Rect& rectangle, const UI::Theme::Color& color, float radius) const;
    void draw_rectangle(HDC device_context, const UI::Rect& rectangle, const UI::Theme::Color& color) const;
    void draw_line(
        HDC device_context,
        int from_x,
        int from_y,
        int to_x,
        int to_y,
        const UI::Theme::Color& color) const;
    void draw_text(
        HDC device_context,
        AntialiasedFont& font,
        std::string_view text,
        float point_x,
        float center_y,
        const UI::Theme::Color& color) const;
    void draw_scaled_text(
        HDC device_context,
        AntialiasedFont& font,
        std::string_view text,
        float point_x,
        float center_y,
        float scale,
        const UI::Theme::Color& color) const;
    [[nodiscard]] int get_text_width(
        HDC device_context,
        AntialiasedFont& font,
        std::string_view text) const;

    [[nodiscard]] std::optional<std::filesystem::path> handle_right_click(
        float point_x,
        float point_y,
        int client_width,
        int client_height,
        float content_top);
    [[nodiscard]] bool is_prompt_modal_visible() const noexcept;
    [[nodiscard]] UI::Components::PromptModal& get_prompt_modal() const noexcept { return m_prompt_modal; }
    [[nodiscard]] Win32PromptDialog& get_prompt_dialog() const noexcept { return m_prompt_dialog; }
    void render_prompt_modal(HDC device_context, int client_width, int client_height) const;

    void set_window_handle(HWND handle) noexcept { m_window_handle = handle; }
    [[nodiscard]] HWND get_window_handle() const noexcept { return m_window_handle; }

    [[nodiscard]] bool is_add_item_dialog_visible() const noexcept { return m_add_item_dialog.is_visible(); }
    [[nodiscard]] UI::Components::AddNewItemDialog& get_add_item_dialog() const noexcept { return m_add_item_dialog; }
    void render_add_item_dialog(HDC device_context, int client_width, int client_height, const UI::Theme::StudioTheme& theme) const;

    HWND m_window_handle = nullptr;

    UINT m_dpi = 96;
    float m_dpi_scale = 1.0F;
    std::unique_ptr<AntialiasedFont> m_ui_font;
    std::unique_ptr<AntialiasedFont> m_small_font;
    std::unique_ptr<AntialiasedFont> m_editor_font;
    std::unique_ptr<AntialiasedFont> m_minimap_font;
    std::unique_ptr<AntialiasedFont> m_large_font;
    std::filesystem::path m_icon_asset_root;
    UI::Editor::StudioEditorLayout m_layout_engine;
    UI::Editor::StudioEditorPalette m_palette = UI::Editor::StudioEditorPalette::dark();
    ActivitySidebar m_activity_sidebar;
    FooterToolbar m_footer_toolbar;
    ToolSidebar m_tool_sidebar;
    TextEditor m_text_editor;
    mutable TerminalPanel m_terminal_panel;
    mutable ShaderSandboxPanel m_shader_sandbox_panel;
    mutable UI::Components::PromptModal m_prompt_modal;
    mutable Win32PromptDialog m_prompt_dialog;
    mutable UI::Components::AddNewItemDialog m_add_item_dialog;
    mutable std::unordered_map<std::string, std::vector<std::uint32_t>> m_svg_cache;
};

} // namespace Zenvra::Platform::Win32::Components
