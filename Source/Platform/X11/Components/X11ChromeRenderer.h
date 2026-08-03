#pragma once

#include "Platform/IPlatformWindow.h"
#include "UI/Chrome/WindowChromeLayout.h"
#include "UI/Chrome/WindowMenuModel.h"
#include "UI/Theme/StudioTheme.h"

#include <X11/Xlib.h>

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

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
    bool maximized = false;
    bool focused = false;
};

class X11ChromeRenderer
{
public:
    X11ChromeRenderer() = default;
    ~X11ChromeRenderer();

    X11ChromeRenderer(const X11ChromeRenderer&) = delete;
    X11ChromeRenderer& operator=(const X11ChromeRenderer&) = delete;

    [[nodiscard]] bool initialize(
        Display* display,
        int screen,
        float dpi_scale,
        const UI::Theme::StudioTheme& theme);
    void shutdown();

    void render(
        Window window_handle,
        int client_width,
        int client_height,
        const UI::Chrome::WindowChromeLayoutResult& chrome_layout,
        std::string_view title,
        const ChromeInteractionState& interaction_state,
        const CommandStateQueryCallback& command_state_query_callback) const;

    [[nodiscard]] PopupMenuGeometry calculate_popup_geometry(
        const UI::Chrome::WindowChromeLayoutResult& chrome_layout,
        std::size_t menu_index) const noexcept;
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

    [[nodiscard]] unsigned long allocate_color(const UI::Theme::Color& color) const;
    void fill_rectangle(Drawable drawable, const UI::Rect& rectangle, unsigned long color) const;
    void draw_rectangle(Drawable drawable, const UI::Rect& rectangle, unsigned long color) const;
    void draw_centered_text(
        Drawable drawable,
        std::string_view text,
        const UI::Rect& rectangle,
        unsigned long color) const;
    void draw_text(
        Drawable drawable,
        std::string_view text,
        const UI::Rect& rectangle,
        float left_padding,
        unsigned long color) const;
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
    XFontStruct* m_font = nullptr;
    ThemePixels m_colors;
};

} // namespace Zenvra::Platform::X11::Components
