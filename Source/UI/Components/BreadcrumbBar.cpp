#include "UI/Components/BreadcrumbBar.h"
#include "UI/Editor/FileIconModel.h"

#include <algorithm>
#include <filesystem>

namespace Zenvra::UI::Components
{

void BreadcrumbBar::set_items(std::vector<UI::Editor::BreadcrumbItem> items)
{
    m_items = std::move(items);
}

void BreadcrumbBar::set_items(std::span<const UI::Editor::BreadcrumbItem> items)
{
    m_items.assign(items.begin(), items.end());
}

bool BreadcrumbBar::handle_pointer_move(float point_x, float point_y) noexcept
{
    const auto new_hovered = hit_test(point_x, point_y);
    if (new_hovered != m_hovered_index)
    {
        m_hovered_index = new_hovered;
        return true;
    }
    return false;
}

std::optional<std::size_t> BreadcrumbBar::hit_test(float point_x, float point_y) const noexcept
{
    for (const auto& seg : m_cached_segments)
    {
        if (seg.bounds.contains(point_x, point_y))
        {
            return seg.index;
        }
    }
    return std::nullopt;
}

std::vector<BreadcrumbSegment> BreadcrumbBar::calculate_layout(
    const std::function<float(std::string_view)>& text_width_fn,
    float dpi_scale,
    float max_width)
{
    m_cached_segments.clear();
    if (m_items.empty() || max_width <= 0.0F)
    {
        return m_cached_segments;
    }

    const float icon_size = std::max(12.0F * dpi_scale, 10.0F);
    const float icon_gap = 4.0F * dpi_scale;
    const float chevron_size = std::max(11.0F * dpi_scale, 9.0F);
    const float chevron_gap = 3.0F * dpi_scale;
    const float segment_pad_h = 4.0F * dpi_scale;
    const float center_y = m_bounds.y + m_bounds.height * 0.5F;
    const float height = std::min(m_bounds.height, 22.0F * dpi_scale);
    const float seg_y = center_y - height * 0.5F;

    float current_x = m_bounds.x;

    for (std::size_t i = 0; i < m_items.size(); ++i)
    {
        const auto& item = m_items[i];
        const bool is_last = (i + 1 == m_items.size());
        const float text_w = text_width_fn
            ? text_width_fn(item.text)
            : (static_cast<float>(item.text.size()) * 7.0F * dpi_scale);
        const float seg_w = segment_pad_h + icon_size + icon_gap + text_w + segment_pad_h;
        const float needed_chevron_w = is_last ? 0.0F : (chevron_gap + chevron_size + chevron_gap);

        if (current_x + seg_w + needed_chevron_w > m_bounds.x + max_width && i > 0)
        {
            BreadcrumbSegment ellipsis_seg;
            ellipsis_seg.index = i;
            ellipsis_seg.text = "...";
            ellipsis_seg.is_ellipsis = true;
            const float el_text_w = text_width_fn ? text_width_fn("...") : 16.0F * dpi_scale;
            ellipsis_seg.bounds = Rect{current_x, seg_y, el_text_w + segment_pad_h * 2.0F, height};
            ellipsis_seg.text_bounds = Rect{current_x + segment_pad_h, seg_y, el_text_w, height};
            m_cached_segments.push_back(std::move(ellipsis_seg));
            break;
        }

        BreadcrumbSegment seg;
        seg.index = i;
        seg.text = item.text;
        seg.icon_kind = item.icon;
        seg.icon_svg = get_icon_svg(item.icon, item.text);
        seg.has_chevron = !is_last;

        seg.bounds = Rect{current_x, seg_y, seg_w, height};
        seg.icon_bounds = Rect{current_x + segment_pad_h, center_y - icon_size * 0.5F, icon_size, icon_size};
        seg.text_bounds = Rect{current_x + segment_pad_h + icon_size + icon_gap, seg_y, text_w, height};

        current_x += seg_w;

        if (!is_last)
        {
            seg.chevron_bounds = Rect{current_x + chevron_gap, center_y - chevron_size * 0.5F, chevron_size, chevron_size};
            current_x += needed_chevron_w;
        }

        m_cached_segments.push_back(std::move(seg));
    }

    return m_cached_segments;
}

std::string BreadcrumbBar::get_icon_svg(UI::Editor::BreadcrumbIconKind kind, const std::string& file_name)
{
    switch (kind)
    {
    case UI::Editor::BreadcrumbIconKind::Folder:
        return "Assets/icons/folder.svg";
    case UI::Editor::BreadcrumbIconKind::Namespace:
        return "Assets/icons/material-icon-theme/folder-interface.svg";
    case UI::Editor::BreadcrumbIconKind::Class:
    case UI::Editor::BreadcrumbIconKind::Struct:
        return "Assets/icons/material-icon-theme/folder-class.svg";
    case UI::Editor::BreadcrumbIconKind::Function:
        return "Assets/icons/material-icon-theme/folder-functions.svg";
    case UI::Editor::BreadcrumbIconKind::File:
    {
        const std::string asset = UI::Editor::file_icon_asset_for_path(std::filesystem::path{file_name});
        return "Assets/icons/" + asset;
    }
    }
    return "Assets/icons/file.svg";
}

} // namespace Zenvra::UI::Components
