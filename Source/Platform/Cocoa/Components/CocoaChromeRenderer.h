#pragma once

#include "Platform/IPlatformWindow.h"
#include "Platform/Cocoa/Components/StudioWorkspaceRenderer.h"
#include "UI/Chrome/WindowChromeLayout.h"
#include "UI/Components/MenuModel.h"
#include "UI/Theme/StudioTheme.h"

#include <CoreGraphics/CoreGraphics.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

class AntialiasedFont;

namespace Zenvra::Platform::Cocoa::Components
{

inline constexpr std::size_t max_popup_menu_items = 16;

struct PopupMenuGeometry
{
    UI::Rect bounds;
    std::array<UI::Rect, max_popup_menu_items> item_bounds{};
    std::size_t item_count = 0;
};

struct PopupMenuItem
{
    std::string text;
    std::string command_id;
    bool separator = false;
    bool enabled = true;
    bool checked = false;
    std::string shortcut;
};

struct OverflowMenuGeometry
{
    UI::Rect bounds;
    std::array<UI::Rect, UI::Chrome::window_menu_count> item_bounds{};
    std::size_t first_menu_index = UI::Chrome::window_menu_count;
    std::size_t item_count = 0;
};

struct ChromeInteractionState
{
    UI::Chrome::WindowControl hovered_control = UI::Chrome::WindowControl::NoControl;
    UI::Chrome::WindowControl pressed_control = UI::Chrome::WindowControl::NoControl;
    std::optional<std::size_t> hovered_menu_index;
    std::optional<std::size_t> open_menu_index;
    std::optional<std::size_t> hovered_popup_item_index;
    std::optional<std::size_t> hovered_overflow_menu_index;
    bool overflow_menu_hovered = false;
    bool overflow_menu_open = false;
    bool command_center_hovered = false;
    bool run_button_hovered = false;
    bool debug_button_hovered = false;
    bool ellipsis_button_hovered = false;
    bool compiler_button_hovered = false;
    bool platform_button_hovered = false;
    bool binary_button_hovered = false;
    bool mode_button_hovered = false;
    bool build_button_hovered = false;
    bool gear_button_hovered = false;
    bool maximized = false;
    bool focused = false;

    [[nodiscard]] bool operator==(const ChromeInteractionState&) const noexcept = default;
};

class CocoaChromeRenderer
{
public:
    CocoaChromeRenderer();
    ~CocoaChromeRenderer();

    CocoaChromeRenderer(const CocoaChromeRenderer&) = delete;
    CocoaChromeRenderer& operator=(const CocoaChromeRenderer&) = delete;

    [[nodiscard]] bool initialize(
        float dpi_scale,
        const UI::Theme::StudioTheme& theme);
    void shutdown();
    [[nodiscard]] const std::filesystem::path& get_icon_asset_root() const noexcept;

    [[nodiscard]] bool open_workspace_file(const std::filesystem::path& path);
    [[nodiscard]] bool set_workspace_root(const std::filesystem::path& root);
    [[nodiscard]] std::size_t open_dropped_paths(
        std::span<const std::filesystem::path> dropped_paths);
    [[nodiscard]] bool create_workspace_buffer();
    [[nodiscard]] bool toggle_terminal();

