#pragma once

#include "Utility/Flex.h"

#include <span>
#include <utility>
#include <vector>

namespace Zenvra::Utility
{

class Row final
{
public:
    explicit Row(float gap = 0.0F) noexcept
    {
        m_options.axis = LayoutAxis::Horizontal;
        m_options.gap = gap;
    }

    Row& add(FlexItem item)
    {
        m_items.push_back(std::move(item));
        return *this;
    }

    Row& gap(float value) noexcept
    {
        m_options.gap = value;
        return *this;
    }

    Row& justify(LayoutJustify value) noexcept
    {
        m_options.justify_content = value;
        return *this;
    }

    Row& align(LayoutAlign value) noexcept
    {
        m_options.align_items = value;
        return *this;
    }

    Row& reverse(bool value = true) noexcept
    {
        m_options.reverse = value;
        return *this;
    }

    [[nodiscard]] FlexLayoutResult calculate(const UI::Rect& container) const
    {
        return Flex::calculate(container, m_items, m_options);
    }

    [[nodiscard]] static FlexLayoutResult calculate(
        const UI::Rect& container,
        std::span<const FlexItem> items,
        float gap = 0.0F,
        LayoutJustify justify = LayoutJustify::Start,
        LayoutAlign align = LayoutAlign::Stretch,
        bool reverse = false)
    {
        return Flex::calculate(container, items, FlexOptions{
            .axis = LayoutAxis::Horizontal,
            .justify_content = justify,
            .align_items = align,
            .gap = gap,
            .reverse = reverse,
        });
    }

private:
    std::vector<FlexItem> m_items;
    FlexOptions m_options{.axis = LayoutAxis::Horizontal};
};

} // namespace Zenvra::Utility
