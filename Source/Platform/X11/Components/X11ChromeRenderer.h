#pragma once

#include "Platform/IPlatformWindow.h"
#include "Platform/X11/Components/StudioWorkspaceRenderer.h"
#include "UI/Chrome/WindowChromeLayout.h"
#include "UI/Components/MenuModel.h"
#include "UI/Theme/StudioTheme.h"

#include <X11/Xlib.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

class AntialiasedFont;

namespace Zenvra::Platform::X11::Components
{

inline constexpr std::size_t max_popup_menu_items = 16;

struct PopupMenuGeometry
{
    UI::Rect bounds;
    std::array<UI::Rect, max_popup_menu_items> item_bounds{};
    std::size_t item_count = 0;
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
};

class X11ChromeRenderer
{
public:
    X11ChromeRenderer();
    ~X11ChromeRenderer();

    X11ChromeRenderer(const X11ChromeRenderer&) = delete;
    X11ChromeRenderer& operator=(const X11ChromeRenderer&) = delete;

    [[nodiscard]] bool initialize(
        Display* display,
        int screen,
        float dpi_scale,
        const UI::Theme::StudioTheme& theme);
    void shutdown();
    [[nodiscard]] const std::filesystem::path& get_icon_asset_root() const noexcept;

    [[nodiscard]] bool open_workspace_file(const std::filesystem::path& path);
    [[nodiscard]] bool set_workspace_root(const std::filesystem::path& root);
    [[nodiscard]] std::size_t open_dropped_paths(
        std::span<const std::filesystem::path> dropped_paths);
    [[nodiscard]] bool create_workspace_buffer();
    [[nodiscard]] bool handle_workspace_pointer_press(
        float point_x,
        float point_y,
        int client_width,
        int client_height,
        float content_top,
        bool extend_selection,
        int click_count,
        Time event_time,
        std::string& command_out);
    [[nodiscard]] bool handle_workspace_pointer_move(
        float point_x,
        float point_y,
        int client_width,
        int client_height,
        float content_top) noexcept;
    [[nodiscard]] bool handle_workspace_pointer_drag(
        float point_x,
        float point_y,
        int client_width,
        int client_height,
        float content_top);
    [[nodiscard]] bool handle_workspace_pointer_release() noexcept;
    [[nodiscard]] bool handle_workspace_scroll(
        std::ptrdiff_t line_delta,
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
    [[nodiscard]] bool handle_terminal_key(Terminal::TerminalInputKey key);
    [[nodiscard]] bool handle_terminal_control(char letter);
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
    [[nodiscard]] bool is_terminal_resize_handle_point(
        float point_x,
        float point_y,
        int client_width,
        int client_height,
        float content_top) const noexcept;
    [[nodiscard]] bool is_terminal_resizing() const noexcept;
    [[nodiscard]] bool is_empty_state_button_hovered() const noexcept;
    [[nodiscard]] bool tick_animations() noexcept;

    void render(
        Window window_handle,
        int client_width,
        int client_height,
        const UI::Chrome::WindowChromeLayoutResult& chrome_layout,
        const ChromeInteractionState& interaction_state,
        const CommandStateQueryCallback& command_state_query_callback) const;

    [[nodiscard]] PopupMenuGeometry calculate_popup_geometry(
        const UI::Chrome::WindowChromeLayoutResult& chrome_layout,
        std::size_t menu_index,
        bool opened_from_overflow = false) const noexcept;
    [[nodiscard]] OverflowMenuGeometry calculate_overflow_menu_geometry(
        const UI::Chrome::WindowChromeLayoutResult& chrome_layout) const noexcept;

private:
    struct ThemePixels
    {
        unsigned long window_background = 0;
        unsigned long titlebar_background = 0;
        unsigned long titlebar_border = 0;
        unsigned long text_primary = 0;
        unsigned long text_secondary = 0;
        unsigned long hover = 0;
        unsigned long pressed = 0;
        unsigned long accent = 0;
        unsigned long command_center_background = 0;
        unsigned long command_center_border = 0;
        unsigned long close_hover = 0;
        unsigned long popup_background = 0;
        unsigned long popup_border = 0;
    };

    struct ThemeTextColors
    {
        std::string primary;
        std::string secondary;
        std::string white;
    };

    [[nodiscard]] unsigned long allocate_color(const UI::Theme::Color& color) const;
    void fill_rectangle(Drawable drawable, const UI::Rect& rectangle, unsigned long color, int radius = 0) const;
    void draw_rectangle(Drawable drawable, const UI::Rect& rectangle, unsigned long color, int radius = 0) const;
    void draw_centered_text(
        Drawable drawable,
        std::string_view text,
        const UI::Rect& rectangle,
        const std::string& color) const;
    void draw_text(
        Drawable drawable,
        std::string_view text,
        const UI::Rect& rectangle,
        float left_padding,
        const std::string& color) const;
    void draw_window_control(
        Drawable drawable,
        const UI::Rect& bounds,
        UI::Chrome::WindowControl control,
        const ChromeInteractionState& interaction_state) const;
    void draw_popup_menu(
        Drawable drawable,
        const UI::Chrome::WindowChromeLayoutResult& chrome_layout,
        const ChromeInteractionState& interaction_state,
        const CommandStateQueryCallback& command_state_query_callback) const;
    void draw_overflow_menu(
        Drawable drawable,
        const UI::Chrome::WindowChromeLayoutResult& chrome_layout,
        const ChromeInteractionState& interaction_state) const;

    Display* m_display = nullptr;
    int m_screen = 0;
    float m_dpi_scale = 1.0F;
    GC m_graphics_context = nullptr;
    std::unique_ptr<AntialiasedFont> m_font;
    ThemePixels m_colors;
    ThemeTextColors m_text_colors;
    UI::Theme::Color m_titlebar_background_color{};
    UI::Theme::Color m_hover_color{};
    StudioWorkspaceRenderer m_workspace_renderer;
};

} // namespace Zenvra::Platform::X11::Components