    // Workspace event forwarding
    [[nodiscard]] bool handle_workspace_pointer_press(
        float point_x, float point_y,
        int client_width, int client_height,
        float content_top, bool extend_selection,
        int click_count, double event_time,
        std::string& command_out);
    [[nodiscard]] bool handle_workspace_pointer_move(
        float point_x, float point_y,
        int client_width, int client_height,
        float content_top) noexcept;
    [[nodiscard]] bool handle_workspace_pointer_drag(
        float point_x, float point_y,
        int client_width, int client_height,
        float content_top);
    [[nodiscard]] bool handle_workspace_pointer_release() noexcept;
    [[nodiscard]] bool handle_workspace_scroll(
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
    [[nodiscard]] bool is_activity_bar_point(float px, float py, int cw, int ch, float ct) const noexcept;
    [[nodiscard]] bool is_tab_bar_point(float px, float py, int cw, int ch, float ct) const noexcept;
    [[nodiscard]] bool is_tab_bar_area_point(float px, float py, int cw, int ch, float ct) const noexcept;
    [[nodiscard]] bool is_editor_point(float px, float py, int cw, int ch, float ct) const noexcept;
    [[nodiscard]] bool is_scrollbar_point(float px, float py, int cw, int ch, float ct) const noexcept;
    [[nodiscard]] bool is_minimap_point(float px, float py, int cw, int ch, float ct) const noexcept;
    [[nodiscard]] bool is_fold_margin_point(float px, float py, int cw, int ch, float ct) const noexcept;
    [[nodiscard]] bool is_terminal_point(float px, float py, int cw, int ch, float ct) const noexcept;
    [[nodiscard]] bool is_tool_sidebar_point(float px, float py, int cw, int ch, float ct) const noexcept;
    [[nodiscard]] bool is_terminal_resize_handle_point(float px, float py, int cw, int ch, float ct) const noexcept;
    [[nodiscard]] bool is_terminal_resizing() const noexcept;
    [[nodiscard]] bool is_editor_interactive_point(float px, float py) const noexcept;
    [[nodiscard]] bool is_terminal_interactive_point(float px, float py, int cw, int ch, float ct) const noexcept;
    [[nodiscard]] bool is_sidebar_resize_handle_point(float px, float py, int cw, int ch, float ct) const noexcept;
    [[nodiscard]] bool is_sidebar_resizing() const noexcept;
    [[nodiscard]] bool is_empty_state_button_hovered() const noexcept;
    [[nodiscard]] bool tick_animations() noexcept;

    // Updates the chrome interaction state (hovered menus/buttons/combos)
    // for the given pointer position. Returns true when the state changed.
    [[nodiscard]] bool update_chrome_hover_state(
        float point_x, float point_y,
        const UI::Chrome::WindowChromeLayoutResult& chrome_layout,
        ChromeInteractionState& state) const noexcept;

    void render(
        CGContextRef context,
        int client_width, int client_height,
        const UI::Chrome::WindowChromeLayoutResult& chrome_layout,
        const ChromeInteractionState& interaction_state,
        const CommandStateQueryCallback& command_state_query_callback) const;

    [[nodiscard]] OverflowMenuGeometry calculate_overflow_menu_geometry(
        const UI::Chrome::WindowChromeLayoutResult& chrome_layout) const noexcept;
    [[nodiscard]] PopupMenuGeometry calculate_popup_geometry(
        const UI::Chrome::WindowChromeLayoutResult& chrome_layout,
        std::size_t menu_index,
        bool opened_from_overflow = false) const noexcept;

private:
    struct ThemeColors
    {
        CGFloat window_background[4]{};
        CGFloat titlebar_background[4]{};
        CGFloat titlebar_border[4]{};
        CGFloat text_primary[4]{};
        CGFloat text_secondary[4]{};
        CGFloat hover[4]{};
        CGFloat pressed[4]{};
        CGFloat accent[4]{};
        CGFloat command_center_background[4]{};
        CGFloat command_center_border[4]{};
        CGFloat close_hover[4]{};
        CGFloat popup_background[4]{};
        CGFloat popup_border[4]{};
    };

    struct ThemeTextColors
    {
        std::string primary;
        std::string secondary;
        std::string white;
    };

    static void color_to_rgba(const UI::Theme::Color& color, CGFloat* rgba);
    void fill_rectangle(CGContextRef context, const UI::Rect& rectangle,
                        const CGFloat* rgba, int radius = 0) const;
    void draw_rectangle(CGContextRef context, const UI::Rect& rectangle,
                        const CGFloat* rgba, int radius = 0) const;
    void draw_line(CGContextRef context, int x1, int y1, int x2, int y2,
                   const CGFloat* rgba) const;
    void draw_centered_text(CGContextRef context, std::string_view text,
                            const UI::Rect& rectangle,
                            const std::string& color) const;
    void draw_text(CGContextRef context, std::string_view text,
                   const UI::Rect& rectangle, float left_padding,
                   const std::string& color) const;
    void draw_popup_menu(CGContextRef context,
                         const UI::Chrome::WindowChromeLayoutResult& chrome_layout,
                         const ChromeInteractionState& interaction_state,
                         const CommandStateQueryCallback& callback) const;
    void draw_overflow_menu(CGContextRef context,
                            const UI::Chrome::WindowChromeLayoutResult& chrome_layout,
                            const ChromeInteractionState& interaction_state) const;

    float m_dpi_scale = 1.0F;
    std::unique_ptr<AntialiasedFont> m_font;
    ThemeColors m_colors;
    ThemeTextColors m_text_colors;
    UI::Theme::Color m_titlebar_background_color{};
    UI::Theme::Color m_hover_color{};
    UI::Theme::StudioTheme m_theme{};
    StudioWorkspaceRenderer m_workspace_renderer;
};

} // namespace Zenvra::Platform::Cocoa::Components
