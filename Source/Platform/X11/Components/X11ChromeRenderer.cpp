#include "Platform/X11/Components/X11ChromeRenderer.h"

#include "Utility/Fonts.h"
#include "Utility/X11Rounded.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Zenvra::Platform::X11::Components
{

namespace
{

int round_to_int(float value)
{
    return static_cast<int>(std::lround(value));
}

std::string to_xft_color(const UI::Theme::Color& color)
{
    char value[8]{};
    std::snprintf(
        value,
        sizeof(value),
        "#%02x%02x%02x",
        static_cast<unsigned int>(color.red),
        static_cast<unsigned int>(color.green),
        static_cast<unsigned int>(color.blue));
    return value;
}

} // namespace

X11ChromeRenderer::X11ChromeRenderer() = default;

X11ChromeRenderer::~X11ChromeRenderer()
{
    shutdown();
}

bool X11ChromeRenderer::initialize(
    Display* display,
    int screen,
    float dpi_scale,
    const UI::Theme::StudioTheme& theme)
{
    shutdown();

    m_display = display;
    m_screen = screen;
    m_dpi_scale = std::max(dpi_scale, 0.5F);
    if (m_display == nullptr)
    {
        return false;
    }

    m_graphics_context = XCreateGC(m_display, RootWindow(m_display, m_screen), 0, nullptr);
    if (m_graphics_context == nullptr)
    {
        shutdown();
        return false;
    }

    char font_pattern[96]{};
    const int pixel_size = std::max(round_to_int(12.0F * m_dpi_scale), 8);
    std::snprintf(
        font_pattern,
        sizeof(font_pattern),
        "sans:pixelsize=%d:antialias=true:hinting=true",
        pixel_size);
    m_font = std::make_unique<AntialiasedFont>(m_display, m_screen, font_pattern);
    if (m_font->getHeight() <= 0)
    {
        shutdown();
        return false;
    }

    m_colors.window_background = allocate_color(theme.window_background);
    m_colors.titlebar_background = allocate_color(theme.titlebar_background);
    m_colors.titlebar_border = allocate_color(theme.titlebar_border);
    m_colors.text_primary = allocate_color(theme.text_primary);
    m_colors.text_secondary = allocate_color(theme.text_secondary);
    m_colors.hover = allocate_color(theme.hover);
    m_colors.pressed = allocate_color(theme.pressed);
    m_colors.accent = allocate_color(theme.accent);
    m_colors.command_center_background = allocate_color(theme.command_center_background);
    m_colors.command_center_border = allocate_color(theme.command_center_border);
    m_colors.close_hover = allocate_color(theme.close_hover);
    m_colors.popup_background = allocate_color(theme.panel_background);
    m_colors.popup_border = allocate_color(theme.titlebar_border);
    m_text_colors.primary = to_xft_color(theme.text_primary);
    m_text_colors.secondary = to_xft_color(theme.text_secondary);
    m_text_colors.white = "#ffffff";
    if (!m_workspace_renderer.initialize(m_display, m_screen, m_dpi_scale))
    {
        shutdown();
        return false;
    }
    return true;
}

void X11ChromeRenderer::shutdown()
{
    m_workspace_renderer.shutdown();
    // AntialiasedFont releases Xft resources through this display, so it must
    // be destroyed before the renderer forgets the connection.
    m_font.reset();
    if (m_display != nullptr && m_graphics_context != nullptr)
    {
        XFreeGC(m_display, m_graphics_context);
    }

    m_graphics_context = nullptr;
    m_display = nullptr;
}

bool X11ChromeRenderer::open_workspace_file(const std::filesystem::path& path)
{
    return m_workspace_renderer.open_file(path);
}

std::size_t X11ChromeRenderer::open_dropped_paths(
    std::span<const std::filesystem::path> dropped_paths)
{
    return m_workspace_renderer.open_dropped_paths(dropped_paths);
}

bool X11ChromeRenderer::create_workspace_buffer()
{
    return m_workspace_renderer.create_buffer();
}

bool X11ChromeRenderer::handle_workspace_pointer_press(
    float point_x,
    float point_y,
    int client_width,
    int client_height,
    float content_top,
    bool extend_selection,
    Time event_time)
{
    return m_workspace_renderer.handle_pointer_press(
        point_x,
        point_y,
        client_width,
        client_height,
        content_top,
        extend_selection,
        event_time);
}

bool X11ChromeRenderer::handle_workspace_pointer_move(
    float point_x,
    float point_y,
    int client_width,
    int client_height,
    float content_top) noexcept
{
    return m_workspace_renderer.handle_pointer_move(
        point_x, point_y, client_width, client_height, content_top);
}

bool X11ChromeRenderer::handle_workspace_pointer_drag(
    float point_x,
    float point_y,
    int client_width,
    int client_height,
    float content_top)
{
    return m_workspace_renderer.handle_pointer_drag(
        point_x, point_y, client_width, client_height, content_top);
}

bool X11ChromeRenderer::handle_workspace_pointer_release() noexcept
{
    return m_workspace_renderer.handle_pointer_release();
}

bool X11ChromeRenderer::handle_workspace_scroll(
    std::ptrdiff_t line_delta,
    int client_width,
    int client_height,
    float content_top) noexcept
{
    return m_workspace_renderer.handle_scroll(
        line_delta, client_width, client_height, content_top);
}

bool X11ChromeRenderer::handle_editor_input(
    UI::Editor::EditorInputCommand command,
    bool extend_selection)
{
    return m_workspace_renderer.handle_editor_input(command, extend_selection);
}

bool X11ChromeRenderer::handle_editor_action(UI::Editor::EditorAction action)
{
    return m_workspace_renderer.handle_editor_action(action);
}

std::optional<bool> X11ChromeRenderer::handle_editor_command(std::string_view command_id)
{
    return m_workspace_renderer.handle_editor_command(command_id);
}

std::optional<bool> X11ChromeRenderer::is_editor_command_enabled(
    std::string_view command_id) const noexcept
{
    return m_workspace_renderer.is_editor_command_enabled(command_id);
}

bool X11ChromeRenderer::handle_text_input(std::string_view utf8_text)
{
    return m_workspace_renderer.handle_text_input(utf8_text);
}

bool X11ChromeRenderer::handle_terminal_key(Terminal::TerminalInputKey key)
{
    return m_workspace_renderer.handle_terminal_key(key);
}

bool X11ChromeRenderer::handle_terminal_control(char letter)
{
    return m_workspace_renderer.handle_terminal_control(letter);
}

bool X11ChromeRenderer::handle_terminal_scroll(std::ptrdiff_t line_delta) noexcept
{
    return m_workspace_renderer.handle_terminal_scroll(line_delta);
}

bool X11ChromeRenderer::handle_tool_sidebar_scroll(
    std::ptrdiff_t line_delta,
    int client_width,
    int client_height,
    float content_top) noexcept
{
    return m_workspace_renderer.handle_tool_sidebar_scroll(
        line_delta, client_width, client_height, content_top);
}

bool X11ChromeRenderer::is_editor_focused() const noexcept
{
    return m_workspace_renderer.is_editor_focused();
}

bool X11ChromeRenderer::is_terminal_focused() const noexcept
{
    return m_workspace_renderer.is_terminal_focused();
}

bool X11ChromeRenderer::is_tab_bar_point(
    float point_x,
    float point_y,
    int client_width,
    int client_height,
    float content_top) const noexcept
{
    return m_workspace_renderer.is_tab_bar_point(
        point_x, point_y, client_width, client_height, content_top);
}

bool X11ChromeRenderer::is_editor_point(
    float point_x,
    float point_y,
    int client_width,
    int client_height,
    float content_top) const noexcept
{
    return m_workspace_renderer.is_editor_point(
        point_x, point_y, client_width, client_height, content_top);
}

bool X11ChromeRenderer::is_scrollbar_point(
    float point_x,
    float point_y,
    int client_width,
    int client_height,
    float content_top) const noexcept
{
    return m_workspace_renderer.is_scrollbar_point(
        point_x, point_y, client_width, client_height, content_top);
}

bool X11ChromeRenderer::is_minimap_point(
    float point_x,
    float point_y,
    int client_width,
    int client_height,
    float content_top) const noexcept
{
    return m_workspace_renderer.is_minimap_point(
        point_x, point_y, client_width, client_height, content_top);
}

bool X11ChromeRenderer::is_terminal_point(
    float point_x,
    float point_y,
    int client_width,
    int client_height,
    float content_top) const noexcept
{
    return m_workspace_renderer.is_terminal_point(
        point_x, point_y, client_width, client_height, content_top);
}

bool X11ChromeRenderer::is_tool_sidebar_point(
    float point_x,
    float point_y,
    int client_width,
    int client_height,
    float content_top) const noexcept
{
    return m_workspace_renderer.is_tool_sidebar_point(
        point_x, point_y, client_width, client_height, content_top);
}

bool X11ChromeRenderer::is_terminal_resize_handle_point(
    float point_x,
    float point_y,
    int client_width,
    int client_height,
    float content_top) const noexcept
{
    return m_workspace_renderer.is_terminal_resize_handle_point(
        point_x, point_y, client_width, client_height, content_top);
}

bool X11ChromeRenderer::is_terminal_resizing() const noexcept
{
    return m_workspace_renderer.is_terminal_resizing();
}

bool X11ChromeRenderer::tick_caret_blink() noexcept
{
    return m_workspace_renderer.tick_caret_blink();
}

void X11ChromeRenderer::render(
    Window window_handle,
    int client_width,
    int client_height,
    const UI::Chrome::WindowChromeLayoutResult& chrome_layout,
    const ChromeInteractionState& interaction_state,
    const CommandStateQueryCallback& command_state_query_callback) const
{
    if (m_display == nullptr || m_graphics_context == nullptr || window_handle == 0 ||
        client_width <= 0 || client_height <= 0)
    {
        return;
    }

    const unsigned int pixmap_width = static_cast<unsigned int>(client_width);
    const unsigned int pixmap_height = static_cast<unsigned int>(client_height);
    Pixmap back_buffer = XCreatePixmap(
        m_display,
        window_handle,
        pixmap_width,
        pixmap_height,
        static_cast<unsigned int>(DefaultDepth(m_display, m_screen)));
    if (back_buffer == 0)
    {
        return;
    }

    fill_rectangle(
        back_buffer,
        UI::Rect{0.0F, 0.0F, static_cast<float>(client_width), static_cast<float>(client_height)},
        m_colors.window_background);
    fill_rectangle(back_buffer, chrome_layout.titlebar_bounds, m_colors.titlebar_background);

    const std::span<const UI::Chrome::WindowMenu> menus = UI::Chrome::get_window_menu_model();
    for (std::size_t region_index = 0; region_index < chrome_layout.visible_menu_count; ++region_index)
    {
        const UI::Chrome::MenuRegion& region = chrome_layout.menu_regions[region_index];
        const bool hovered = interaction_state.hovered_menu_index == region.menu_index ||
            interaction_state.open_menu_index == region.menu_index;
        if (hovered)
        {
            UI::Rect hover_bounds = region.bounds;
            hover_bounds.y += 4.0F * m_dpi_scale;
            hover_bounds.height -= 8.0F * m_dpi_scale;
            fill_rectangle(back_buffer, hover_bounds, m_colors.hover, 4);
        }
        if (region.menu_index < menus.size())
        {
            draw_centered_text(
                back_buffer,
                menus[region.menu_index].label,
                region.bounds,
                m_text_colors.primary);
        }
    }

    if (chrome_layout.has_overflow_menu())
    {
        const bool hidden_menu_open = interaction_state.open_menu_index &&
            *interaction_state.open_menu_index >= chrome_layout.first_overflow_menu_index;
        if (interaction_state.overflow_menu_hovered || interaction_state.overflow_menu_open ||
            hidden_menu_open)
        {
            UI::Rect hover_bounds = chrome_layout.overflow_menu_bounds;
            hover_bounds.y += 4.0F * m_dpi_scale;
            hover_bounds.height -= 8.0F * m_dpi_scale;
            fill_rectangle(back_buffer, hover_bounds, m_colors.hover, 4);
        }
        const int line_half_width = std::max(round_to_int(6.0F * m_dpi_scale), 4);
        const int line_gap = std::max(round_to_int(4.0F * m_dpi_scale), 3);
        const int center_x = round_to_int(
            chrome_layout.overflow_menu_bounds.x +
            chrome_layout.overflow_menu_bounds.width * 0.5F);
        const int center_y = round_to_int(
            chrome_layout.overflow_menu_bounds.y +
            chrome_layout.overflow_menu_bounds.height * 0.5F);
        XSetForeground(m_display, m_graphics_context, m_colors.text_primary);
        for (int row = -1; row <= 1; ++row)
        {
            XDrawLine(
                m_display,
                back_buffer,
                m_graphics_context,
                center_x - line_half_width,
                center_y + row * line_gap,
                center_x + line_half_width,
                center_y + row * line_gap);
        }
    }

    const float scale = chrome_layout.dpi_scale;
    const float logo_size = 22.0F * scale;
    const UI::Rect logo_bounds{
        chrome_layout.logo_bounds.x + (chrome_layout.logo_bounds.width - logo_size) * 0.5F,
        chrome_layout.logo_bounds.y + (chrome_layout.logo_bounds.height - logo_size) * 0.5F,
        logo_size,
        logo_size,
    };
    fill_rectangle(back_buffer, logo_bounds, m_colors.accent, static_cast<int>(logo_size * 0.25F));
    draw_centered_text(back_buffer, "Z", logo_bounds, m_text_colors.white);

    draw_window_control(
        back_buffer,
        chrome_layout.minimize_bounds,
        UI::Chrome::WindowControl::Minimize,
        interaction_state);
    draw_window_control(
        back_buffer,
        chrome_layout.maximize_bounds,
        UI::Chrome::WindowControl::MaximizeRestore,
        interaction_state);
    draw_window_control(
        back_buffer,
        chrome_layout.close_bounds,
        UI::Chrome::WindowControl::Close,
        interaction_state);

    m_workspace_renderer.render(
        back_buffer,
        client_width,
        client_height,
        chrome_layout.titlebar_bounds.bottom());

    draw_overflow_menu(back_buffer, chrome_layout, interaction_state);
    draw_popup_menu(
        back_buffer,
        chrome_layout,
        interaction_state,
        command_state_query_callback);

    XCopyArea(
        m_display,
        back_buffer,
        window_handle,
        m_graphics_context,
        0,
        0,
        pixmap_width,
        pixmap_height,
        0,
        0);
    XFreePixmap(m_display, back_buffer);
    XFlush(m_display);
}

PopupMenuGeometry X11ChromeRenderer::calculate_popup_geometry(
    const UI::Chrome::WindowChromeLayoutResult& chrome_layout,
    std::size_t menu_index,
    bool opened_from_overflow) const noexcept
{
    PopupMenuGeometry geometry;
    const std::span<const UI::Chrome::WindowMenu> menus = UI::Chrome::get_window_menu_model();
    if (menu_index >= menus.size())
    {
        return geometry;
    }

    const UI::Rect* anchor_bounds = nullptr;
    for (std::size_t index = 0; index < chrome_layout.visible_menu_count; ++index)
    {
        if (chrome_layout.menu_regions[index].menu_index == menu_index)
        {
            anchor_bounds = &chrome_layout.menu_regions[index].bounds;
            break;
        }
    }
    if (anchor_bounds == nullptr && chrome_layout.has_overflow_menu() &&
        menu_index >= chrome_layout.first_overflow_menu_index)
    {
        anchor_bounds = &chrome_layout.overflow_menu_bounds;
    }
    if (anchor_bounds == nullptr)
    {
        return geometry;
    }

    const UI::Chrome::WindowMenu& menu = menus[menu_index];
    const float row_height = 28.0F * m_dpi_scale;
    const float separator_height = 9.0F * m_dpi_scale;
    float popup_width = 220.0F * m_dpi_scale;
    for (const UI::Chrome::WindowMenuItem& item : menu.items)
    {
        popup_width = std::max(
            popup_width,
            static_cast<float>(item.label.size()) * 7.0F * m_dpi_scale + 48.0F * m_dpi_scale);
    }
    popup_width = std::min(popup_width, 380.0F * m_dpi_scale);

    float current_y = chrome_layout.titlebar_bounds.bottom();
    geometry.bounds.x = anchor_bounds->x;
    geometry.bounds.y = current_y;
    if (opened_from_overflow)
    {
        const std::size_t overflow_row =
            menu_index - chrome_layout.first_overflow_menu_index;
        const OverflowMenuGeometry root_geometry =
            calculate_overflow_menu_geometry(chrome_layout);
        geometry.bounds.x = root_geometry.bounds.right() + 2.0F * m_dpi_scale;
        geometry.bounds.y += static_cast<float>(overflow_row) * 28.0F * m_dpi_scale;
        current_y = geometry.bounds.y;
    }
    geometry.bounds.width = popup_width;
    geometry.item_count = std::min(menu.items.size(), max_popup_menu_items);

    for (std::size_t item_index = 0; item_index < geometry.item_count; ++item_index)
    {
        const float height = menu.items[item_index].separator ? separator_height : row_height;
        geometry.item_bounds[item_index] = {
            geometry.bounds.x,
            current_y,
            geometry.bounds.width,
            height,
        };
        current_y += height;
    }
    geometry.bounds.height = current_y - geometry.bounds.y;
    return geometry;
}

OverflowMenuGeometry X11ChromeRenderer::calculate_overflow_menu_geometry(
    const UI::Chrome::WindowChromeLayoutResult& chrome_layout) const noexcept
{
    OverflowMenuGeometry geometry;
    if (!chrome_layout.has_overflow_menu())
    {
        return geometry;
    }

    const std::span<const UI::Chrome::WindowMenu> menus = UI::Chrome::get_window_menu_model();
    geometry.first_menu_index = chrome_layout.first_overflow_menu_index;
    if (geometry.first_menu_index >= menus.size())
    {
        return geometry;
    }
    geometry.item_count = std::min(
        menus.size() - geometry.first_menu_index,
        geometry.item_bounds.size());
    const float row_height = 28.0F * m_dpi_scale;
    float popup_width = 160.0F * m_dpi_scale;
    for (std::size_t menu_index = geometry.first_menu_index; menu_index < menus.size(); ++menu_index)
    {
        popup_width = std::max(
            popup_width,
            static_cast<float>(menus[menu_index].label.size()) * 7.0F * m_dpi_scale +
                32.0F * m_dpi_scale);
    }

    geometry.bounds = {
        chrome_layout.overflow_menu_bounds.x,
        chrome_layout.titlebar_bounds.bottom(),
        popup_width,
        row_height * static_cast<float>(geometry.item_count),
    };
    for (std::size_t item_index = 0; item_index < geometry.item_count; ++item_index)
    {
        geometry.item_bounds[item_index] = {
            geometry.bounds.x,
            geometry.bounds.y + row_height * static_cast<float>(item_index),
            geometry.bounds.width,
            row_height,
        };
    }
    return geometry;
}

unsigned long X11ChromeRenderer::allocate_color(const UI::Theme::Color& color) const
{
    XColor x_color{};
    x_color.red = static_cast<unsigned short>(color.red * 257U);
    x_color.green = static_cast<unsigned short>(color.green * 257U);
    x_color.blue = static_cast<unsigned short>(color.blue * 257U);
    x_color.flags = DoRed | DoGreen | DoBlue;
    const Colormap colormap = DefaultColormap(m_display, m_screen);
    if (XAllocColor(m_display, colormap, &x_color) == 0)
    {
        return BlackPixel(m_display, m_screen);
    }
    return x_color.pixel;
}

void X11ChromeRenderer::fill_rectangle(
    Drawable drawable,
    const UI::Rect& rectangle,
    unsigned long color,
    int radius) const
{
    if (rectangle.is_empty())
    {
        return;
    }

    XSetForeground(m_display, m_graphics_context, color);
    Utility::X11Rounded::X11Rounded::fillRoundedRect(
        m_display,
        drawable,
        m_graphics_context,
        round_to_int(rectangle.x),
        round_to_int(rectangle.y),
        round_to_int(rectangle.width),
        round_to_int(rectangle.height),
        radius);
}

void X11ChromeRenderer::draw_rectangle(
    Drawable drawable,
    const UI::Rect& rectangle,
    unsigned long color,
    int radius) const
{
    if (rectangle.is_empty())
    {
        return;
    }

    XSetForeground(m_display, m_graphics_context, color);
    Utility::X11Rounded::X11Rounded::drawRoundedRect(
        m_display,
        drawable,
        m_graphics_context,
        round_to_int(rectangle.x),
        round_to_int(rectangle.y),
        round_to_int(rectangle.width),
        round_to_int(rectangle.height),
        radius);
}

void X11ChromeRenderer::draw_centered_text(
    Drawable drawable,
    std::string_view text,
    const UI::Rect& rectangle,
    const std::string& color) const
{
    if (text.empty() || rectangle.is_empty())
    {
        return;
    }

    const std::string utf8_text{text};
    const int text_width = m_font != nullptr
        ? m_font->getTextWidth(utf8_text)
        : static_cast<int>(text.size()) * 8;
    const int font_ascent = m_font != nullptr ? m_font->getAscent() : 8;
    const int font_descent = m_font != nullptr ? m_font->getDescent() : 2;
    const int text_x = round_to_int(rectangle.x + (rectangle.width - static_cast<float>(text_width)) * 0.5F);
    const int text_y = round_to_int(
        rectangle.y +
        (rectangle.height - static_cast<float>(font_ascent + font_descent)) * 0.5F +
        static_cast<float>(font_ascent));

    if (m_font != nullptr)
    {
        m_font->drawString(drawable, color, text_x, text_y, utf8_text);
    }
}

void X11ChromeRenderer::draw_text(
    Drawable drawable,
    std::string_view text,
    const UI::Rect& rectangle,
    float left_padding,
    const std::string& color) const
{
    if (text.empty() || rectangle.is_empty())
    {
        return;
    }

    const int font_ascent = m_font != nullptr ? m_font->getAscent() : 8;
    const int font_descent = m_font != nullptr ? m_font->getDescent() : 2;
    const int text_x = round_to_int(rectangle.x + left_padding);
    const int text_y = round_to_int(
        rectangle.y +
        (rectangle.height - static_cast<float>(font_ascent + font_descent)) * 0.5F +
        static_cast<float>(font_ascent));

    if (m_font != nullptr)
    {
        m_font->drawString(drawable, color, text_x, text_y, std::string{text});
    }
}

void X11ChromeRenderer::draw_window_control(
    Drawable drawable,
    const UI::Rect& bounds,
    UI::Chrome::WindowControl control,
    const ChromeInteractionState& interaction_state) const
{
    if (bounds.is_empty() || control == UI::Chrome::WindowControl::NoControl)
    {
        return;
    }

    const bool pressed = interaction_state.pressed_control == control;
    const bool hovered = interaction_state.hovered_control == control;
    if (pressed || hovered)
    {
        const unsigned long background = control == UI::Chrome::WindowControl::Close && hovered
            ? m_colors.close_hover
            : (pressed ? m_colors.pressed : m_colors.hover);
        fill_rectangle(drawable, bounds, background);
    }

    const bool white_close_glyph = control == UI::Chrome::WindowControl::Close && (hovered || pressed);
    const unsigned long icon_color = white_close_glyph
        ? WhitePixel(m_display, m_screen)
        : (interaction_state.focused ? m_colors.text_primary : m_colors.text_secondary);
    const int line_width = std::max(round_to_int(m_dpi_scale), 1);
    const int center_x = round_to_int(bounds.x + bounds.width * 0.5F);
    const int center_y = round_to_int(bounds.y + bounds.height * 0.5F);
    const int half_size = std::max(round_to_int(5.0F * m_dpi_scale), 4);

    XSetForeground(m_display, m_graphics_context, icon_color);
    XSetLineAttributes(m_display, m_graphics_context, line_width, LineSolid, CapButt, JoinMiter);
    if (control == UI::Chrome::WindowControl::Minimize)
    {
        const int baseline_y = center_y + std::max(round_to_int(2.0F * m_dpi_scale), 1);
        XDrawLine(
            m_display,
            drawable,
            m_graphics_context,
            center_x - half_size,
            baseline_y,
            center_x + half_size,
            baseline_y);
    }
    else if (control == UI::Chrome::WindowControl::MaximizeRestore)
    {
        const unsigned int box_size = static_cast<unsigned int>(half_size * 2);
        if (interaction_state.maximized)
        {
            const int offset = std::max(round_to_int(2.0F * m_dpi_scale), 2);
            XDrawRectangle(
                m_display,
                drawable,
                m_graphics_context,
                center_x - half_size + offset,
                center_y - half_size,
                box_size - static_cast<unsigned int>(offset),
                box_size - static_cast<unsigned int>(offset));
            XDrawRectangle(
                m_display,
                drawable,
                m_graphics_context,
                center_x - half_size,
                center_y - half_size + offset,
                box_size - static_cast<unsigned int>(offset),
                box_size - static_cast<unsigned int>(offset));
        }
        else
        {
            XDrawRectangle(
                m_display,
                drawable,
                m_graphics_context,
                center_x - half_size,
                center_y - half_size,
                box_size,
                box_size);
        }
    }
    else if (control == UI::Chrome::WindowControl::Close)
    {
        XDrawLine(
            m_display,
            drawable,
            m_graphics_context,
            center_x - half_size,
            center_y - half_size,
            center_x + half_size,
            center_y + half_size);
        XDrawLine(
            m_display,
            drawable,
            m_graphics_context,
            center_x - half_size,
            center_y + half_size,
            center_x + half_size,
            center_y - half_size);
    }
    XSetLineAttributes(m_display, m_graphics_context, 1, LineSolid, CapButt, JoinMiter);
}

void X11ChromeRenderer::draw_popup_menu(
    Drawable drawable,
    const UI::Chrome::WindowChromeLayoutResult& chrome_layout,
    const ChromeInteractionState& interaction_state,
    const CommandStateQueryCallback& command_state_query_callback) const
{
    if (!interaction_state.open_menu_index)
    {
        return;
    }

    const std::span<const UI::Chrome::WindowMenu> menus = UI::Chrome::get_window_menu_model();
    const std::size_t menu_index = *interaction_state.open_menu_index;
    if (menu_index >= menus.size())
    {
        return;
    }

    const PopupMenuGeometry geometry = calculate_popup_geometry(
        chrome_layout,
        menu_index,
        interaction_state.overflow_menu_open);
    if (geometry.bounds.is_empty())
    {
        return;
    }

    const int popup_radius = std::max(round_to_int(7.0F * m_dpi_scale), 5);
    fill_rectangle(drawable, geometry.bounds, m_colors.popup_background, popup_radius);
    draw_rectangle(drawable, geometry.bounds, m_colors.popup_border, popup_radius);

    const UI::Chrome::WindowMenu& menu = menus[menu_index];
    for (std::size_t item_index = 0; item_index < geometry.item_count; ++item_index)
    {
        const UI::Chrome::WindowMenuItem& item = menu.items[item_index];
        const UI::Rect& item_bounds = geometry.item_bounds[item_index];
        if (item.separator)
        {
            const float line_y = item_bounds.y + item_bounds.height * 0.5F;
            fill_rectangle(
                drawable,
                UI::Rect{
                    item_bounds.x + 8.0F * m_dpi_scale,
                    line_y,
                    item_bounds.width - 16.0F * m_dpi_scale,
                    1.0F,
                },
                m_colors.popup_border);
            continue;
        }

        CommandPresentationState state = item.command_id.empty()
            ? CommandPresentationState{}
            : (command_state_query_callback
                    ? command_state_query_callback(item.command_id)
                    : CommandPresentationState{true, false});
        if (const std::optional<bool> editor_enabled =
                m_workspace_renderer.is_editor_command_enabled(item.command_id))
        {
            state.enabled = *editor_enabled;
        }
        const bool is_hovered = interaction_state.hovered_popup_item_index == item_index && state.enabled;
        if (is_hovered)
        {
            UI::Rect hover_bounds = item_bounds;
            hover_bounds.x += 4.0F * m_dpi_scale;
            hover_bounds.width -= 8.0F * m_dpi_scale;
            hover_bounds.y += 2.0F * m_dpi_scale;
            hover_bounds.height -= 4.0F * m_dpi_scale;
            fill_rectangle(
                drawable,
                hover_bounds,
                m_colors.accent,
                std::max(round_to_int(4.0F * m_dpi_scale), 3));
        }

        std::string text_color = m_text_colors.secondary;
        if (state.enabled)
        {
            text_color = is_hovered ? m_text_colors.white : m_text_colors.primary;
        }

        draw_text(
            drawable,
            item.label,
            item_bounds,
            26.0F * m_dpi_scale,
            text_color);

        if (state.checked)
        {
            XSetForeground(m_display, m_graphics_context, is_hovered ? WhitePixel(m_display, m_screen) : m_colors.text_primary);
            const int check_x = round_to_int(item_bounds.x + 11.0F * m_dpi_scale);
            const int check_y = round_to_int(item_bounds.y + item_bounds.height * 0.5F);
            XDrawLine(m_display, drawable, m_graphics_context, check_x, check_y, check_x + 3, check_y + 3);
            XDrawLine(m_display, drawable, m_graphics_context, check_x + 3, check_y + 3, check_x + 8, check_y - 3);
        }
    }
}

void X11ChromeRenderer::draw_overflow_menu(
    Drawable drawable,
    const UI::Chrome::WindowChromeLayoutResult& chrome_layout,
    const ChromeInteractionState& interaction_state) const
{
    if (!interaction_state.overflow_menu_open)
    {
        return;
    }

    const OverflowMenuGeometry geometry = calculate_overflow_menu_geometry(chrome_layout);
    if (geometry.bounds.is_empty())
    {
        return;
    }

    const int popup_radius = std::max(round_to_int(7.0F * m_dpi_scale), 5);
    fill_rectangle(drawable, geometry.bounds, m_colors.popup_background, popup_radius);
    draw_rectangle(drawable, geometry.bounds, m_colors.popup_border, popup_radius);
    const std::span<const UI::Chrome::WindowMenu> menus = UI::Chrome::get_window_menu_model();
    for (std::size_t item_index = 0; item_index < geometry.item_count; ++item_index)
    {
        const std::size_t menu_index = geometry.first_menu_index + item_index;
        if (menu_index >= menus.size())
        {
            break;
        }
        const bool is_hovered = interaction_state.hovered_overflow_menu_index == menu_index ||
            interaction_state.open_menu_index == menu_index;
        if (is_hovered)
        {
            UI::Rect hover_bounds = geometry.item_bounds[item_index];
            hover_bounds.x += 4.0F * m_dpi_scale;
            hover_bounds.width -= 8.0F * m_dpi_scale;
            hover_bounds.y += 2.0F * m_dpi_scale;
            hover_bounds.height -= 4.0F * m_dpi_scale;
            fill_rectangle(
                drawable,
                hover_bounds,
                m_colors.accent,
                std::max(round_to_int(4.0F * m_dpi_scale), 3));
        }
        draw_text(
            drawable,
            menus[menu_index].label,
            geometry.item_bounds[item_index],
            12.0F * m_dpi_scale,
            is_hovered ? m_text_colors.white : m_text_colors.primary);
    }
}

} // namespace Zenvra::Platform::X11::Components
