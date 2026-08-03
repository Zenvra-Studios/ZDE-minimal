#include "Platform/X11/Components/X11ChromeRenderer.h"

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

unsigned int to_unsigned_size(float value)
{
    return static_cast<unsigned int>(std::max(round_to_int(value), 0));
}

} // namespace

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

    char font_pattern[128]{};
    const int pixel_size = std::max(round_to_int(13.0F * m_dpi_scale), 8);
    std::snprintf(
        font_pattern,
        sizeof(font_pattern),
        "-*-sans-medium-r-normal--%d-*-*-*-*-*-*-*",
        pixel_size);
    m_font = XLoadQueryFont(m_display, font_pattern);
    if (m_font == nullptr)
    {
        m_font = XLoadQueryFont(m_display, "fixed");
    }
    if (m_font != nullptr)
    {
        XSetFont(m_display, m_graphics_context, m_font->fid);
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
    return true;
}

void X11ChromeRenderer::shutdown()
{
    if (m_display != nullptr && m_font != nullptr)
    {
        XFreeFont(m_display, m_font);
    }
    if (m_display != nullptr && m_graphics_context != nullptr)
    {
        XFreeGC(m_display, m_graphics_context);
    }

    m_font = nullptr;
    m_graphics_context = nullptr;
    m_display = nullptr;
}

