#pragma once

#include "UI/Geometry.h"
#include <X11/Xlib.h>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include <chrono>

class AntialiasedFont;

namespace Zenvra::Platform::X11::Components {

enum class PromptDialogMode {
    NewFile,
    NewFolder,
    Rename,
    ConfirmDelete,
    CloneRepository
};

class X11PromptDialog {
public:
    X11PromptDialog();
    ~X11PromptDialog();

    X11PromptDialog(const X11PromptDialog&) = delete;
    X11PromptDialog& operator=(const X11PromptDialog&) = delete;

    [[nodiscard]] bool initialize(Display* display, int screen, float dpi_scale,
                                  const std::filesystem::path& icon_asset_root = {});

    bool open_new_file(Window parent, const std::filesystem::path& target_dir,
                       std::function<void(const std::string&)> on_confirm);
    bool open_new_folder(Window parent, const std::filesystem::path& target_dir,
                         std::function<void(const std::string&)> on_confirm);
    bool open_rename(Window parent, const std::filesystem::path& item_path,
                     std::function<void(const std::string&)> on_confirm);
    bool open_delete(Window parent, const std::filesystem::path& item_path,
                     std::function<void()> on_confirm);
    bool open_clone_repository(Window parent,
                               std::function<void(const std::string&)> on_confirm);

    void close();
    void shutdown();
    [[nodiscard]] bool is_open() const noexcept { return m_open; }
    [[nodiscard]] Window window() const noexcept { return m_window; }

    bool handle_event(const XEvent& event);
    void render();

private:
    void submit();
    void layout_and_create_window(Window parent, int width, int height);
    void draw_icon(Drawable drawable, const std::string& path, int x, int y, int size,
                   uint8_t bg_r = 24, uint8_t bg_g = 24, uint8_t bg_b = 28);

    Display* m_display = nullptr;
    int m_screen = 0;
    float m_dpi_scale = 1.0F;
    std::filesystem::path m_icon_asset_root;
    std::unordered_map<std::string, XImage*> m_svg_cache;
    std::string m_icon_path;
    Window m_parent_window = 0;
    Window m_window = 0;
    Pixmap m_back_buffer = 0;
    GC m_gc = nullptr;

    std::unique_ptr<AntialiasedFont> m_title_font;
    std::unique_ptr<AntialiasedFont> m_ui_font;
    std::unique_ptr<AntialiasedFont> m_editor_font;
    std::unique_ptr<AntialiasedFont> m_small_font;

    PromptDialogMode m_mode = PromptDialogMode::NewFile;
    bool m_open = false;
    std::string m_title;
    std::string m_subtitle;
    std::string m_placeholder;
    std::string m_confirm_label = "Create";
    std::string m_text;

    int m_width = 480;
    int m_height = 190;
    int m_win_x = 0;
    int m_win_y = 0;

    bool m_dragging = false;
    int m_drag_start_root_x = 0;
    int m_drag_start_root_y = 0;
    int m_drag_start_win_x = 0;
    int m_drag_start_win_y = 0;

    bool m_close_hovered = false;
    bool m_ok_hovered = false;
    bool m_cancel_hovered = false;

    UI::Rect m_titlebar_rect{};
    UI::Rect m_close_btn_rect{};
    UI::Rect m_input_rect{};
    UI::Rect m_ok_btn_rect{};
    UI::Rect m_cancel_btn_rect{};

    std::chrono::steady_clock::time_point m_last_input_time{std::chrono::steady_clock::now()};

    std::function<void(const std::string&)> m_on_confirm_string;
    std::function<void()> m_on_confirm_void;
};

} // namespace Zenvra::Platform::X11::Components
