#pragma once

#include "UI/Geometry.h"
#include "UI/Theme/StudioTheme.h"

#include <array>
#include <cstddef>
#include <span>
#include <optional>
#include <string_view>

namespace Zenvra::UI::Editor
{

inline constexpr std::size_t maximum_editor_tokens = 128;

enum class EditorTokenKind
{
    Plain,
    Keyword,
    Number,
    Label,
    Type,
    Comment,
    String,
};

struct EditorToken
{
    std::string_view text;
    EditorTokenKind kind = EditorTokenKind::Plain;
};

enum class SidebarIcon
{
    Project,
    VersionControl,
    Search,
    Services,
    More,
    Run,
    Terminal,
    Problems,
};

enum class SidebarPlacement
{
    Top,
    Bottom,
};

struct SidebarItem
{
    std::string_view id;
    std::string_view tooltip;
    SidebarIcon icon = SidebarIcon::Project;
    SidebarPlacement placement = SidebarPlacement::Top;
    bool active = false;
};

struct FooterEditorStatus
{
    std::size_t line = 1;
    std::size_t column = 1;
    std::string_view line_ending = "LF";
    std::string_view encoding = "UTF-8";
    std::size_t indent_width = 4;
};

struct StudioEditorPalette
{
    Theme::Color workspace_background;
    Theme::Color tab_background;
    Theme::Color tab_active_background;
    Theme::Color sidebar_background;
    Theme::Color editor_background;
    Theme::Color active_line_background;
    Theme::Color selection_background;
    Theme::Color status_background;
    Theme::Color border;
    Theme::Color text_primary;
    Theme::Color text_muted;
    Theme::Color keyword;
    Theme::Color number;
    Theme::Color label;
    Theme::Color type;
    Theme::Color comment;
    Theme::Color accent;
    Theme::Color warning;
    Theme::Color success;
    Theme::Color tooltip_background;
    Theme::Color hover_background;
    Theme::Color indent_guide;
    Theme::Color indent_guide_active;

    [[nodiscard]] static StudioEditorPalette jetbrains_dark() noexcept;
};

struct StudioEditorLayoutResult
{
    Rect workspace_bounds;
    Rect tab_bar_bounds;
    Rect activity_bar_bounds;
    Rect tool_sidebar_bounds;
    Rect gutter_bounds;
    Rect editor_bounds;
    Rect minimap_bounds;
    Rect terminal_panel_bounds;
    Rect terminal_header_bounds;
    Rect terminal_content_bounds;
    Rect status_bar_bounds;
    Rect scrollbar_bounds;
    float dpi_scale = 1.0F;
};

struct StudioEditorMetrics final
{
    static constexpr float activity_width = 38.0F;
    static constexpr float tab_height = 30.0F;
    static constexpr float status_height = 24.0F;
    static constexpr float gutter_width = 66.0F;
    static constexpr float fold_margin_width = 14.0F;
    static constexpr float sidebar_item_height = 36.0F;
    static constexpr float sidebar_item_spacing = 40.0F;
    static constexpr float sidebar_top_offset = 21.0F;
    static constexpr float sidebar_bottom_offset = 13.0F;
    static constexpr float sidebar_icon_size = 18.0F;
    static constexpr float editor_tab_action_width = 30.0F;
    static constexpr float editor_tab_minimum_width = 112.0F;
    static constexpr float editor_tab_maximum_width = 224.0F;
    static constexpr float editor_tab_chrome_width = 68.0F;
    static constexpr float editor_tab_gap = -1.0F;
    static constexpr float editor_tab_icon_offset = 10.0F;
    static constexpr float editor_tab_label_offset = 26.0F;
    static constexpr float editor_tab_close_width = 30.0F;
    static constexpr float titlebar_navigation_width = 80.0F;
    static constexpr float titlebar_window_controls_width = 600.0F;
};

class StudioEditorLayout
{
public:
    [[nodiscard]] StudioEditorLayoutResult calculate(
        float client_width,
        float client_height,
        float content_top,
        float dpi_scale,
        bool terminal_visible = false,
        float terminal_height = 218.0F,
        bool terminal_maximized = false,
        bool tool_sidebar_visible = false,
        float tool_sidebar_width = 260.0F) const noexcept;
};

[[nodiscard]] std::span<const SidebarItem> get_studio_sidebar_items() noexcept;
[[nodiscard]] Rect calculate_studio_sidebar_item_bounds(
    const StudioEditorLayoutResult& layout,
    std::size_t item_index) noexcept;
[[nodiscard]] std::optional<std::size_t> hit_test_studio_sidebar(
    const StudioEditorLayoutResult& layout,
    float point_x,
    float point_y) noexcept;
[[nodiscard]] float calculate_editor_tab_width(
    float text_width,
    float dpi_scale) noexcept;
[[nodiscard]] std::size_t tokenize_editor_line(
    std::string_view line,
    std::array<EditorToken, maximum_editor_tokens>& output) noexcept;
[[nodiscard]] bool supports_editor_syntax_highlighting(
    std::string_view file_name) noexcept;

} // namespace Zenvra::UI::Editor
