#pragma once

#include "Platform/Cocoa/Components/ActivitySidebar.h"
#include "Platform/Cocoa/Components/FooterToolbar.h"
#include "Platform/Cocoa/Components/TerminalPanel.h"
#include "Platform/Cocoa/Components/TextEditor.h"
#include "Platform/Cocoa/Components/ToolSidebar.h"
#include "UI/Editor/StudioEditorModel.h"

#include <CoreGraphics/CoreGraphics.h>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

class AntialiasedFont;

namespace Zenvra::Platform::Cocoa::Components
{

class StudioWorkspaceRenderer
{
public:
    StudioWorkspaceRenderer();
    ~StudioWorkspaceRenderer();

    StudioWorkspaceRenderer(const StudioWorkspaceRenderer&) = delete;
    StudioWorkspaceRenderer& operator=(const StudioWorkspaceRenderer&) = delete;

    [[nodiscard]] bool initialize(float dpi_scale);
    [[nodiscard]] bool open_file(const std::filesystem::path& path);
    [[nodiscard]] bool set_workspace_root(const std::filesystem::path& root);
    [[nodiscard]] std::size_t open_dropped_paths(
        std::span<const std::filesystem::path> dropped_paths);
    [[nodiscard]] bool create_buffer();
    [[nodiscard]] bool toggle_terminal();
    [[nodiscard]] bool handle_pointer_press(
        float point_x, float point_y,
        int client_width, int client_height,
        float content_top, bool extend_selection,
        int click_count, double event_time,
        std::string& command_out);
    [[nodiscard]] bool handle_pointer_move(
        float point_x, float point_y,
        int client_width, int client_height,
        float content_top) noexcept;
    [[nodiscard]] bool handle_pointer_drag(
        float point_x, float point_y,
        int client_width, int client_height,
        float content_top);
    [[nodiscard]] bool handle_pointer_release() noexcept;
    [[nodiscard]] bool handle_scroll(
        float point_x, float point_y,
        std::string& command_out,
        std::ptrdiff_t line_delta, bool horizontal,
        int client_width, int client_height,
        float content_top) noexcept;
    [[nodiscard]] bool handle_editor_input(
        UI::Editor::EditorInputCommand command, bool extend_selection);
    [[nodiscard]] bool handle_editor_action(UI::Editor::EditorAction action);
    [[nodiscard]] std::optional<bool> handle_editor_command(std::string_view command_id);
    [[nodiscard]] std::optional<bool> is_editor_command_enabled(
        std::string_view command_id) const noexcept;
    [[nodiscard]] bool handle_text_input(std::string_view utf8_text);
    [[nodiscard]] bool handle_terminal_key(Terminal::TerminalInputKey key);
    [[nodiscard]] bool handle_terminal_control(char letter);
    [[nodiscard]] bool handle_terminal_scroll(std::ptrdiff_t line_delta, bool horizontal) noexcept;
    [[nodiscard]] bool handle_tool_sidebar_scroll(
        std::ptrdiff_t line_delta,
        int client_width, int client_height,
        float content_top) noexcept;
    [[nodiscard]] bool is_editor_focused() const noexcept;
    [[nodiscard]] bool is_terminal_focused() const noexcept;
    [[nodiscard]] bool is_activity_bar_point(
        float point_x, float point_y,
        int client_width, int client_height,
        float content_top) const noexcept;
    [[nodiscard]] bool is_tab_bar_point(
        float point_x, float point_y,
        int client_width, int client_height,
        float content_top) const noexcept;
    [[nodiscard]] bool is_tab_bar_area_point(
        float point_x, float point_y,
        int client_width, int client_height,
        float content_top) const noexcept;
    [[nodiscard]] bool is_editor_point(
        float point_x, float point_y,
        int client_width, int client_height,
        float content_top) const noexcept;
    [[nodiscard]] bool is_scrollbar_point(
        float point_x, float point_y,
        int client_width, int client_height,
        float content_top) const noexcept;
    [[nodiscard]] bool is_minimap_point(
        float point_x, float point_y,
        int client_width, int client_height,
        float content_top) const noexcept;
    [[nodiscard]] bool is_fold_margin_point(
        float point_x, float point_y,
        int client_width, int client_height,
        float content_top) const noexcept;
    [[nodiscard]] bool is_terminal_point(
        float point_x, float point_y,
        int client_width, int client_height,
        float content_top) const noexcept;
    [[nodiscard]] bool is_tool_sidebar_point(
        float point_x, float point_y,
        int client_width, int client_height,
        float content_top) const noexcept;
    [[nodiscard]] bool is_terminal_resize_handle_point(
        float point_x, float point_y,
        int client_width, int client_height,
        float content_top) const noexcept;
    [[nodiscard]] bool is_terminal_resizing() const noexcept;
    [[nodiscard]] bool is_editor_interactive_point(
        float point_x, float point_y) const noexcept;
    [[nodiscard]] bool is_terminal_interactive_point(
        float point_x, float point_y,
        int client_width, int client_height,
        float content_top) const noexcept;
    [[nodiscard]] bool is_sidebar_resize_handle_point(
        float point_x, float point_y,
        int client_width, int client_height,
        float content_top) const noexcept;
    [[nodiscard]] bool is_sidebar_resizing() const noexcept;
    [[nodiscard]] bool is_empty_state_button_hovered() const noexcept;
    [[nodiscard]] bool tick_animations() noexcept;
    void shutdown();
    void render(CGContextRef context, int client_width, int client_height, float content_top) const;
    [[nodiscard]] const std::filesystem::path& get_icon_asset_root() const noexcept;
    [[nodiscard]] std::string_view get_active_buffer_name() const noexcept;

private:
    friend class CocoaChromeRenderer;
    friend class ActivitySidebar;
    friend class EditorMinimap;
    friend class EditorScrollbar;
    friend class ExplorerHeader;
    friend class FooterToolbar;
    friend class TerminalPanel;
    friend class TextEditor;
    friend class ToolSidebar;

