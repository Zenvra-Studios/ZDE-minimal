#pragma once

#if defined(_WIN32)

#include "UI/Geometry.h"
#include "UI/Theme/StudioTheme.h"
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <windows.h>


namespace Zenvra::UI::Components {

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

class AddNewItemDialog {
public:
  using CreateCallback = std::function<void(
      const std::string &filename, const std::string &initial_content)>;

  AddNewItemDialog();
  ~AddNewItemDialog();

  AddNewItemDialog(const AddNewItemDialog &) = delete;
  AddNewItemDialog &operator=(const AddNewItemDialog &) = delete;

  void open(HWND parent_hwnd, const std::filesystem::path &target_folder,
            const std::string &project_name, CreateCallback callback);
  void close();
  [[nodiscard]] bool is_visible() const noexcept {
    return m_hwnd != nullptr && IsWindow(m_hwnd);
  }
  [[nodiscard]] HWND get_hwnd() const noexcept { return m_hwnd; }

  struct LayoutResult {
    UI::Rect backdrop{};
    UI::Rect dialog_bounds{};
    UI::Rect titlebar_bounds{};
    UI::Rect close_button_bounds{};

    UI::Rect category_pane_bounds{};
    std::vector<UI::Rect> category_item_bounds{};

    UI::Rect template_pane_bounds{};
    std::vector<UI::Rect> template_item_bounds{};

    UI::Rect details_pane_bounds{};

    UI::Rect footer_bounds{};
    UI::Rect name_label_bounds{};
    UI::Rect name_input_bounds{};
    UI::Rect location_label_bounds{};
    UI::Rect location_value_bounds{};
    UI::Rect add_button_bounds{};
    UI::Rect cancel_button_bounds{};
  };

  [[nodiscard]] LayoutResult calculate_layout(const UI::Rect &viewport,
                                              float dpi_scale) const;
  [[nodiscard]] LayoutResult calculate_layout(float width, float height,
                                              float dpi_scale) const;

  void render(HDC dc, const LayoutResult &layout,
              const UI::Theme::StudioTheme &theme, float dpi_scale) const;

  bool handle_pointer_move(float x, float y, const LayoutResult &layout);
  bool handle_pointer_press(float x, float y, const LayoutResult &layout);
  bool handle_double_click(float x, float y, const LayoutResult &layout);
  bool handle_scroll(int delta);
  bool handle_text_input(std::string_view text);
  bool handle_key_down(WPARAM w_param);

  [[nodiscard]] bool is_interactive_point(float x, float y,
                                          const LayoutResult &layout) const;

private:
  static LRESULT CALLBACK dialog_proc(HWND hwnd, UINT message, WPARAM w_param,
                                      LPARAM l_param);
  LRESULT handle_message(HWND hwnd, UINT message, WPARAM w_param,
                         LPARAM l_param);

  void init_default_templates();
  void select_template(std::size_t index);
  void submit();
  void draw_icon(HDC dc, const std::string &icon_rel_path, int x, int y,
                 int size) const;
  void refresh_fonts();

  HWND m_hwnd = nullptr;
  HWND m_parent_hwnd = nullptr;
  std::filesystem::path m_target_folder;
  std::string m_project_name = "Project";
  CreateCallback m_callback;

  std::vector<TemplateCategory> m_categories;
  std::size_t m_selected_category_index = 0;
  std::size_t m_selected_template_index = 0;
  int m_template_scroll_offset = 0;

  std::string m_filename_input;
  std::size_t m_caret_position = 0;
  std::optional<std::size_t> m_selection_anchor;
  bool m_is_dragging_text = false;

  [[nodiscard]] bool has_selection() const noexcept {
    return m_selection_anchor.has_value() && *m_selection_anchor != m_caret_position;
  }
  [[nodiscard]] std::pair<std::size_t, std::size_t> get_selection_range() const noexcept;
  void clear_selection() noexcept { m_selection_anchor.reset(); }
  void select_all() noexcept;
  void select_stem() noexcept;
  void delete_selection();
  [[nodiscard]] std::size_t get_char_index_from_x(float click_x, const LayoutResult &layout, float dpi_scale) const;
  void copy_selection_to_clipboard() const;
  void paste_from_clipboard();
  void cut_selection_to_clipboard();

  // Hover & focus states
  bool m_close_hovered = false;
  bool m_add_hovered = false;
  bool m_cancel_hovered = false;
  std::optional<std::size_t> m_hovered_category_index;
  std::optional<std::size_t> m_hovered_template_index;
  bool m_name_input_focused = true;
  bool m_caret_visible = true;

  UI::Theme::StudioTheme m_theme = UI::Theme::StudioTheme::zenvra_dark();
  UINT m_dpi = 96;

  // Fonts with ClearType Antialiasing
  HFONT m_regular_font = nullptr;
  HFONT m_semibold_font = nullptr;
  HFONT m_small_font = nullptr;

  // Icon cache
  struct CachedBitmap {
    HBITMAP bitmap = nullptr;
    int width = 0;
    int height = 0;
  };
  mutable std::unordered_map<std::string, CachedBitmap> m_icon_cache;
};

} // namespace Zenvra::UI::Components

#endif // defined(_WIN32)
