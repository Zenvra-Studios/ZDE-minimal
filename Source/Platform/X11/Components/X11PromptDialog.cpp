#include "Platform/X11/Components/X11PromptDialog.h"
#include "Utility/Fonts.h"
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <lunasvg.h>

namespace Zenvra::Platform::X11::Components {

static std::string get_clipboard_text() {
    std::string text;
    // NOLINTNEXTLINE(cert-env33-c)
    FILE* pipe = ::popen("wl-paste 2>/dev/null || xclip -o -selection clipboard 2>/dev/null || xsel -b -o 2>/dev/null", "r");
    if (pipe != nullptr) {
        std::array<char, 1024> buffer{};
        while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
            text += buffer.data();
        }
        ::pclose(pipe);
    }
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.pop_back();
    }
    return text;
}

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

    const int base_title_size = std::max(12, static_cast<int>(13.5F * m_dpi_scale));
    const int base_ui_size = std::max(10, static_cast<int>(12.0F * m_dpi_scale));
    const int base_small_size = std::max(9, static_cast<int>(11.0F * m_dpi_scale));
    const int base_editor_size = std::max(11, static_cast<int>(13.0F * m_dpi_scale));

    char pattern[256]{};
    std::snprintf(pattern, sizeof(pattern),
                  "Open Sans, Adwaita Sans, Inter, Cantarell, sans-serif:pixelsize=%d:antialias=true:hinting=true:hintstyle=hintslight",
                  base_title_size);
    m_title_font = std::make_unique<AntialiasedFont>(m_display, m_screen, pattern);

    std::snprintf(pattern, sizeof(pattern),
                  "Open Sans, Adwaita Sans, Inter, Cantarell, sans-serif:pixelsize=%d:antialias=true:hinting=true:hintstyle=hintslight",
                  base_ui_size);
    m_ui_font = std::make_unique<AntialiasedFont>(m_display, m_screen, pattern);

    std::snprintf(pattern, sizeof(pattern),
                  "Hack, JetBrainsMono Nerd Font, JetBrains Mono, monospace:pixelsize=%d:antialias=true:hinting=true:hintstyle=hintslight",
                  base_editor_size);
    m_editor_font = std::make_unique<AntialiasedFont>(m_display, m_screen, pattern);

    std::snprintf(pattern, sizeof(pattern),
                  "Open Sans, Adwaita Sans, Inter, Cantarell, sans-serif:pixelsize=%d:antialias=true:hinting=true:hintstyle=hintslight",
                  base_small_size);
    m_small_font = std::make_unique<AntialiasedFont>(m_display, m_screen, pattern);

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
                             ZPixmap, 0, x11_data, size, size, 32, size * 4);
        if (image) {
            m_svg_cache[cache_key] = image;
        } else {
            std::free(x11_data);
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

    m_win_x = root_x + (parent_attrs.width - width) / 2;
    m_win_y = root_y + (parent_attrs.height - height) / 2;

    const int screen_w = DisplayWidth(m_display, m_screen);
    const int screen_h = DisplayHeight(m_display, m_screen);
    m_win_x = std::clamp(m_win_x, 0, std::max(screen_w - width, 0));
    m_win_y = std::clamp(m_win_y, 0, std::max(screen_h - height, 0));

    XSetWindowAttributes attrs{};
    attrs.override_redirect = True;
    attrs.background_pixel = alloc_rgb(m_display, m_screen, 30, 31, 36);
    attrs.save_under = True;
    attrs.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask |
                       PointerMotionMask | KeyPressMask | LeaveWindowMask |
                       FocusChangeMask;

    m_window = XCreateWindow(
        m_display, RootWindow(m_display, m_screen),
        m_win_x, m_win_y, static_cast<unsigned int>(width), static_cast<unsigned int>(height),
        0, DefaultDepth(m_display, m_screen), InputOutput,
        DefaultVisual(m_display, m_screen),
        CWOverrideRedirect | CWBackPixel | CWSaveUnder | CWEventMask,
        &attrs);

    // Set transient for parent window
    if (parent != 0) {
        XSetTransientForHint(m_display, m_window, parent);
    }

    // Set borderless floating window via Motif WM hints
    struct MotifHints {
        unsigned long flags;
        unsigned long functions;
        unsigned long decorations;
        long inputMode;
        unsigned long status;
    };
    Atom motif_atom = XInternAtom(m_display, "_MOTIF_WM_HINTS", False);
    MotifHints hints{
        .flags = (1UL << 1U), // MWM_HINTS_DECORATIONS
        .functions = 0,
        .decorations = 0,     // 0 = borderless, compositor provides drop shadow
        .inputMode = 0,
        .status = 0
    };
    XChangeProperty(m_display, m_window, motif_atom, motif_atom, 32, PropModeReplace,
                    reinterpret_cast<const unsigned char*>(&hints), 5);

    // Set Window Type to Dialog
    Atom net_wm_type = XInternAtom(m_display, "_NET_WM_WINDOW_TYPE", False);
    Atom net_wm_type_dialog = XInternAtom(m_display, "_NET_WM_WINDOW_TYPE_DIALOG", False);
    XChangeProperty(m_display, m_window, net_wm_type, XA_ATOM, 32, PropModeReplace,
                    reinterpret_cast<const unsigned char*>(&net_wm_type_dialog), 1);

    // Set Dialog Title for Window Manager / task switcher
    XStoreName(m_display, m_window, m_title.c_str());

    m_back_buffer = XCreatePixmap(
        m_display, m_window, static_cast<unsigned int>(width),
        static_cast<unsigned int>(height),
        static_cast<unsigned int>(DefaultDepth(m_display, m_screen)));

    m_gc = XCreateGC(m_display, m_window, 0, nullptr);

    // Calculate interactive rects (matching macOS Cocoa floating card)
    const float scale = m_dpi_scale;
    m_titlebar_rect = {0.0F, 0.0F, static_cast<float>(width), static_cast<float>(height)};
    m_close_btn_rect = {};

    if (m_mode == PromptDialogMode::ConfirmDelete) {
        const float btn_w = 75.0F * scale;
        const float btn_h = 26.0F * scale;
        const float btn_y = static_cast<float>(height) - 36.0F * scale;
        const float cancel_x = static_cast<float>(width) * 0.5F - 85.0F * scale;
        const float ok_x = static_cast<float>(width) * 0.5F + 10.0F * scale;

        m_cancel_btn_rect = {cancel_x, btn_y, btn_w, btn_h};
        m_ok_btn_rect = {ok_x, btn_y, btn_w, btn_h};
        m_input_rect = {};
    } else {
        m_input_rect = {22.0F * scale, 46.0F * scale, static_cast<float>(width) - 44.0F * scale, 32.0F * scale};
        m_cancel_btn_rect = {};
        m_ok_btn_rect = {};
    }

    m_open = true;
    m_dragging = false;
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
    const std::string dir_name = target_dir.empty() ? "Project" : target_dir.filename().string();
    m_title = "New File - " + dir_name;
    m_subtitle.clear();
    m_placeholder = "Name";
    m_confirm_label = "Create";
    m_text.clear();
    m_on_confirm_string = std::move(on_confirm);
    m_on_confirm_void = nullptr;

    const int width = static_cast<int>(360.0F * m_dpi_scale);
    const int height = static_cast<int>(96.0F * m_dpi_scale);
    layout_and_create_window(parent, width, height);
    return true;
}

