#pragma once

#include "Settings/SettingsDefinition.h"
#include "Settings/SettingsService.h"
#include "UI/Components/Input.h"
#include "UI/Geometry.h"
#include "UI/Settings/SettingsControl.h"
#include "UI/Theme/StudioTheme.h"

#include <windows.h>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Zenvra::UI::Settings
{

struct SettingsCategoryItem
{
    std::string id;
    std::string name;
    Rect bounds;
    Rect chevron_bounds;
    bool has_children = false;
    bool is_expanded = false;
    int depth = 0;
};

struct SettingsSectionHeader
{
    std::string category_id;
    std::string title;
    Rect bounds;
    float content_y = 0.0F;
};

struct SettingsWindowLayoutResult
{
    Rect overlay_bounds;
    Rect dialog_bounds;
    Rect header_bounds;
    Rect title_bounds;
    Rect close_btn_bounds;

    Rect search_bar_bounds;
    Rect search_clear_btn_bounds;

    Rect tabs_bar_bounds;
    Rect user_tab_bounds;
    Rect workspace_tab_bounds;
    Rect tab_underline_bounds;
    Rect header_separator_bounds;
    Rect sidebar_divider_bounds;

    Rect sidebar_bounds;
    std::vector<SettingsCategoryItem> category_items;
    Rect sidebar_scrollbar_track;
    Rect sidebar_scrollbar_thumb;

    Rect content_bounds;
    Rect category_header_bounds;
    std::vector<SettingsSectionHeader> section_headers;
    std::vector<SettingRowLayout> rows;
    Rect scrollbar_track;
    Rect scrollbar_thumb;

    bool close_hovered = false;
    bool user_tab_hovered = false;
    bool workspace_tab_hovered = false;
    std::string hovered_category;
    bool scrollbar_thumb_hovered = false;
    bool sidebar_scrollbar_thumb_hovered = false;
    bool search_clear_hovered = false;
};

class SettingsWindow
{
public:
    SettingsWindow();
    ~SettingsWindow();

    SettingsWindow(const SettingsWindow &) = delete;
    SettingsWindow &operator=(const SettingsWindow &) = delete;

    void open(HWND parent_hwnd = nullptr);
    void close();
    void toggle(HWND parent_hwnd = nullptr);

    [[nodiscard]] bool is_visible() const noexcept {
        return m_hwnd != nullptr && IsWindow(m_hwnd);
    }
    [[nodiscard]] HWND get_hwnd() const noexcept { return m_hwnd; }

    [[nodiscard]] SettingsWindowLayoutResult calculate_layout(
        float width,
        float height,
        float dpi_scale = 1.0F) const noexcept;

    [[nodiscard]] SettingsWindowLayoutResult calculate_layout(
        const Rect& viewport_bounds,
        float dpi_scale = 1.0F) const noexcept;

    [[nodiscard]] bool is_interactive_point(
        float x,
        float y,
        const SettingsWindowLayoutResult& layout) const noexcept;

    bool handle_pointer_press(
        float x,
        float y,
        const SettingsWindowLayoutResult& layout) noexcept;

    bool handle_pointer_move(
        float x,
        float y,
        const SettingsWindowLayoutResult& layout) noexcept;

    bool handle_pointer_release(
        float x,
        float y,
        const SettingsWindowLayoutResult& layout) noexcept;

    bool handle_scroll(
        float delta_y,
        const SettingsWindowLayoutResult& layout,
        float mouse_x = -1.0F,
        float mouse_y = -1.0F) noexcept;

    bool handle_char(char32_t codepoint) noexcept;
    bool handle_backspace() noexcept;
    bool handle_escape() noexcept;

    void render(
        HDC device_context,
        const SettingsWindowLayoutResult& layout,
        const Theme::StudioTheme& theme,
        float dpi_scale) const;

    void render(
        HDC device_context,
        const Rect& viewport_bounds,
        float dpi_scale,
        const Theme::StudioTheme& theme) const;

    [[nodiscard]] const std::string& get_search_query() const noexcept { return m_search_input.get_text(); }
    [[nodiscard]] const std::string& get_active_category() const noexcept { return m_active_category; }
    [[nodiscard]] Zenvra::Settings::SettingsScope get_active_scope() const noexcept { return m_active_scope; }

    void set_theme(const Theme::StudioTheme& theme) noexcept { m_theme = theme; }
    [[nodiscard]] const Theme::StudioTheme& get_theme() const noexcept { return m_theme; }

private:
    static LRESULT CALLBACK dialog_proc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param);
    LRESULT handle_message(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param);

    void refresh_fonts();
    void draw_icon(HDC dc, const std::string &icon_rel_path, int x, int y, int size,
                   std::optional<COLORREF> tint_color = std::nullopt) const;
    void draw_mascot_thumbnail(HDC dc, const std::string &image_path, const Rect &bounds,
                               const Theme::StudioTheme &theme, float dpi_scale) const;
    bool browse_mascot_image();

    HWND m_hwnd = nullptr;
    HWND m_parent_hwnd = nullptr;
    UINT m_dpi = 96;

    HFONT m_regular_font = nullptr;
    HFONT m_semibold_font = nullptr;
    HFONT m_small_font = nullptr;
    HFONT m_title_font = nullptr;
    HFONT m_header_large_font = nullptr;

    struct CachedBitmap {
        HBITMAP bitmap = nullptr;
        int width = 0;
        int height = 0;
    };
    mutable std::unordered_map<std::string, CachedBitmap> m_icon_cache;

    Theme::StudioTheme m_theme = Theme::StudioTheme::zenvra_dark();

    std::string m_active_category = "Commonly Used";
    std::unordered_map<std::string, bool> m_category_expanded;
    Zenvra::Settings::SettingsScope m_active_scope = Zenvra::Settings::SettingsScope::User;

    mutable UI::Components::Input m_search_input;
    float m_scroll_offset = 0.0F;
    mutable float m_max_scroll = 0.0F;

    float m_sidebar_scroll_offset = 0.0F;
    mutable float m_sidebar_max_scroll = 0.0F;

    bool m_is_dragging_scrollbar = false;
    float m_drag_start_y = 0.0F;
    float m_drag_start_scroll_offset = 0.0F;

    bool m_is_dragging_sidebar_scrollbar = false;
    float m_drag_start_sidebar_y = 0.0F;
    float m_drag_start_sidebar_scroll_offset = 0.0F;

    bool m_caret_visible = true;

    mutable bool m_close_hovered = false;
    mutable bool m_user_tab_hovered = false;
    mutable bool m_workspace_tab_hovered = false;
    mutable std::string m_hovered_category;
    mutable bool m_scrollbar_thumb_hovered = false;
    mutable bool m_sidebar_scrollbar_thumb_hovered = false;
    mutable bool m_search_clear_hovered = false;

    std::string m_focused_setting_id = "editor.fontSize";
    std::string m_editing_setting_id;
    std::string m_editing_text;
    bool m_font_dropdown_open = false;
    float m_font_dropdown_scroll = 0.0F;
    mutable float m_font_dropdown_max_scroll = 0.0F;
    bool m_is_dragging_font_scrollbar = false;
    float m_drag_start_font_y = 0.0F;
    float m_drag_start_font_scroll = 0.0F;
    mutable Rect m_font_dropdown_bounds;
    mutable Rect m_font_dropdown_scrollbar_track;
    mutable Rect m_font_dropdown_scrollbar_thumb;
    mutable bool m_font_scrollbar_thumb_hovered = false;
    mutable std::vector<std::pair<std::string, Rect>> m_font_dropdown_items;
    mutable std::string m_hovered_font_option;
};

} // namespace Zenvra::UI::Settings
