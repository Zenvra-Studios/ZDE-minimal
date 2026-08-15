#include "UI/Components/CompletionPopup.h"

#include <algorithm>
#include <cctype>

namespace Zenvra::UI::Components
{

namespace
{

bool fuzzy_matches(std::string_view text, std::string_view query) noexcept
{
    if (query.empty())
    {
        return true;
    }

    auto match_sub = [](std::string_view t, std::string_view q) {
        std::size_t text_idx = 0;
        std::size_t query_idx = 0;

        while (text_idx < t.size() && query_idx < q.size())
        {
            const char tc = static_cast<char>(std::tolower(static_cast<unsigned char>(t[text_idx])));
            const char qc = static_cast<char>(std::tolower(static_cast<unsigned char>(q[query_idx])));
            if (tc == qc)
            {
                ++query_idx;
            }
            ++text_idx;
        }

        return query_idx == q.size();
    };

    if (match_sub(text, query))
    {
        return true;
    }

    if (query.starts_with('#'))
    {
        return match_sub(text, query.substr(1));
    }

    return false;
}

} // namespace

void CompletionPopup::show(std::vector<Language::Protocol::CompletionItem> items, float anchor_x, float anchor_y)
{
    m_all_items = std::move(items);
    m_anchor_x = anchor_x;
    m_anchor_y = anchor_y;
    m_filter_query.clear();
    m_visible = !m_all_items.empty();
    update_filtering();
}

void CompletionPopup::hide() noexcept
{
    m_visible = false;
    m_all_items.clear();
    m_filtered_indices.clear();
    m_selected_index = 0;
}

void CompletionPopup::set_filter(std::string_view query)
{
    m_filter_query = std::string(query);
    update_filtering();
}

void CompletionPopup::update_filtering()
{
    m_filtered_indices.clear();
    for (std::size_t i = 0; i < m_all_items.size(); ++i)
    {
        const auto& item = m_all_items[i];
        const std::string_view candidate = !item.filter_text.empty() ? item.filter_text : item.label;
        if (fuzzy_matches(candidate, m_filter_query))
        {
            m_filtered_indices.push_back(i);
        }
    }

    if (m_filtered_indices.empty())
    {
        m_selected_index = 0;
        m_scroll_offset = 0;
    }
    else if (m_selected_index >= m_filtered_indices.size())
    {
        m_selected_index = m_filtered_indices.size() - 1;
        ensure_selection_visible();
    }
    else
    {
        ensure_selection_visible();
    }
}

void CompletionPopup::ensure_selection_visible() noexcept
{
    if (m_filtered_indices.empty())
    {
        m_scroll_offset = 0;
        return;
    }

    if (m_selected_index < m_scroll_offset)
    {
        m_scroll_offset = m_selected_index;
    }
    else if (m_selected_index >= m_scroll_offset + m_max_visible_items)
    {
        m_scroll_offset = m_selected_index - m_max_visible_items + 1;
    }

    if (m_scroll_offset + m_max_visible_items > m_filtered_indices.size())
    {
        m_scroll_offset = m_filtered_indices.size() > m_max_visible_items
            ? m_filtered_indices.size() - m_max_visible_items
            : 0;
    }
}

void CompletionPopup::select_previous() noexcept
{
    if (m_filtered_indices.empty()) return;
    if (m_selected_index > 0)
    {
        --m_selected_index;
    }
    else
    {
        m_selected_index = m_filtered_indices.size() - 1;
    }
    ensure_selection_visible();
}

void CompletionPopup::select_next() noexcept
{
    if (m_filtered_indices.empty()) return;
    if (m_selected_index + 1 < m_filtered_indices.size())
    {
        ++m_selected_index;
    }
    else
    {
        m_selected_index = 0;
    }
    ensure_selection_visible();
}

void CompletionPopup::select_first() noexcept
{
    m_selected_index = 0;
    ensure_selection_visible();
}

void CompletionPopup::select_last() noexcept
{
    if (!m_filtered_indices.empty())
    {
        m_selected_index = m_filtered_indices.size() - 1;
    }
    ensure_selection_visible();
}

bool CompletionPopup::scroll(int delta_lines) noexcept
{
    if (m_filtered_indices.size() <= m_max_visible_items)
    {
        return false;
    }

    const std::size_t max_scroll = m_filtered_indices.size() - m_max_visible_items;
    const std::size_t prev_offset = m_scroll_offset;

    if (delta_lines < 0) // Scroll down
    {
        const std::size_t lines = static_cast<std::size_t>(-delta_lines);
        m_scroll_offset = std::min(m_scroll_offset + lines, max_scroll);
    }
    else if (delta_lines > 0) // Scroll up
    {
        const std::size_t lines = static_cast<std::size_t>(delta_lines);
        m_scroll_offset = (m_scroll_offset >= lines) ? m_scroll_offset - lines : 0;
    }

    return m_scroll_offset != prev_offset;
}

const Language::Protocol::CompletionItem* CompletionPopup::get_selected_item() const noexcept
{
    if (m_filtered_indices.empty() || m_selected_index >= m_filtered_indices.size())
    {
        return nullptr;
    }
    const std::size_t item_index = m_filtered_indices[m_selected_index];
    if (item_index >= m_all_items.size())
    {
        return nullptr;
    }
    return &m_all_items[item_index];
}

const Language::Protocol::CompletionItem* CompletionPopup::get_item(std::size_t index) const noexcept
{
    if (index >= m_filtered_indices.size())
    {
        return nullptr;
    }
    const std::size_t item_index = m_filtered_indices[index];
    if (item_index >= m_all_items.size())
    {
        return nullptr;
    }
    return &m_all_items[item_index];
}

Rect CompletionPopup::calculate_bounds(float line_height, float max_width) const noexcept
{
    if (m_filtered_indices.empty())
    {
        return Rect{m_anchor_x, m_anchor_y, 0.0F, 0.0F};
    }
    const std::size_t rows = std::clamp<std::size_t>(m_filtered_indices.size(), 1, m_max_visible_items);
    const float height = static_cast<float>(rows) * line_height + 4.0F;

    return Rect{
        .x = m_anchor_x,
        .y = m_anchor_y,
        .width = max_width,
        .height = height
    };
}

Rect CompletionPopup::calculate_detail_bounds(const Rect& main_bounds, float detail_width) const noexcept
{
    return Rect{
        .x = main_bounds.x + main_bounds.width + 4.0F,
        .y = main_bounds.y,
        .width = detail_width,
        .height = main_bounds.height
    };
}

bool CompletionPopup::is_point_inside(float x, float y, float line_height, float max_width) const noexcept
{
    if (!m_visible || m_filtered_indices.empty()) return false;
    const Rect bounds = calculate_bounds(line_height, max_width);
    return bounds.contains(x, y);
}

} // namespace Zenvra::UI::Components