bool X11PromptDialog::open_new_folder(Window parent, const std::filesystem::path& target_dir,
                                      std::function<void(const std::string&)> on_confirm) {
    m_mode = PromptDialogMode::NewFolder;
    const std::string dir_name = target_dir.empty() ? "Project" : target_dir.filename().string();
    m_title = "New Folder - " + dir_name;
    m_subtitle.clear();
    m_placeholder = "Name";
    m_confirm_label = "Create";
    m_text.clear();
    m_on_confirm_string = std::move(on_confirm);
    m_on_confirm_void = nullptr;

    const int width = static_cast<int>(360.0F * m_dpi_scale);
    const int height = static_cast<int>(96.0F * m_dpi_scale);
    layout_and_create_window(parent, width, height);
    return true;
}

bool X11PromptDialog::open_rename(Window parent, const std::filesystem::path& item_path,
                                  std::function<void(const std::string&)> on_confirm) {
    m_mode = PromptDialogMode::Rename;
    m_title = "Rename - " + item_path.filename().string();
    m_subtitle.clear();
    m_placeholder = item_path.filename().string();
    m_confirm_label = "Rename";
    m_text = item_path.filename().string();
    m_on_confirm_string = std::move(on_confirm);
    m_on_confirm_void = nullptr;

    const int width = static_cast<int>(360.0F * m_dpi_scale);
    const int height = static_cast<int>(96.0F * m_dpi_scale);
    layout_and_create_window(parent, width, height);
    return true;
}

bool X11PromptDialog::open_delete(Window parent, const std::filesystem::path& item_path,
                                  std::function<void()> on_confirm) {
    m_mode = PromptDialogMode::ConfirmDelete;
    m_title = "Delete " + item_path.filename().string() + "?";
    m_subtitle = "This action cannot be undone.";
    m_placeholder.clear();
    m_confirm_label = "Delete";
    m_text.clear();
    m_on_confirm_string = nullptr;
    m_on_confirm_void = std::move(on_confirm);

    const int width = static_cast<int>(360.0F * m_dpi_scale);
    const int height = static_cast<int>(110.0F * m_dpi_scale);
    layout_and_create_window(parent, width, height);
    return true;
}

