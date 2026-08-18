#pragma once

#include "UI/Editor/StudioEditorModel.h"
#include "UI/Geometry.h"
#include "UI/Theme/StudioTheme.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Zenvra::UI::Components
{

struct BreadcrumbSegment
{
    std::size_t index = 0;
    std::string text;
    UI::Editor::BreadcrumbIconKind icon_kind = UI::Editor::BreadcrumbIconKind::File;
    std::string icon_svg;
    Rect bounds;
    Rect icon_bounds;
    Rect text_bounds;
    Rect chevron_bounds;
    bool has_chevron = false;
    bool is_ellipsis = false;
};

class BreadcrumbBar
{
public:
    BreadcrumbBar() = default;

    void set_items(std::vector<UI::Editor::BreadcrumbItem> items);
    void set_items(std::span<const UI::Editor::BreadcrumbItem> items);
    [[nodiscard]] std::span<const UI::Editor::BreadcrumbItem> get_items() const noexcept { return m_items; }

    void set_bounds(const Rect& bounds) noexcept { m_bounds = bounds; }
    [[nodiscard]] const Rect& get_bounds() const noexcept { return m_bounds; }

    [[nodiscard]] std::optional<std::size_t> get_hovered_index() const noexcept { return m_hovered_index; }
    void set_hovered_index(std::optional<std::size_t> index) noexcept { m_hovered_index = index; }
    void clear_hover() noexcept { m_hovered_index = std::nullopt; }

    [[nodiscard]] bool handle_pointer_move(float point_x, float point_y) noexcept;
    [[nodiscard]] std::optional<std::size_t> hit_test(float point_x, float point_y) const noexcept;

    [[nodiscard]] const std::vector<BreadcrumbSegment>& get_cached_segments() const noexcept { return m_cached_segments; }

    /// Computes and caches the layout rectangles of each visible segment
    std::vector<BreadcrumbSegment> calculate_layout(
        const std::function<float(std::string_view)>& text_width_fn,
        float dpi_scale,
        float max_width);

    /// Helper to resolve the SVG asset path for a breadcrumb item
    [[nodiscard]] static std::string get_icon_svg(UI::Editor::BreadcrumbIconKind kind, const std::string& file_name);

private:
    std::vector<UI::Editor::BreadcrumbItem> m_items;
    std::vector<BreadcrumbSegment> m_cached_segments;
    Rect m_bounds;
    std::optional<std::size_t> m_hovered_index;
};

} // namespace Zenvra::UI::Components
