#pragma once

#include "Language/Protocol/LspTypes.h"
#include "UI/Geometry.h"
#include "UI/Theme/StudioTheme.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace Zenvra::UI::Components
{

class CompletionPopup
{
public:
    CompletionPopup() = default;

    void show(std::vector<Language::Protocol::CompletionItem> items, float anchor_x, float anchor_y);
    void merge_items(std::vector<Language::Protocol::CompletionItem> items);
    void hide() noexcept;
    [[nodiscard]] bool is_visible() const noexcept { return m_visible && !m_filtered_indices.empty(); }

    void set_filter(std::string_view query);
    [[nodiscard]] const std::string& get_filter() const noexcept { return m_filter_query; }
    void select_previous() noexcept;
    void select_next() noexcept;
    void select_first() noexcept;
    void select_last() noexcept;
    bool scroll(int delta_lines) noexcept;

    [[nodiscard]] const Language::Protocol::CompletionItem* get_selected_item() const noexcept;
    [[nodiscard]] std::size_t get_selected_index() const noexcept { return m_selected_index; }
    [[nodiscard]] std::size_t get_item_count() const noexcept { return m_filtered_indices.size(); }
    [[nodiscard]] std::size_t get_scroll_offset() const noexcept { return m_scroll_offset; }
    [[nodiscard]] std::size_t get_max_visible_items() const noexcept { return m_max_visible_items; }
    [[nodiscard]] const Language::Protocol::CompletionItem* get_item(std::size_t index) const noexcept;

    [[nodiscard]] Rect calculate_bounds(float line_height = 24.0F, float max_width = 340.0F) const noexcept;
    [[nodiscard]] Rect calculate_detail_bounds(const Rect& main_bounds, float detail_width = 260.0F) const noexcept;
    [[nodiscard]] bool is_point_inside(float x, float y, float line_height = 24.0F, float max_width = 340.0F) const noexcept;

    [[nodiscard]] float get_anchor_x() const noexcept { return m_anchor_x; }
    [[nodiscard]] float get_anchor_y() const noexcept { return m_anchor_y; }

private:
    void update_filtering();
    void ensure_selection_visible() noexcept;

    bool m_visible = false;
    float m_anchor_x = 0.0F;
    float m_anchor_y = 0.0F;
    std::string m_filter_query;

    std::vector<Language::Protocol::CompletionItem> m_all_items;
    std::vector<std::size_t> m_filtered_indices;
    std::size_t m_selected_index = 0;
    std::size_t m_scroll_offset = 0;
    std::size_t m_max_visible_items = 8;
};

} // namespace Zenvra::UI::Components