bool X11PromptDialog::open_clone_repository(Window parent,
                                             std::function<void(const std::string&)> on_confirm) {
    m_mode = PromptDialogMode::CloneRepository;
    m_title = "Clone Repository";
    m_subtitle.clear();
    m_placeholder = "https://github.com/user/repo.git";
    m_confirm_label = "Clone";
    m_text.clear();
    m_icon_path = "vscode-codicons/icons/repo.svg";
    m_on_confirm_string = std::move(on_confirm);
    m_on_confirm_void = nullptr;

    const int width = static_cast<int>(480.0F * m_dpi_scale);
    const int height = static_cast<int>(96.0F * m_dpi_scale);
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
        m_dragging = false;
    }
}

void X11PromptDialog::shutdown() {
    close();
    m_title_font.reset();
    m_ui_font.reset();
    m_editor_font.reset();
    m_small_font.reset();
    for (auto& [k, img] : m_svg_cache) {
        if (img) {
            XDestroyImage(img);
        }
    }
    m_svg_cache.clear();
    m_display = nullptr;
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
    const unsigned long bg_col = alloc_rgb(m_display, m_screen, 30, 31, 36);     // #1e1f24 macOS deep dark
    const unsigned long border_col = alloc_rgb(m_display, m_screen, 48, 50, 55); // #303237 subtle border

    // 1. Fill entire card background
    XSetForeground(m_display, m_gc, bg_col);
    XFillRectangle(m_display, m_back_buffer, m_gc, 0, 0,
                   static_cast<unsigned int>(m_width), static_cast<unsigned int>(m_height));

    // 2. Centered Bold Title (matching macOS Cocoa)
    if (m_title_font && m_title_font->isValid()) {
        const int title_w = m_title_font->getTextWidth(m_title);
        const int title_x = (m_width - title_w) / 2;
        const int title_y = static_cast<int>(16.0F * scale) + m_title_font->getAscent();
        m_title_font->drawString(m_back_buffer, "#ebeef2", title_x, title_y, m_title);
    }

    if (m_mode == PromptDialogMode::ConfirmDelete) {
        // Subtitle (centered)
        if (m_small_font && m_small_font->isValid() && !m_subtitle.empty()) {
            const int sub_w = m_small_font->getTextWidth(m_subtitle);
            const int sub_x = (m_width - sub_w) / 2;
            const int sub_y = static_cast<int>(42.0F * scale) + m_small_font->getAscent();
            m_small_font->drawString(m_back_buffer, "#a0a0aa", sub_x, sub_y, m_subtitle);
        }

        // Cancel Button (rounded 6px style)
        const unsigned long cancel_bg = m_cancel_hovered
                                            ? alloc_rgb(m_display, m_screen, 58, 61, 68)
                                            : alloc_rgb(m_display, m_screen, 45, 47, 52);
        XSetForeground(m_display, m_gc, cancel_bg);
        XFillRectangle(m_display, m_back_buffer, m_gc,
                       static_cast<int>(m_cancel_btn_rect.x),
                       static_cast<int>(m_cancel_btn_rect.y),
                       static_cast<unsigned int>(m_cancel_btn_rect.width),
                       static_cast<unsigned int>(m_cancel_btn_rect.height));
        if (m_ui_font && m_ui_font->isValid()) {
            const int tw = m_ui_font->getTextWidth("Cancel");
            const int tx = static_cast<int>(m_cancel_btn_rect.x + (m_cancel_btn_rect.width - tw) * 0.5F);
            const int ty = static_cast<int>(m_cancel_btn_rect.y + (m_cancel_btn_rect.height * 0.5F) + m_ui_font->getAscent() * 0.35F);
            m_ui_font->drawString(m_back_buffer, "#cccccc", tx, ty, "Cancel");
        }

        // Delete Button (rounded 6px style)
        const unsigned long delete_bg = m_ok_hovered
                                            ? alloc_rgb(m_display, m_screen, 235, 65, 70)
                                            : alloc_rgb(m_display, m_screen, 218, 45, 50);
        XSetForeground(m_display, m_gc, delete_bg);
        XFillRectangle(m_display, m_back_buffer, m_gc,
                       static_cast<int>(m_ok_btn_rect.x),
                       static_cast<int>(m_ok_btn_rect.y),
                       static_cast<unsigned int>(m_ok_btn_rect.width),
                       static_cast<unsigned int>(m_ok_btn_rect.height));
        if (m_ui_font && m_ui_font->isValid()) {
            const int tw = m_ui_font->getTextWidth("Delete");
            const int tx = static_cast<int>(m_ok_btn_rect.x + (m_ok_btn_rect.width - tw) * 0.5F);
            const int ty = static_cast<int>(m_ok_btn_rect.y + (m_ok_btn_rect.height * 0.5F) + m_ui_font->getAscent() * 0.35F);
            m_ui_font->drawString(m_back_buffer, "#ffffff", tx, ty, "Delete");
        }
    } else {
        // 3. Pure Transparent Borderless Input Field (matching macOS Cocoa)
        const int input_x = static_cast<int>(22.0F * scale + 4.0F * scale);
        const int input_y = static_cast<int>(46.0F * scale + 7.0F * scale) + (m_ui_font ? m_ui_font->getAscent() : 12);

        if (m_text.empty()) {
            if (m_ui_font && m_ui_font->isValid()) {
                m_ui_font->drawString(m_back_buffer, "#6e7382", input_x, input_y, m_placeholder);
            }
        } else {
            if (m_ui_font && m_ui_font->isValid()) {
                m_ui_font->drawString(m_back_buffer, "#f0f2f5", input_x, input_y, m_text);
            }
        }

        // Blinking Caret
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_last_input_time).count();
        const bool show_caret = (elapsed_ms % 1000) < 530;

        if (show_caret) {
            int text_w = 0;
            if (m_ui_font && m_ui_font->isValid() && !m_text.empty()) {
                text_w = m_ui_font->getTextWidth(m_text);
            }
            const int caret_x = input_x + text_w;
            XSetForeground(m_display, m_gc, alloc_rgb(m_display, m_screen, 255, 255, 255));
            const int caret_top = static_cast<int>(46.0F * scale + 6.0F * scale);
            const int caret_bottom = static_cast<int>(46.0F * scale + 26.0F * scale);
            XDrawLine(m_display, m_back_buffer, m_gc, caret_x, caret_top, caret_x, caret_bottom);
        }
    }

    // 4. Subtle 1px outer frame
    XSetForeground(m_display, m_gc, border_col);
    XDrawRectangle(m_display, m_back_buffer, m_gc, 0, 0,
                   static_cast<unsigned int>(m_width - 1), static_cast<unsigned int>(m_height - 1));

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
        if (m_dragging) {
            const int dx = event.xmotion.x_root - m_drag_start_root_x;
            const int dy = event.xmotion.y_root - m_drag_start_root_y;
            m_win_x = m_drag_start_win_x + dx;
            m_win_y = m_drag_start_win_y + dy;
            XMoveWindow(m_display, m_window, m_win_x, m_win_y);
            return true;
        }

        if (m_mode == PromptDialogMode::ConfirmDelete) {
            const float mx = static_cast<float>(event.xmotion.x);
            const float my = static_cast<float>(event.xmotion.y);

            const bool new_cancel_h = m_cancel_btn_rect.contains(mx, my);
            const bool new_ok_h = m_ok_btn_rect.contains(mx, my);

            if (new_cancel_h != m_cancel_hovered || new_ok_h != m_ok_hovered) {
                m_cancel_hovered = new_cancel_h;
                m_ok_hovered = new_ok_h;
                render();
            }
        }
        return true;
    }

    case ButtonPress: {
        m_last_input_time = std::chrono::steady_clock::now();
        const float bx = static_cast<float>(event.xbutton.x);
        const float by = static_cast<float>(event.xbutton.y);

        if (m_mode == PromptDialogMode::ConfirmDelete) {
            if (m_cancel_btn_rect.contains(bx, by)) {
                close();
                return true;
            }
            if (m_ok_btn_rect.contains(bx, by)) {
                submit();
                return true;
            }
        }

        // Entire background is draggable just like macOS movableByWindowBackground!
        m_dragging = true;
        m_drag_start_root_x = event.xbutton.x_root;
        m_drag_start_root_y = event.xbutton.y_root;
        m_drag_start_win_x = m_win_x;
        m_drag_start_win_y = m_win_y;
        return true;
    }

    case ButtonRelease: {
        m_dragging = false;
        return true;
    }

    case KeyPress: {
        m_last_input_time = std::chrono::steady_clock::now();
        KeySym sym = XLookupKeysym(const_cast<XKeyEvent*>(&event.xkey), 0);
        const bool ctrl_pressed = (event.xkey.state & ControlMask) != 0;

        if (sym == XK_Escape) {
            close();
            return true;
        }
        if (sym == XK_Return || sym == XK_KP_Enter) {
            submit();
            return true;
        }
        if (ctrl_pressed && (sym == XK_v || sym == XK_V)) {
            std::string clip = get_clipboard_text();
            if (!clip.empty()) {
                m_text.append(clip);
                render();
            }
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
