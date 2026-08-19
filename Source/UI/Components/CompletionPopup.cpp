#include "UI/Components/CompletionPopup.h"

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace Zenvra::UI::Components
{

namespace
{

int calculate_match_score(std::string_view candidate, std::string_view query) noexcept
{
    if (query.empty())
    {
        return 1;
    }

    // Exact match
    if (candidate == query)
    {
        return 10000;
    }

    // Exact case-sensitive prefix
    if (candidate.starts_with(query))
    {
        return 8000 - static_cast<int>(candidate.size() - query.size());
    }

    // Case-insensitive prefix
    bool prefix_ci = true;
    if (candidate.size() >= query.size())
    {
        for (std::size_t i = 0; i < query.size(); ++i)
        {
            if (std::tolower(static_cast<unsigned char>(candidate[i])) !=
                std::tolower(static_cast<unsigned char>(query[i])))
            {
                prefix_ci = false;
                break;
            }
        }
    }
    else
    {
        prefix_ci = false;
    }

    if (prefix_ci)
    {
        return 6000 - static_cast<int>(candidate.size() - query.size());
    }

    // Substring match
    std::string cand_lower;
    cand_lower.reserve(candidate.size());
    for (char c : candidate) cand_lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

    std::string query_lower;
    query_lower.reserve(query.size());
    for (char c : query) query_lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

    const auto sub_pos = cand_lower.find(query_lower);
    if (sub_pos != std::string::npos)
    {
        const bool at_boundary = (sub_pos > 0 && (candidate[sub_pos - 1] == '_' || candidate[sub_pos - 1] == ':' || candidate[sub_pos - 1] == '.'));
        return (at_boundary ? 4000 : 2500) - static_cast<int>(sub_pos * 10 + candidate.size());
    }

    // Fuzzy subsequence match
    std::size_t text_idx = 0;
    std::size_t query_idx = 0;
    int matches = 0;

    while (text_idx < cand_lower.size() && query_idx < query_lower.size())
    {
        if (cand_lower[text_idx] == query_lower[query_idx])
        {
            ++matches;
            ++query_idx;
        }
        ++text_idx;
    }

    if (query_idx == query_lower.size())
    {
        return 1000 - static_cast<int>(text_idx * 5 + candidate.size());
    }

    return 0;
}

} // namespace

void CompletionPopup::show(std::vector<Language::Protocol::CompletionItem> items, float anchor_x, float anchor_y)
{
    m_all_items = std::move(items);
    m_anchor_x = anchor_x;
    m_anchor_y = anchor_y;
    m_filter_query.clear();
    m_visible = !m_all_items.empty();
    m_selected_index = 0;
    m_scroll_offset = 0;
    update_filtering();
}

void CompletionPopup::merge_items(std::vector<Language::Protocol::CompletionItem> items)
{
    if (items.empty()) return;
    std::unordered_set<std::string> existing;
    for (const auto& it : m_all_items)
    {
        existing.insert(it.label);
    }
    bool added = false;
    for (auto& item : items)
    {
        if (existing.insert(item.label).second)
        {
            m_all_items.push_back(std::move(item));
            added = true;
        }
    }
    if (added)
    {
        m_visible = !m_all_items.empty();
        update_filtering();
    }
}

void CompletionPopup::hide() noexcept
{
    m_visible = false;
    m_all_items.clear();
    m_filtered_indices.clear();
    m_selected_index = 0;
    m_scroll_offset = 0;
}

void CompletionPopup::set_filter(std::string_view query)
{
    m_filter_query = std::string(query);
    update_filtering();
}

void CompletionPopup::update_filtering()
{
    struct ScoredIndex
    {
        std::size_t index;
        int score;
    };

    std::vector<ScoredIndex> scored;
    scored.reserve(m_all_items.size());

    for (std::size_t i = 0; i < m_all_items.size(); ++i)
    {
        const auto& item = m_all_items[i];
        const std::string_view candidate = !item.filter_text.empty() ? item.filter_text : item.label;
        const int score = calculate_match_score(candidate, m_filter_query);
        if (score > 0)
        {
            scored.push_back({i, score});
        }
    }

    std::stable_sort(scored.begin(), scored.end(), [](const ScoredIndex& a, const ScoredIndex& b) {
        return a.score > b.score;
    });

    m_filtered_indices.clear();
    m_filtered_indices.reserve(scored.size());
    for (const auto& s : scored)
    {
        m_filtered_indices.push_back(s.index);
    }

    if (m_filtered_indices.empty())
    {
        m_selected_index = 0;
        m_scroll_offset = 0;
    }
    else if (m_selected_index >= m_filtered_indices.size())
    {
        m_selected_index = 0;
        m_scroll_offset = 0;
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
}

void CompletionPopup::select_previous() noexcept
{
    if (m_filtered_indices.empty()) return;

    if (m_selected_index == 0)
    {
        m_selected_index = m_filtered_indices.size() - 1;
    }
    else
    {
        --m_selected_index;
    }
    ensure_selection_visible();
}

void CompletionPopup::select_next() noexcept
{
    if (m_filtered_indices.empty()) return;

    if (m_selected_index + 1 >= m_filtered_indices.size())
    {
        m_selected_index = 0;
    }
    else
    {
        ++m_selected_index;
    }
    ensure_selection_visible();
}

void CompletionPopup::select_first() noexcept
{
    if (m_filtered_indices.empty()) return;
    m_selected_index = 0;
    ensure_selection_visible();
}

void CompletionPopup::select_last() noexcept
{
    if (m_filtered_indices.empty()) return;
    m_selected_index = m_filtered_indices.size() - 1;
    ensure_selection_visible();
}

bool CompletionPopup::scroll(int delta_lines) noexcept
{
    if (m_filtered_indices.empty() || m_filtered_indices.size() <= m_max_visible_items)
    {
        return false;
    }

    const std::size_t max_scroll = m_filtered_indices.size() - m_max_visible_items;
    const std::size_t prev_scroll = m_scroll_offset;

    if (delta_lines > 0)
    {
        m_scroll_offset = std::min(m_scroll_offset + static_cast<std::size_t>(delta_lines), max_scroll);
    }
    else if (delta_lines < 0)
    {
        const std::size_t abs_delta = static_cast<std::size_t>(-delta_lines);
        m_scroll_offset = (m_scroll_offset > abs_delta) ? m_scroll_offset - abs_delta : 0;
    }

    return m_scroll_offset != prev_scroll;
}

const Language::Protocol::CompletionItem* CompletionPopup::get_selected_item() const noexcept
{
    if (m_filtered_indices.empty() || m_selected_index >= m_filtered_indices.size())
    {
        return nullptr;
    }
    return &m_all_items[m_filtered_indices[m_selected_index]];
}

const Language::Protocol::CompletionItem* CompletionPopup::get_item(std::size_t index) const noexcept
{
    if (index >= m_filtered_indices.size())
    {
        return nullptr;
    }
    return &m_all_items[m_filtered_indices[index]];
}

Rect CompletionPopup::calculate_bounds(float line_height, float max_width) const noexcept
{
    if (m_filtered_indices.empty())
    {
        return Rect{m_anchor_x, m_anchor_y, 0.0F, 0.0F};
    }

    const std::size_t visible_count = std::min(m_filtered_indices.size(), m_max_visible_items);
    const float popup_h = static_cast<float>(visible_count) * line_height + 4.0F;
    const float popup_w = max_width;

    return Rect{m_anchor_x, m_anchor_y, popup_w, popup_h};
}

Rect CompletionPopup::calculate_detail_bounds(const Rect& main_bounds, float detail_width) const noexcept
{
    const float detail_x = main_bounds.right() + 4.0F;
    const float detail_y = main_bounds.y;
    const float detail_h = main_bounds.height;
    return Rect{detail_x, detail_y, detail_width, detail_h};
}

bool CompletionPopup::is_point_inside(float x, float y, float line_height, float max_width) const noexcept
{
    if (!is_visible()) return false;
    const Rect bounds = calculate_bounds(line_height, max_width);
    return bounds.contains(x, y);
}

} // namespace Zenvra::UI::Components
