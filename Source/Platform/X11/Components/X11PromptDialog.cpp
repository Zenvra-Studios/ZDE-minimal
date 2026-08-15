#include "Platform/X11/Components/X11PromptDialog.h"
#include "Utility/Fonts.h"
#include <X11/Xutil.h>
#include <algorithm>
#include <cctype>
#include <lunasvg.h>

namespace Zenvra::Platform::X11::Components {

static unsigned long alloc_rgb(Display* dpy, int scr, unsigned char r, unsigned char g, unsigned char b) {
    XColor c{};
    c.red = static_cast<unsigned short>(r * 257U);
    c.green = static_cast<unsigned short>(g * 257U);
    c.blue = static_cast<unsigned short>(b * 257U);
    c.flags = DoRed | DoGreen | DoBlue;
    if (XAllocColor(dpy, DefaultColormap(dpy, scr), &c) == 0) {
        return BlackPixel(dpy, scr);
    }
    return c.pixel;
}

X11PromptDialog::X11PromptDialog() = default;

X11PromptDialog::~X11PromptDialog() {
    close();
    for (auto& [k, img] : m_svg_cache) {
        if (img) {
            XDestroyImage(img);
        }
    }
    m_svg_cache.clear();
}

bool X11PromptDialog::initialize(Display* display, int screen, float dpi_scale,
                                const std::filesystem::path& icon_asset_root) {
    m_display = display;
    m_screen = screen;
    m_dpi_scale = std::max(dpi_scale, 1.0F);
    m_icon_asset_root = icon_asset_root;

    const int base_title_size = std::max(8, static_cast<int>(8.5F * m_dpi_scale));
    const int base_ui_size = std::max(9, static_cast<int>(9.0F * m_dpi_scale));
    const int base_small_size = std::max(8, static_cast<int>(8.0F * m_dpi_scale));

    m_title_font = std::make_unique<AntialiasedFont>(
        m_display, m_screen, "sans-" + std::to_string(base_title_size));
    m_ui_font = std::make_unique<AntialiasedFont>(
        m_display, m_screen, "sans-" + std::to_string(base_ui_size));
    m_editor_font = std::make_unique<AntialiasedFont>(
        m_display, m_screen, "monospace-" + std::to_string(base_ui_size));
    m_small_font = std::make_unique<AntialiasedFont>(
        m_display, m_screen, "sans-" + std::to_string(base_small_size));

    return true;
}

void X11PromptDialog::draw_icon(Drawable drawable, const std::string& path, int x, int y, int size,
                               uint8_t bg_r, uint8_t bg_g, uint8_t bg_b) {
    if (size <= 0 || m_display == nullptr || path.empty()) {
        return;
    }

    std::filesystem::path resolved_path{path};
    if (!std::filesystem::exists(resolved_path)) {
        if (!m_icon_asset_root.empty()) {
            if (std::filesystem::exists(m_icon_asset_root / resolved_path)) {
                resolved_path = m_icon_asset_root / resolved_path;
            } else if (resolved_path.string().starts_with("Assets/icons/")) {
                const auto sub = resolved_path.string().substr(13);
                if (std::filesystem::exists(m_icon_asset_root / sub)) {
                    resolved_path = m_icon_asset_root / sub;
                }
            } else if (resolved_path.string().starts_with("Assets/")) {
                const auto sub = resolved_path.string().substr(7);
                if (std::filesystem::exists(m_icon_asset_root.parent_path() / "Assets" / sub)) {
                    resolved_path = m_icon_asset_root.parent_path() / "Assets" / sub;
                }
            }
        }
    }
    if (!std::filesystem::exists(resolved_path)) {
        const auto cwd_p = std::filesystem::current_path() / path;
        if (std::filesystem::exists(cwd_p)) {
            resolved_path = cwd_p;
        }
    }

    if (!std::filesystem::exists(resolved_path)) {
        return;
    }

    const std::string cache_key = resolved_path.string() + "@" + std::to_string(size) + "_" +
                                  std::to_string(bg_r) + "_" + std::to_string(bg_g) + "_" + std::to_string(bg_b);
    XImage* image = nullptr;
    auto it = m_svg_cache.find(cache_key);
    if (it != m_svg_cache.end()) {
        image = it->second;
    } else {
        auto document = lunasvg::Document::loadFromFile(resolved_path.string());
        if (!document) {
            return;
        }
        auto bitmap = document->renderToBitmap(static_cast<std::uint32_t>(size), static_cast<std::uint32_t>(size));
        if (bitmap.isNull()) {
            return;
        }

        char* x11_data = static_cast<char*>(std::malloc(size * size * 4));
        if (!x11_data) {
            return;
        }

        const uint32_t* src = reinterpret_cast<const uint32_t*>(bitmap.data());
        uint32_t* dst = reinterpret_cast<uint32_t*>(x11_data);

        for (int i = 0; i < size * size; ++i) {
            uint32_t pixel = src[i];
            uint32_t a = (pixel >> 24) & 0xFF;
            uint32_t source_r = (pixel >> 16) & 0xFF;
            uint32_t source_g = (pixel >> 8) & 0xFF;
            uint32_t source_b = pixel & 0xFF;

            uint32_t out_r = source_r + (static_cast<uint32_t>(bg_r) * (255 - a)) / 255;
            uint32_t out_g = source_g + (static_cast<uint32_t>(bg_g) * (255 - a)) / 255;
            uint32_t out_b = source_b + (static_cast<uint32_t>(bg_b) * (255 - a)) / 255;

            dst[i] = (out_r << 16) | (out_g << 8) | out_b;
        }

        image = XCreateImage(m_display, DefaultVisual(m_display, m_screen),
                             static_cast<unsigned int>(DefaultDepth(m_display, m_screen)),
                             ZPixmap, 0, x11_data, size, size, 32, 0);
        if (image) {
            m_svg_cache[cache_key] = image;
        }
    }

    if (image) {
        XPutImage(m_display, drawable, m_gc, image, 0, 0, x, y, size, size);
    }
}

void X11PromptDialog::layout_and_create_window(Window parent, int width, int height) {
    close();

    m_parent_window = parent;
    m_width = width;
    m_height = height;

    XWindowAttributes parent_attrs{};
    XGetWindowAttributes(m_display, parent, &parent_attrs);

    int root_x = 0;
    int root_y = 0;
    Window child = 0;
    XTranslateCoordinates(m_display, parent, RootWindow(m_display, m_screen),
                         0, 0, &root_x, &root_y, &child);

    int win_x = root_x + (parent_attrs.width - width) / 2;
    int win_y = root_y + (parent_attrs.height - height) / 2;

    const int screen_w = DisplayWidth(m_display, m_screen);
    const int screen_h = DisplayHeight(m_display, m_screen);
    win_x = std::clamp(win_x, 0, std::max(screen_w - width, 0));
    win_y = std::clamp(win_y, 0, std::max(screen_h - height, 0));

    XSetWindowAttributes attrs{};
    attrs.override_redirect = True;
    attrs.background_pixel = alloc_rgb(m_display, m_screen, 30, 30, 34);
    attrs.save_under = True;
    attrs.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask |
                       PointerMotionMask | KeyPressMask | LeaveWindowMask |
                       FocusChangeMask;

    m_window = XCreateWindow(
        m_display, RootWindow(m_display, m_screen),
        win_x, win_y, static_cast<unsigned int>(width), static_cast<unsigned int>(height),
        0, DefaultDepth(m_display, m_screen), InputOutput,
        DefaultVisual(m_display, m_screen),
        CWOverrideRedirect | CWBackPixel | CWSaveUnder | CWEventMask,
        &attrs);

    m_back_buffer = XCreatePixmap(
        m_display, m_window, static_cast<unsigned int>(width),
        static_cast<unsigned int>(height),
        static_cast<unsigned int>(DefaultDepth(m_display, m_screen)));

    m_gc = XCreateGC(m_display, m_window, 0, nullptr);

    // Calculate interactive rects
    const float scale = m_dpi_scale;
    m_close_btn_rect = {static_cast<float>(width) - 46.0F * scale, 0.0F, 46.0F * scale, 34.0F * scale};
    m_input_rect = {24.0F * scale, 76.0F * scale, static_cast<float>(width) - 48.0F * scale, 30.0F * scale};
    m_cancel_btn_rect = {static_cast<float>(width) - 176.0F * scale, static_cast<float>(height) - 44.0F * scale, 76.0F * scale, 28.0F * scale};
    m_ok_btn_rect = {static_cast<float>(width) - 90.0F * scale, static_cast<float>(height) - 44.0F * scale, 76.0F * scale, 28.0F * scale};

    m_open = true;
    m_close_hovered = false;
    m_ok_hovered = false;
    m_cancel_hovered = false;

    XMapRaised(m_display, m_window);
    XSetInputFocus(m_display, m_window, RevertToParent, CurrentTime);
    render();
}

bool X11PromptDialog::open_new_file(Window parent, const std::filesystem::path& target_dir,
                                    std::function<void(const std::string&)> on_confirm) {
    m_mode = PromptDialogMode::NewFile;
    m_icon_path = "Assets/icons/material-icon-theme/document.svg";
    m_title = "New File";
    m_subtitle = "Target: " + (target_dir.empty() ? "." : target_dir.filename().string()) + "/";
    m_placeholder = "File name (e.g. main.cpp, utils.h)";
    m_confirm_label = "Create";
    m_text.clear();
    m_on_confirm_string = std::move(on_confirm);
    m_on_confirm_void = nullptr;

    const int width = static_cast<int>(480.0F * m_dpi_scale);
    const int height = static_cast<int>(180.0F * m_dpi_scale);
    layout_and_create_window(parent, width, height);
    return true;
}

bool X11PromptDialog::open_new_folder(Window parent, const std::filesystem::path& target_dir,
                                      std::function<void(const std::string&)> on_confirm) {
    m_mode = PromptDialogMode::NewFolder;
    m_icon_path = "Assets/icons/material-icon-theme/folder-custom.svg";
    m_title = "New Folder";
    m_subtitle = "Target: " + (target_dir.empty() ? "." : target_dir.filename().string()) + "/";
    m_placeholder = "Folder name";
    m_confirm_label = "Create";
    m_text.clear();
    m_on_confirm_string = std::move(on_confirm);
    m_on_confirm_void = nullptr;

    const int width = static_cast<int>(480.0F * m_dpi_scale);
    const int height = static_cast<int>(180.0F * m_dpi_scale);
    layout_and_create_window(parent, width, height);
    return true;
}

bool X11PromptDialog::open_rename(Window parent, const std::filesystem::path& item_path,
                                  std::function<void(const std::string&)> on_confirm) {
    m_mode = PromptDialogMode::Rename;
    m_icon_path = "Assets/icons/material-icon-theme/document.svg";
    m_title = "Rename Item";
    m_subtitle = "Current: " + item_path.filename().string();
    m_placeholder = "New name";
    m_confirm_label = "Rename";
    m_text = item_path.filename().string();
    m_on_confirm_string = std::move(on_confirm);
    m_on_confirm_void = nullptr;

    const int width = static_cast<int>(480.0F * m_dpi_scale);
    const int height = static_cast<int>(180.0F * m_dpi_scale);
    layout_and_create_window(parent, width, height);
    return true;
}

bool X11PromptDialog::open_delete(Window parent, const std::filesystem::path& item_path,
                                  std::function<void()> on_confirm) {
    m_mode = PromptDialogMode::ConfirmDelete;
    m_icon_path = "Assets/icons/material-icon-theme/folder-trash.svg";
    m_title = "Delete Item";
    m_subtitle = "Item: " + item_path.filename().string();
    m_placeholder.clear();
    m_confirm_label = "Delete";
    m_text.clear();
    m_on_confirm_string = nullptr;
    m_on_confirm_void = std::move(on_confirm);

    const int width = static_cast<int>(480.0F * m_dpi_scale);
    const int height = static_cast<int>(150.0F * m_dpi_scale);
    layout_and_create_window(parent, width, height);
    return true;
}

void X11PromptDialog::close() {
    if (m_open && m_display != nullptr) {
        if (m_window != 0) {
            XDestroyWindow(m_display, m_window);
            m_window = 0;
        }
        if (m_back_buffer != 0) {
            XFreePixmap(m_display, m_back_buffer);
            m_back_buffer = 0;
        }
        if (m_gc != nullptr) {
            XFreeGC(m_display, m_gc);
            m_gc = nullptr;
        }
        m_open = false;
    }
}

void X11PromptDialog::submit() {
    if (m_mode == PromptDialogMode::ConfirmDelete) {
        auto cb = std::move(m_on_confirm_void);
        close();
        if (cb) {
            cb();
        }
    } else {
        if (m_text.empty()) {
            return;
        }
        std::string res = m_text;
        auto cb = std::move(m_on_confirm_string);
        close();
        if (cb) {
            cb(res);
        }
    }
}

void X11PromptDialog::render() {
    if (!m_open || m_display == nullptr || m_back_buffer == 0) {
        return;
    }

    const float scale = m_dpi_scale;
    const unsigned long bg_col = alloc_rgb(m_display, m_screen, 31, 31, 31);
    const unsigned long titlebar_bg = alloc_rgb(m_display, m_screen, 29, 30, 33);
    const unsigned long border_col = alloc_rgb(m_display, m_screen, 48, 50, 55);
    const unsigned long titlebar_border = alloc_rgb(m_display, m_screen, 48, 50, 55);
    const unsigned long input_bg = alloc_rgb(m_display, m_screen, 24, 24, 26);
    const unsigned long input_border = alloc_rgb(m_display, m_screen, 0, 122, 204);

    // 1. Fill dialog background
    XSetForeground(m_display, m_gc, bg_col);
    XFillRectangle(m_display, m_back_buffer, m_gc, 0, 0,
                   static_cast<unsigned int>(m_width), static_cast<unsigned int>(m_height));

    // 2. Draw dialog outer border
    XSetForeground(m_display, m_gc, border_col);
    XDrawRectangle(m_display, m_back_buffer, m_gc, 0, 0,
                   static_cast<unsigned int>(m_width - 1), static_cast<unsigned int>(m_height - 1));

    // 3. Titlebar background & separator
    const int titlebar_h = static_cast<int>(34.0F * scale);
    XSetForeground(m_display, m_gc, titlebar_bg);
    XFillRectangle(m_display, m_back_buffer, m_gc, 0, 0,
                   static_cast<unsigned int>(m_width), static_cast<unsigned int>(titlebar_h));
    XSetForeground(m_display, m_gc, titlebar_border);
    XDrawLine(m_display, m_back_buffer, m_gc, 0, titlebar_h - 1, m_width, titlebar_h - 1);

    // Titlebar Material Icon
    if (!m_icon_path.empty()) {
        draw_icon(m_back_buffer, m_icon_path,
                  static_cast<int>(12.0F * scale), static_cast<int>(9.0F * scale),
                  static_cast<int>(16.0F * scale), 29, 30, 33);
    }

    // 4. Title in Titlebar
    if (m_title_font && m_title_font->isValid()) {
        m_title_font->drawString(m_back_buffer, "#cccccc",
                                static_cast<int>(36.0F * scale),
                                static_cast<int>(21.0F * scale),
                                m_title);
    }
    if (m_small_font && m_small_font->isValid() && !m_subtitle.empty()) {
        m_small_font->drawString(m_back_buffer, "#888894",
                                static_cast<int>(24.0F * scale),
                                static_cast<int>(54.0F * scale),
                                m_subtitle);
    }

    // 5. Close (X) button with crisp diagonal cross (matching main chrome)
    if (m_close_hovered) {
        XSetForeground(m_display, m_gc, alloc_rgb(m_display, m_screen, 232, 17, 35));
        XFillRectangle(m_display, m_back_buffer, m_gc,
                       static_cast<int>(m_close_btn_rect.x),
                       static_cast<int>(m_close_btn_rect.y),
                       static_cast<unsigned int>(m_close_btn_rect.width),
                       static_cast<unsigned int>(m_close_btn_rect.height));
    }
    const int close_cx = static_cast<int>(m_close_btn_rect.x + m_close_btn_rect.width * 0.5F);
    const int close_cy = static_cast<int>(m_close_btn_rect.y + m_close_btn_rect.height * 0.5F);
    const int close_half = std::max(static_cast<int>(5.0F * scale), 4);
    const int line_width = std::max(static_cast<int>(scale), 1);

    XSetForeground(m_display, m_gc, m_close_hovered ? alloc_rgb(m_display, m_screen, 255, 255, 255)
                                                    : alloc_rgb(m_display, m_screen, 204, 204, 204));
    XSetLineAttributes(m_display, m_gc, line_width, LineSolid, CapProjecting, JoinMiter);
    XDrawLine(m_display, m_back_buffer, m_gc, close_cx - close_half, close_cy - close_half,
              close_cx + close_half, close_cy + close_half);
    XDrawLine(m_display, m_back_buffer, m_gc, close_cx - close_half, close_cy + close_half,
              close_cx + close_half, close_cy - close_half);
    XSetLineAttributes(m_display, m_gc, 1, LineSolid, CapButt, JoinMiter);

    // 5. Input or Delete Warning
    if (m_mode != PromptDialogMode::ConfirmDelete) {
        XSetForeground(m_display, m_gc, input_bg);
        XFillRectangle(m_display, m_back_buffer, m_gc,
                       static_cast<int>(m_input_rect.x),
                       static_cast<int>(m_input_rect.y),
                       static_cast<unsigned int>(m_input_rect.width),
                       static_cast<unsigned int>(m_input_rect.height));

        XSetForeground(m_display, m_gc, input_border);
        XDrawRectangle(m_display, m_back_buffer, m_gc,
                       static_cast<int>(m_input_rect.x),
                       static_cast<int>(m_input_rect.y),
                       static_cast<unsigned int>(m_input_rect.width),
                       static_cast<unsigned int>(m_input_rect.height));

        const int text_x = static_cast<int>(m_input_rect.x + 8.0F * scale);
        const int text_y = static_cast<int>(m_input_rect.y + 20.0F * scale);

        if (m_text.empty()) {
            if (m_ui_font && m_ui_font->isValid()) {
                m_ui_font->drawString(m_back_buffer, "#686874", text_x, text_y, m_placeholder);
            }
        } else {
            if (m_editor_font && m_editor_font->isValid()) {
                m_editor_font->drawString(m_back_buffer, "#f0f0f5", text_x, text_y, m_text);
            }
        }

        // Draw Caret
        int text_w = 0;
        if (m_editor_font && m_editor_font->isValid() && !m_text.empty()) {
            text_w = m_editor_font->getTextWidth(m_text);
        }
        const int caret_x = text_x + text_w;
        XSetForeground(m_display, m_gc, alloc_rgb(m_display, m_screen, 255, 255, 255));
        XDrawLine(m_display, m_back_buffer, m_gc,
                  caret_x, static_cast<int>(m_input_rect.y + 5.0F * scale),
                  caret_x, static_cast<int>(m_input_rect.bottom() - 5.0F * scale));
    } else {
        if (m_ui_font && m_ui_font->isValid()) {
            m_ui_font->drawString(m_back_buffer, "#dcdcdc",
                                  static_cast<int>(24.0F * scale),
                                  static_cast<int>(76.0F * scale),
                                  "Are you sure you want to delete this item? This cannot be undone.");
        }
    }

    // 6. Cancel Button
    const unsigned long cancel_bg = m_cancel_hovered
                                        ? alloc_rgb(m_display, m_screen, 55, 55, 62)
                                        : alloc_rgb(m_display, m_screen, 45, 45, 50);
    XSetForeground(m_display, m_gc, cancel_bg);
    XFillRectangle(m_display, m_back_buffer, m_gc,
                   static_cast<int>(m_cancel_btn_rect.x),
                   static_cast<int>(m_cancel_btn_rect.y),
                   static_cast<unsigned int>(m_cancel_btn_rect.width),
                   static_cast<unsigned int>(m_cancel_btn_rect.height));
    XSetForeground(m_display, m_gc, border_col);
    XDrawRectangle(m_display, m_back_buffer, m_gc,
                   static_cast<int>(m_cancel_btn_rect.x),
                   static_cast<int>(m_cancel_btn_rect.y),
                   static_cast<unsigned int>(m_cancel_btn_rect.width),
                   static_cast<unsigned int>(m_cancel_btn_rect.height));
    if (m_ui_font && m_ui_font->isValid()) {
        m_ui_font->drawString(m_back_buffer, "#e0e0e0",
                              static_cast<int>(m_cancel_btn_rect.x + 16.0F * scale),
                              static_cast<int>(m_cancel_btn_rect.y + 19.0F * scale),
                              "Cancel");
    }

    // 7. OK / Confirm Button
    unsigned long ok_bg = 0;
    if (m_mode == PromptDialogMode::ConfirmDelete) {
        ok_bg = m_ok_hovered ? alloc_rgb(m_display, m_screen, 247, 84, 100)
                             : alloc_rgb(m_display, m_screen, 232, 17, 35);
    } else {
        ok_bg = m_ok_hovered ? alloc_rgb(m_display, m_screen, 14, 99, 156)
                             : alloc_rgb(m_display, m_screen, 0, 122, 204);
    }
    XSetForeground(m_display, m_gc, ok_bg);
    XFillRectangle(m_display, m_back_buffer, m_gc,
                   static_cast<int>(m_ok_btn_rect.x),
                   static_cast<int>(m_ok_btn_rect.y),
                   static_cast<unsigned int>(m_ok_btn_rect.width),
                   static_cast<unsigned int>(m_ok_btn_rect.height));
    if (m_ui_font && m_ui_font->isValid()) {
        m_ui_font->drawString(m_back_buffer, "#ffffff",
                              static_cast<int>(m_ok_btn_rect.x + 16.0F * scale),
                              static_cast<int>(m_ok_btn_rect.y + 19.0F * scale),
                              m_confirm_label);
    }

    // Blit to screen window
    XCopyArea(m_display, m_back_buffer, m_window, m_gc,
              0, 0, static_cast<unsigned int>(m_width),
              static_cast<unsigned int>(m_height), 0, 0);
    XFlush(m_display);
}

bool X11PromptDialog::handle_event(const XEvent& event) {
    if (!m_open || event.xany.window != m_window) {
        return false;
    }

    switch (event.type) {
    case Expose:
        render();
        return true;

    case MotionNotify: {
        const float mx = static_cast<float>(event.xmotion.x);
        const float my = static_cast<float>(event.xmotion.y);

        const bool new_close_h = m_close_btn_rect.contains(mx, my);
        const bool new_cancel_h = m_cancel_btn_rect.contains(mx, my);
        const bool new_ok_h = m_ok_btn_rect.contains(mx, my);

        if (new_close_h != m_close_hovered || new_cancel_h != m_cancel_hovered ||
            new_ok_h != m_ok_hovered) {
            m_close_hovered = new_close_h;
            m_cancel_hovered = new_cancel_h;
            m_ok_hovered = new_ok_h;
            render();
        }
        return true;
    }

    case ButtonPress: {
        const float bx = static_cast<float>(event.xbutton.x);
        const float by = static_cast<float>(event.xbutton.y);

        if (m_close_btn_rect.contains(bx, by)) {
            close();
            return true;
        }
        if (m_cancel_btn_rect.contains(bx, by)) {
            close();
            return true;
        }
        if (m_ok_btn_rect.contains(bx, by)) {
            submit();
            return true;
        }
        return true;
    }

    case KeyPress: {
        KeySym sym = XLookupKeysym(const_cast<XKeyEvent*>(&event.xkey), 0);
        if (sym == XK_Escape) {
            close();
            return true;
        }
        if (sym == XK_Return || sym == XK_KP_Enter) {
            submit();
            return true;
        }
        if (sym == XK_BackSpace) {
            if (!m_text.empty()) {
                m_text.pop_back();
                render();
            }
            return true;
        }

        char buf[32]{};
        int len = XLookupString(const_cast<XKeyEvent*>(&event.xkey), buf, sizeof(buf), nullptr, nullptr);
        if (len > 0 && !std::iscntrl(static_cast<unsigned char>(buf[0]))) {
            m_text.append(buf, len);
            render();
            return true;
        }
        return true;
    }

    default:
        break;
    }

    return true;
}

} // namespace Zenvra::Platform::X11::Components