    struct PaletteColors
    {
        CGFloat workspace_background[4]{};
        CGFloat tab_background[4]{};
        CGFloat tab_active_background[4]{};
        CGFloat sidebar_background[4]{};
        CGFloat editor_background[4]{};
        CGFloat active_line_background[4]{};
        CGFloat selection_background[4]{};
        CGFloat status_background[4]{};
        CGFloat border[4]{};
        CGFloat text_primary[4]{};
        CGFloat text_muted[4]{};
        CGFloat accent[4]{};
        CGFloat warning[4]{};
        CGFloat success[4]{};
        CGFloat hover_background[4]{};
        CGFloat indent_guide[4]{};
        CGFloat indent_guide_active[4]{};
    };

    struct PaletteText
    {
        std::string primary;
        std::string muted;
        std::string keyword;
        std::string number;
        std::string label;
        std::string type;
        std::string comment;
        std::string accent;
        std::string warning;
        std::string success;
    };

    static void color_to_rgba(const UI::Theme::Color& color, CGFloat* rgba);
    static std::string color_to_hex(const UI::Theme::Color& color);
    void fill_rectangle(CGContextRef context, const UI::Rect& rectangle, const CGFloat* rgba) const;
    void fill_rounded_rectangle(CGContextRef context, const UI::Rect& rectangle,
                                const CGFloat* rgba, float radius) const;
    void draw_rectangle(CGContextRef context, const UI::Rect& rectangle, const CGFloat* rgba) const;
    void draw_line(CGContextRef context, int from_x, int from_y, int to_x, int to_y,
                   const CGFloat* rgba) const;
    void draw_text(CGContextRef context, AntialiasedFont& font,
                   std::string_view text, float point_x, float center_y,
                   const std::string& color, const UI::Rect* clip_rect = nullptr) const;
    void push_clip(CGContextRef context, const UI::Rect& rect) const;
    void pop_clip(CGContextRef context) const;
    void draw_svg_icon(CGContextRef context, const std::string& asset_path,
                       int center_x, int center_y, int size,
                       const UI::Theme::Color& color,
                       const UI::Theme::Color& background,
                       bool preserve_source_colors = true) const;
    bool draw_png_image(CGContextRef context, const std::string& asset_path,
                        int center_x, int center_y, int size) const;
    [[nodiscard]] std::filesystem::path resolve_icon_path(
        const std::string& asset_path) const;

    float m_dpi_scale = 1.0F;
    std::unique_ptr<AntialiasedFont> m_ui_font;
    std::unique_ptr<AntialiasedFont> m_small_font;
    std::unique_ptr<AntialiasedFont> m_editor_font;
    std::unique_ptr<AntialiasedFont> m_minimap_font;
    std::unique_ptr<AntialiasedFont> m_large_font;
    std::filesystem::path m_icon_asset_root;
    UI::Editor::StudioEditorLayout m_layout_engine;
    UI::Editor::StudioEditorPalette m_palette = UI::Editor::StudioEditorPalette::jetbrains_dark();
    PaletteColors m_colors;
    PaletteText m_text;
    ActivitySidebar m_activity_sidebar;
    FooterToolbar m_footer_toolbar;
    ToolSidebar m_tool_sidebar;
    TextEditor m_text_editor;
    mutable TerminalPanel m_terminal_panel;
};

} // namespace Zenvra::Platform::Cocoa::Components
