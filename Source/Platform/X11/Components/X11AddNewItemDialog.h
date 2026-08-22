#pragma once

#include "UI/Geometry.h"
#include <X11/Xlib.h>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>


class AntialiasedFont;

namespace Zenvra::Platform::X11::Components {

struct ItemTemplate {
  std::string id;
  std::string name;             // e.g. "C++ File (.cpp)"
  std::string default_filename; // e.g. "Source.cpp"
  std::string extension;        // e.g. ".cpp"
  std::string category;         // e.g. "C/C++"
  std::string description; // e.g. "Creates a file containing C++ source code"
  std::string icon_path;
  std::string default_content;
};

struct TemplateCategory {
  std::string id;
  std::string name;
  std::string icon_path;
  std::vector<ItemTemplate> templates;
};

class X11AddNewItemDialog {
public:
  using CreateCallback = std::function<void(
      const std::string &filename, const std::string &initial_content)>;

  X11AddNewItemDialog();
  ~X11AddNewItemDialog();

  X11AddNewItemDialog(const X11AddNewItemDialog &) = delete;
  X11AddNewItemDialog &operator=(const X11AddNewItemDialog &) = delete;

  [[nodiscard]] bool
  initialize(Display *display, int screen, float dpi_scale,
             const std::filesystem::path &icon_asset_root = {});

  void open(Window parent_window, const std::filesystem::path &target_folder,
            const std::string &project_name, CreateCallback callback);
  void close();
  void shutdown();
  [[nodiscard]] bool is_open() const noexcept { return m_open; }
  [[nodiscard]] Window window() const noexcept { return m_window; }

  bool handle_event(const XEvent &event);
  void render();

private:
  void init_default_templates();
  void select_category(std::size_t index);
  void select_template(std::size_t index);
  void submit();
  void draw_icon(Drawable drawable, const std::string &path, int x, int y,
                 int size, uint8_t bg_r = 27, uint8_t bg_g = 27,
                 uint8_t bg_b = 30);

  Display *m_display = nullptr;
  int m_screen = 0;
  float m_dpi_scale = 1.0F;
  std::filesystem::path m_icon_asset_root;

  Window m_parent_window = 0;
  Window m_window = 0;
  Pixmap m_back_buffer = 0;
  GC m_gc = nullptr;

  std::unique_ptr<AntialiasedFont> m_title_font;
  std::unique_ptr<AntialiasedFont> m_bold_font;
  std::unique_ptr<AntialiasedFont> m_ui_font;
  std::unique_ptr<AntialiasedFont> m_small_font;

  std::filesystem::path m_target_folder;
  std::string m_project_name = "Project";
  CreateCallback m_callback;

  std::vector<TemplateCategory> m_categories;
  std::size_t m_selected_category_index = 0;
  std::size_t m_selected_template_index = 0;

  std::string m_filename_input;
  std::size_t m_caret_position = 0;

  int m_width = 820;
  int m_height = 520;
  int m_win_x = 0;
  int m_win_y = 0;

  bool m_open = false;
  bool m_close_hovered = false;
  bool m_add_hovered = false;
  bool m_cancel_hovered = false;
  std::optional<std::size_t> m_hovered_category_index;
  std::optional<std::size_t> m_hovered_template_index;
  bool m_name_input_focused = true;

  bool m_dragging_titlebar = false;
  int m_drag_start_root_x = 0;
  int m_drag_start_root_y = 0;
  int m_drag_start_win_x = 0;
  int m_drag_start_win_y = 0;

  Time m_last_click_time = 0;
  std::size_t m_last_clicked_template_index = 0;

  UI::Rect m_titlebar_rect{};
  UI::Rect m_close_btn_rect{};
  UI::Rect m_category_pane_rect{};
  std::vector<UI::Rect> m_category_item_rects;
  UI::Rect m_template_pane_rect{};
  std::vector<UI::Rect> m_template_item_rects;
  UI::Rect m_details_pane_rect{};
  UI::Rect m_name_input_rect{};
  UI::Rect m_add_btn_rect{};
  UI::Rect m_cancel_btn_rect{};

  mutable std::unordered_map<std::string, XImage *> m_svg_cache;
};

} // namespace Zenvra::Platform::X11::Components
