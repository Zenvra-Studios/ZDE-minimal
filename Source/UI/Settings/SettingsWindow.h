#pragma once

#include "Settings/SettingsDefinition.h"
#include "Settings/SettingsService.h"
#include "UI/Components/Dropdown.h"
#include "UI/Components/Input.h"
#include "UI/Geometry.h"
#include "UI/Settings/SettingsControl.h"
#include "UI/Theme/StudioTheme.h"

#ifdef _WIN32
#include <windows.h>
#endif
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

#ifdef _WIN32
    void open(HWND parent_hwnd = nullptr);
    void close();
    void toggle(HWND parent_hwnd = nullptr);

    [[nodiscard]] bool is_visible() const noexcept {
        return m_hwnd != nullptr && IsWindow(m_hwnd);
    }
    [[nodiscard]] HWND get_hwnd() const noexcept { return m_hwnd; }
#else
    // Cross-platform stubs: native popup is Win32-only for now.
    // Layout / interaction API below stays fully functional on Linux/macOS.
    void open(void* parent_hwnd = nullptr);
    void close();
    void toggle(void* parent_hwnd = nullptr);

    [[nodiscard]] bool is_visible() const noexcept { return m_visible; }
    [[nodiscard]] void* get_hwnd() const noexcept { return nullptr; }
#endif

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
    bool handle_enter() noexcept;
    bool handle_key(uint32_t key_symbol, float dpi_scale = 1.0F) noexcept;

#ifdef _WIN32
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
#endif

    [[nodiscard]] const std::string& get_search_query() const noexcept { return m_search_input.get_text(); }
    [[nodiscard]] const std::string& get_active_category() const noexcept { return m_active_category; }
    [[nodiscard]] Zenvra::Settings::SettingsScope get_active_scope() const noexcept { return m_active_scope; }

    // Cross-platform read-only access for non-Win32 renderers (e.g. X11).
    [[nodiscard]] const UI::Components::Dropdown& get_dropdown() const noexcept { return m_dropdown; }
    [[nodiscard]] const std::string& get_editing_setting_id() const noexcept { return m_editing_setting_id; }
    [[nodiscard]] const std::string& get_editing_text() const noexcept { return m_editing_text; }
    [[nodiscard]] bool is_caret_visible() const noexcept { return m_caret_visible; }
    [[nodiscard]] bool is_dragging_scrollbar() const noexcept { return m_is_dragging_scrollbar; }
    [[nodiscard]] bool is_dragging_sidebar_scrollbar() const noexcept { return m_is_dragging_sidebar_scrollbar; }
    [[nodiscard]] const UI::Components::Input& get_search_input() const noexcept { return m_search_input; }
    [[nodiscard]] UI::Components::Input& get_search_input_mut() noexcept { return m_search_input; }
    [[nodiscard]] bool tick() noexcept {
        bool changed = m_search_input.tick();
        if (!m_editing_setting_id.empty()) {
            const auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - m_last_setting_blink_toggle).count() >= 530) {
                m_last_setting_blink_toggle = now;
                m_caret_visible = !m_caret_visible;
                changed = true;
            }
        }
        return changed;
    }

    void set_theme(const Theme::StudioTheme& theme) noexcept { m_theme = theme; }
    [[nodiscard]] const Theme::StudioTheme& get_theme() const noexcept { return m_theme; }

private:
#ifdef _WIN32
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
#else
    bool m_visible = false;
#endif

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
    std::chrono::steady_clock::time_point m_last_setting_blink_toggle = std::chrono::steady_clock::now();

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
    UI::Components::Dropdown m_dropdown;
};

} // namespace Zenvra::UI::Settings
