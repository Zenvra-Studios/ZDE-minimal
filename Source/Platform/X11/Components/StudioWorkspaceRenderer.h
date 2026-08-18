#pragma once

#include "Platform/X11/Components/ActivitySidebar.h"
#include "Platform/X11/Components/FooterToolbar.h"
#include "Platform/X11/Components/ShaderSandboxPanel.h"
#include "Platform/X11/Components/TerminalPanel.h"
#include "Platform/X11/Components/TextEditor.h"
#include "Platform/X11/Components/ToolSidebar.h"
#include "UI/Components/PromptModal.h"
#include "UI/Editor/StudioEditorModel.h"

#include <X11/Xlib.h>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

class AntialiasedFont;

namespace Zenvra::Platform::X11 {
class X11Window;
}

namespace Zenvra::Platform::X11::Components
{

class StudioWorkspaceRenderer
{
public:
    StudioWorkspaceRenderer();
    ~StudioWorkspaceRenderer();

    StudioWorkspaceRenderer(const StudioWorkspaceRenderer&) = delete;
    StudioWorkspaceRenderer& operator=(const StudioWorkspaceRenderer&) = delete;

    [[nodiscard]] bool initialize(Display* display, int screen, float dpi_scale);
    [[nodiscard]] bool open_file(const std::filesystem::path& path);
    [[nodiscard]] bool set_workspace_root(const std::filesystem::path& root);
    [[nodiscard]] bool close_project();
    [[nodiscard]] std::size_t open_dropped_paths(
        std::span<const std::filesystem::path> dropped_paths);
    [[nodiscard]] bool create_buffer();
    [[nodiscard]] bool handle_pointer_press(
        float point_x,
        float point_y,
        int client_width,
        int client_height,
        float content_top,
        bool extend_selection,
        int click_count,
        Time event_time,
        std::string& command_out);
    [[nodiscard]] bool handle_pointer_move(
        float point_x,
        float point_y,
        int client_width,
        int client_height,
        float content_top) noexcept;
    [[nodiscard]] bool handle_pointer_drag(
        float point_x,
        float point_y,
        int client_width,
        int client_height,
        float content_top);
    [[nodiscard]] bool handle_pointer_release() noexcept;
    [[nodiscard]] bool handle_scroll(
        float point_x,
        float point_y,
        std::string& command_out,
        std::ptrdiff_t line_delta,
        bool horizontal,
        int client_width,
        int client_height,
        float content_top) noexcept;
    [[nodiscard]] bool handle_editor_input(
        UI::Editor::EditorInputCommand command,
        bool extend_selection);
    [[nodiscard]] bool handle_editor_action(UI::Editor::EditorAction action);
    [[nodiscard]] std::optional<bool> handle_editor_command(std::string_view command_id);
    [[nodiscard]] std::optional<bool> is_editor_command_enabled(std::string_view command_id) const noexcept;
    void reset_layout() noexcept;
    [[nodiscard]] bool handle_text_input(std::string_view utf8_text);
    [[nodiscard]] bool handle_terminal_key(Terminal::TerminalInputKey key);
    [[nodiscard]] bool handle_terminal_control(char letter);
    [[nodiscard]] bool handle_terminal_scroll(
        float point_x,
        float point_y,
        std::ptrdiff_t line_delta,
        bool horizontal,
        int client_width,
        int client_height,
        float content_top) noexcept;
    [[nodiscard]] bool handle_terminal_scroll(std::ptrdiff_t line_delta, bool horizontal) noexcept;
    [[nodiscard]] bool handle_tool_sidebar_scroll(
        std::ptrdiff_t line_delta,
        int client_width,
        int client_height,
        float content_top) noexcept;
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
    [[nodiscard]] bool is_tab_bar_area_point(
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
    [[nodiscard]] bool is_fold_margin_point(
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
    [[nodiscard]] std::optional<std::filesystem::path> handle_right_click(
        float point_x,
        float point_y,
        int client_width,
        int client_height,
        float content_top);
    [[nodiscard]] UI::Components::PromptModal& get_prompt_modal() const noexcept { return m_prompt_modal; }
    [[nodiscard]] bool is_prompt_modal_visible() const noexcept;
    void render_prompt_modal(Drawable drawable, int client_width, int client_height) const;
    [[nodiscard]] ToolSidebar& get_tool_sidebar() noexcept { return m_tool_sidebar; }
    [[nodiscard]] const ToolSidebar& get_tool_sidebar() const noexcept { return m_tool_sidebar; }
    [[nodiscard]] TerminalPanel& get_terminal_panel() noexcept { return m_terminal_panel; }
    [[nodiscard]] const TerminalPanel& get_terminal_panel() const noexcept { return m_terminal_panel; }
    [[nodiscard]] TextEditor& get_text_editor() noexcept { return m_text_editor; }
    [[nodiscard]] const TextEditor& get_text_editor() const noexcept { return m_text_editor; }
    [[nodiscard]] bool is_terminal_resize_handle_point(
        float point_x,
        float point_y,
        int client_width,
        int client_height,
        float content_top) const noexcept;
    [[nodiscard]] bool is_terminal_resizing() const noexcept;
    [[nodiscard]] bool is_editor_interactive_point(
        float point_x,
        float point_y) const noexcept;
    [[nodiscard]] bool is_terminal_interactive_point(
        float point_x,
        float point_y,
        int client_width,
        int client_height,
        float content_top) const noexcept;
    [[nodiscard]] bool is_sidebar_resize_handle_point(
        float point_x,
        float point_y,
        int client_width,
        int client_height,
        float content_top) const noexcept;
    [[nodiscard]] bool is_sidebar_resizing() const noexcept;
    [[nodiscard]] bool is_sidebar_dragging_item() const noexcept;
    [[nodiscard]] bool is_editor_split_resize_handle(
        float point_x,
        float point_y,
        int client_width,
        int client_height,
        float content_top) const noexcept;
    [[nodiscard]] bool is_editor_split_resizing() const noexcept;
    [[nodiscard]] bool is_shader_panel_point(
        float point_x,
        float point_y,
        int client_width,
        int client_height,
        float content_top) const noexcept;
    [[nodiscard]] bool is_shader_splitter_point(
        float point_x,
        float point_y,
        int client_width,
        int client_height,
        float content_top) const noexcept;
    [[nodiscard]] bool is_shader_panel_resizing() const noexcept;
    [[nodiscard]] bool toggle_shader_panel() noexcept;
    [[nodiscard]] bool is_shader_panel_visible() const noexcept;
    void sync_shader_sandbox() const;
    [[nodiscard]] bool is_empty_state_button_hovered() const noexcept;
    [[nodiscard]] bool tick_animations() noexcept;
    void shutdown();
    void render(Drawable drawable, int client_width, int client_height, float content_top) const;
    [[nodiscard]] const std::filesystem::path& get_icon_asset_root() const noexcept;

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
    friend class X11ChromeRenderer;
    friend class ::Zenvra::Platform::X11::X11Window;

    struct PalettePixels
    {
        unsigned long workspace_background = 0;
        unsigned long tab_background = 0;
        unsigned long tab_active_background = 0;
        unsigned long sidebar_background = 0;
        unsigned long editor_background = 0;
        unsigned long active_line_background = 0;
        unsigned long selection_background = 0;
        unsigned long status_background = 0;
        unsigned long border = 0;
        unsigned long text_primary = 0;
        unsigned long text_muted = 0;
        unsigned long accent = 0;
        unsigned long warning = 0;
        unsigned long success = 0;
        unsigned long hover_background = 0;
        unsigned long indent_guide = 0;
        unsigned long indent_guide_active = 0;
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

    [[nodiscard]] unsigned long allocate_color(const UI::Theme::Color& color) const;
    void fill_rectangle(Drawable drawable, const UI::Rect& rectangle, unsigned long color) const;
    void fill_rounded_rectangle(
        Drawable drawable,
        const UI::Rect& rectangle,
        unsigned long color,
        float radius,
        std::optional<unsigned long> bg_color = std::nullopt) const;
    void draw_rectangle(Drawable drawable, const UI::Rect& rectangle, unsigned long color) const;
    void draw_line(Drawable drawable, int from_x, int from_y, int to_x, int to_y, unsigned long color) const;
    void draw_text(
        Drawable drawable,
        AntialiasedFont& font,
        std::string_view text,
        float point_x,
        float center_y,
        const std::string& color,
        const UI::Rect* clip_rect = nullptr) const;
    void draw_text(
        Drawable drawable,
        AntialiasedFont& font,
        std::string_view text,
        float point_x,
        float center_y,
        const UI::Theme::Color& color,
        const UI::Rect* clip_rect = nullptr) const;
    [[nodiscard]] int get_text_width(AntialiasedFont& font, std::string_view text) const;
    void push_clip(const UI::Rect& rect) const;
    void pop_clip() const;
    void draw_svg_icon(
        Drawable drawable,
        const std::string& asset_path,
        int center_x,
        int center_y,
        int size,
        const UI::Theme::Color& color,
        const UI::Theme::Color& background,
        bool preserve_source_colors = true) const;
    void draw_png_icon(
        Drawable drawable,
        const std::string& asset_path,
        int center_x,
        int center_y,
        int max_size,
        const UI::Theme::Color& background) const;
    [[nodiscard]] bool draw_ico_icon(
        Drawable drawable,
        const std::string& asset_path,
        int center_x,
        int center_y,
        int max_size,
        const UI::Theme::Color& background) const;

    [[nodiscard]] UI::Editor::StudioEditorLayoutResult calculate_layout(
        int client_width,
        int client_height,
        float content_top) const noexcept;

    Display* m_display = nullptr;
    int m_screen = 0;
    float m_dpi_scale = 1.0F;
    GC m_graphics_context = nullptr;
    std::unique_ptr<AntialiasedFont> m_ui_font;
    std::unique_ptr<AntialiasedFont> m_small_font;
    std::unique_ptr<AntialiasedFont> m_editor_font;
    std::unique_ptr<AntialiasedFont> m_minimap_font;
    std::unique_ptr<AntialiasedFont> m_large_font;
    std::filesystem::path m_icon_asset_root;
    UI::Editor::StudioEditorLayout m_layout_engine;
    UI::Editor::StudioEditorPalette m_palette = UI::Editor::StudioEditorPalette::jetbrains_dark();
    PalettePixels m_pixels;
    PaletteText m_text;
    ActivitySidebar m_activity_sidebar;
    FooterToolbar m_footer_toolbar;
    ToolSidebar m_tool_sidebar;
    TextEditor m_text_editor;
    mutable TerminalPanel m_terminal_panel;
    mutable ShaderSandboxPanel m_shader_sandbox_panel;
    mutable UI::Components::PromptModal m_prompt_modal;
    static constexpr std::size_t max_image_cache_size = 64;
    void store_cached_image(const std::string& key, XImage* image) const;
    mutable std::unordered_map<std::string, XImage*> m_svg_cache;
};

} // namespace Zenvra::Platform::X11::Components