void X11ChromeRenderer::render(
    Window window_handle,
    int client_width,
    int client_height,
    const UI::Chrome::WindowChromeLayoutResult& chrome_layout,
    std::string_view title,
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
    fill_rectangle(
        back_buffer,
        UI::Rect{
            0.0F,
            chrome_layout.titlebar_bounds.bottom() - 1.0F,
            static_cast<float>(client_width),
            1.0F,
        },
        m_colors.titlebar_border);

    const std::span<const UI::Chrome::WindowMenu> menus = UI::Chrome::get_window_menu_model();
    for (std::size_t region_index = 0; region_index < chrome_layout.visible_menu_count; ++region_index)
    {
        const UI::Chrome::MenuRegion& region = chrome_layout.menu_regions[region_index];
        const bool hovered = interaction_state.hovered_menu_index == region.menu_index ||
            interaction_state.open_menu_index == region.menu_index;
        if (hovered)
        {
            fill_rectangle(back_buffer, region.bounds, m_colors.hover);
        }
        if (region.menu_index < menus.size())
        {
            draw_centered_text(back_buffer, menus[region.menu_index].label, region.bounds, m_colors.text_primary);
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
    fill_rectangle(back_buffer, logo_bounds, m_colors.accent);
    draw_centered_text(back_buffer, "Z", logo_bounds, WhitePixel(m_display, m_screen));

    if (!chrome_layout.command_center_bounds.is_empty())
    {
        fill_rectangle(
            back_buffer,
            chrome_layout.command_center_bounds,
            interaction_state.command_center_hovered ? m_colors.hover : m_colors.command_center_background);
        draw_rectangle(back_buffer, chrome_layout.command_center_bounds, m_colors.command_center_border);
        draw_centered_text(back_buffer, title, chrome_layout.command_center_bounds, m_colors.text_secondary);

        const int search_x = round_to_int(chrome_layout.command_center_bounds.x + 14.0F * scale);
        const int search_y = round_to_int(
            chrome_layout.command_center_bounds.y + chrome_layout.command_center_bounds.height * 0.5F);
        const int search_radius = std::max(round_to_int(4.0F * scale), 2);
        XSetForeground(m_display, m_graphics_context, m_colors.text_secondary);
        XDrawArc(
            m_display,
            back_buffer,
            m_graphics_context,
            search_x - search_radius,
            search_y - search_radius,
            static_cast<unsigned int>(search_radius * 2),
            static_cast<unsigned int>(search_radius * 2),
            0,
            360 * 64);
        XDrawLine(
            m_display,
            back_buffer,
            m_graphics_context,
            search_x + search_radius - 1,
            search_y + search_radius - 1,
            search_x + search_radius + round_to_int(3.0F * scale),
            search_y + search_radius + round_to_int(3.0F * scale));
    }

    const auto draw_control_background = [this, back_buffer, &interaction_state](
                                             UI::Chrome::WindowControl control,
                                             const UI::Rect& bounds) {
        if (interaction_state.pressed_control == control)
        {
            fill_rectangle(back_buffer, bounds, m_colors.pressed);
        }
        else if (interaction_state.hovered_control == control)
        {
            fill_rectangle(
                back_buffer,
                bounds,
                control == UI::Chrome::WindowControl::Close ? m_colors.close_hover : m_colors.hover);
        }
    };

    draw_control_background(UI::Chrome::WindowControl::Minimize, chrome_layout.minimize_bounds);
    draw_control_background(UI::Chrome::WindowControl::MaximizeRestore, chrome_layout.maximize_bounds);
    draw_control_background(UI::Chrome::WindowControl::Close, chrome_layout.close_bounds);

    XSetForeground(m_display, m_graphics_context, m_colors.text_primary);
    const float icon_half_size = 5.0F * scale;
    const auto center_x = [](const UI::Rect& bounds) { return bounds.x + bounds.width * 0.5F; };
    const auto center_y = [](const UI::Rect& bounds) { return bounds.y + bounds.height * 0.5F; };

    const int minimize_x = round_to_int(center_x(chrome_layout.minimize_bounds));
    const int minimize_y = round_to_int(center_y(chrome_layout.minimize_bounds));
    XDrawLine(
        m_display,
        back_buffer,
        m_graphics_context,
        minimize_x - round_to_int(icon_half_size),
        minimize_y,
        minimize_x + round_to_int(icon_half_size),
        minimize_y);

    const int maximize_x = round_to_int(center_x(chrome_layout.maximize_bounds));
    const int maximize_y = round_to_int(center_y(chrome_layout.maximize_bounds));
    const int icon_size = std::max(round_to_int(icon_half_size * 2.0F), 4);
    if (interaction_state.maximized)
    {
        XDrawRectangle(
            m_display,
            back_buffer,
            m_graphics_context,
            maximize_x - icon_size / 2 + 2,
            maximize_y - icon_size / 2 - 2,
            static_cast<unsigned int>(icon_size),
            static_cast<unsigned int>(icon_size));
        XDrawRectangle(
            m_display,
            back_buffer,
            m_graphics_context,
            maximize_x - icon_size / 2 - 2,
            maximize_y - icon_size / 2 + 2,
            static_cast<unsigned int>(icon_size),
            static_cast<unsigned int>(icon_size));
    }
    else
    {
        XDrawRectangle(
            m_display,
            back_buffer,
            m_graphics_context,
            maximize_x - icon_size / 2,
            maximize_y - icon_size / 2,
            static_cast<unsigned int>(icon_size),
            static_cast<unsigned int>(icon_size));
    }

    const int close_x = round_to_int(center_x(chrome_layout.close_bounds));
    const int close_y = round_to_int(center_y(chrome_layout.close_bounds));
    const int close_half_size = std::max(round_to_int(icon_half_size), 2);
    XDrawLine(
        m_display,
        back_buffer,
        m_graphics_context,
        close_x - close_half_size,
        close_y - close_half_size,
        close_x + close_half_size,
        close_y + close_half_size);
    XDrawLine(
        m_display,
        back_buffer,
        m_graphics_context,
        close_x + close_half_size,
        close_y - close_half_size,
        close_x - close_half_size,
        close_y + close_half_size);

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
    std::size_t menu_index) const noexcept
{
    PopupMenuGeometry geometry;
    const std::span<const UI::Chrome::WindowMenu> menus = UI::Chrome::get_window_menu_model();
    if (menu_index >= menus.size())
    {
        return geometry;
    }

    const UI::Chrome::MenuRegion* menu_region = nullptr;
    for (std::size_t index = 0; index < chrome_layout.visible_menu_count; ++index)
    {
        if (chrome_layout.menu_regions[index].menu_index == menu_index)
        {
            menu_region = &chrome_layout.menu_regions[index];
            break;
        }
    }
    if (menu_region == nullptr)
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
    geometry.bounds.x = menu_region->bounds.x;
    geometry.bounds.y = current_y;
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
    unsigned long color) const
{
    if (rectangle.is_empty())
    {
        return;
    }

    XSetForeground(m_display, m_graphics_context, color);
    XFillRectangle(
        m_display,
        drawable,
        m_graphics_context,
        round_to_int(rectangle.x),
        round_to_int(rectangle.y),
        to_unsigned_size(rectangle.width),
        to_unsigned_size(rectangle.height));
}

void X11ChromeRenderer::draw_rectangle(
    Drawable drawable,
    const UI::Rect& rectangle,
    unsigned long color) const
{
    if (rectangle.is_empty())
    {
        return;
    }

    XSetForeground(m_display, m_graphics_context, color);
    XDrawRectangle(
        m_display,
        drawable,
        m_graphics_context,
        round_to_int(rectangle.x),
        round_to_int(rectangle.y),
        to_unsigned_size(rectangle.width - 1.0F),
        to_unsigned_size(rectangle.height - 1.0F));
}

void X11ChromeRenderer::draw_centered_text(
    Drawable drawable,
    std::string_view text,
    const UI::Rect& rectangle,
    unsigned long color) const
{
    if (text.empty() || rectangle.is_empty())
    {
        return;
    }

    const int text_width = m_font != nullptr
        ? XTextWidth(m_font, text.data(), static_cast<int>(text.size()))
        : static_cast<int>(text.size()) * 8;
    const int font_ascent = m_font != nullptr ? m_font->ascent : 8;
    const int font_descent = m_font != nullptr ? m_font->descent : 2;
    const int text_x = round_to_int(rectangle.x + (rectangle.width - static_cast<float>(text_width)) * 0.5F);
    const int text_y = round_to_int(
        rectangle.y +
        (rectangle.height - static_cast<float>(font_ascent + font_descent)) * 0.5F +
        static_cast<float>(font_ascent));

    XSetForeground(m_display, m_graphics_context, color);
    XDrawString(
        m_display,
        drawable,
        m_graphics_context,
        text_x,
        text_y,
        text.data(),
        static_cast<int>(text.size()));
}

void X11ChromeRenderer::draw_text(
    Drawable drawable,
    std::string_view text,
    const UI::Rect& rectangle,
    float left_padding,
    unsigned long color) const
{
    if (text.empty() || rectangle.is_empty())
    {
        return;
    }

    const int font_ascent = m_font != nullptr ? m_font->ascent : 8;
    const int font_descent = m_font != nullptr ? m_font->descent : 2;
    const int text_x = round_to_int(rectangle.x + left_padding);
    const int text_y = round_to_int(
        rectangle.y +
        (rectangle.height - static_cast<float>(font_ascent + font_descent)) * 0.5F +
        static_cast<float>(font_ascent));

    XSetForeground(m_display, m_graphics_context, color);
    XDrawString(
        m_display,
        drawable,
        m_graphics_context,
        text_x,
        text_y,
        text.data(),
        static_cast<int>(text.size()));
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

    const PopupMenuGeometry geometry = calculate_popup_geometry(chrome_layout, menu_index);
    if (geometry.bounds.is_empty())
    {
        return;
    }

    fill_rectangle(drawable, geometry.bounds, m_colors.popup_background);
    draw_rectangle(drawable, geometry.bounds, m_colors.popup_border);

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

        const CommandPresentationState state = item.command_id.empty()
            ? CommandPresentationState{}
            : (command_state_query_callback
                    ? command_state_query_callback(item.command_id)
                    : CommandPresentationState{true, false});
        if (interaction_state.hovered_popup_item_index == item_index && state.enabled)
        {
            fill_rectangle(drawable, item_bounds, m_colors.hover);
        }

        draw_text(
            drawable,
            item.label,
            item_bounds,
            26.0F * m_dpi_scale,
            state.enabled ? m_colors.text_primary : m_colors.text_secondary);

        if (state.checked)
        {
            XSetForeground(m_display, m_graphics_context, m_colors.text_primary);
            const int check_x = round_to_int(item_bounds.x + 11.0F * m_dpi_scale);
            const int check_y = round_to_int(item_bounds.y + item_bounds.height * 0.5F);
            XDrawLine(m_display, drawable, m_graphics_context, check_x, check_y, check_x + 3, check_y + 3);
            XDrawLine(m_display, drawable, m_graphics_context, check_x + 3, check_y + 3, check_x + 8, check_y - 3);
        }
    }
}

} // namespace Zenvra::Platform::X11::Components
