#pragma once

#include "UI/Geometry.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numeric>
#include <span>
#include <vector>

namespace Zenvra::Utility
{

enum class LayoutAxis
{
    Horizontal,
    Vertical,
};

enum class LayoutJustify
{
    Start,
    Center,
    End,
    SpaceBetween,
    SpaceAround,
    SpaceEvenly,
};

enum class LayoutAlign
{
    Auto,
    Start,
    Center,
    End,
    Stretch,
};

struct FlexItem
{
    float basis = 0.0F;
    float grow = 0.0F;
    float shrink = 1.0F;
    float minimum_size = 0.0F;
    float maximum_size = std::numeric_limits<float>::infinity();
    float cross_size = -1.0F;
    float minimum_cross_size = 0.0F;
    float maximum_cross_size = std::numeric_limits<float>::infinity();
    LayoutAlign align_self = LayoutAlign::Auto;

    [[nodiscard]] static constexpr FlexItem fixed(float size) noexcept
    {
        FlexItem item;
        item.basis = size;
        item.maximum_size = size;
        return item;
    }

    [[nodiscard]] static constexpr FlexItem flexible(
        float grow_factor = 1.0F,
        float basis_size = 0.0F,
        float minimum = 0.0F,
        float maximum = std::numeric_limits<float>::infinity()) noexcept
    {
        FlexItem item;
        item.basis = basis_size;
        item.grow = grow_factor;
        item.minimum_size = minimum;
        item.maximum_size = maximum;
        return item;
    }
};

struct FlexOptions
{
    LayoutAxis axis = LayoutAxis::Horizontal;
    LayoutJustify justify_content = LayoutJustify::Start;
    LayoutAlign align_items = LayoutAlign::Stretch;
    float gap = 0.0F;
    bool reverse = false;
};

struct FlexLayoutResult
{
    std::vector<UI::Rect> items;
    float used_main_size = 0.0F;
    float remaining_main_size = 0.0F;
    float overflow = 0.0F;
};

class Flex final
{
public:
    [[nodiscard]] static FlexLayoutResult calculate(
        const UI::Rect& container,
        std::span<const FlexItem> items,
        const FlexOptions& options = {})
    {
        FlexLayoutResult result;
        result.items.resize(items.size());
        if (items.empty())
        {
            return result;
        }

        const float container_main = sanitize_size(options.axis == LayoutAxis::Horizontal
            ? container.width
            : container.height);
        const float container_cross = sanitize_size(options.axis == LayoutAxis::Horizontal
            ? container.height
            : container.width);
        const float gap = sanitize_size(options.gap);
        const float total_gap = gap * static_cast<float>(items.size() - 1);
        const float item_space = std::max(container_main - total_gap, 0.0F);

        std::vector<float> sizes(items.size(), 0.0F);
        for (std::size_t index = 0; index < items.size(); ++index)
        {
            const float minimum = sanitize_size(items[index].minimum_size);
            const float maximum = sanitize_maximum(items[index].maximum_size, minimum);
            sizes[index] = std::clamp(sanitize_size(items[index].basis), minimum, maximum);
        }

        float occupied = std::accumulate(sizes.begin(), sizes.end(), 0.0F);
        if (occupied < item_space)
        {
            distribute_growth(items, sizes, item_space - occupied);
        }
        else if (occupied > item_space)
        {
            distribute_shrink(items, sizes, occupied - item_space);
        }

        occupied = std::accumulate(sizes.begin(), sizes.end(), 0.0F);
        result.used_main_size = occupied + total_gap;
        result.remaining_main_size = std::max(container_main - result.used_main_size, 0.0F);
        result.overflow = std::max(result.used_main_size - container_main, 0.0F);

        float leading_space = 0.0F;
        float distributed_gap = gap;
        calculate_justification(
            options.justify_content,
            result.remaining_main_size,
            items.size(),
            leading_space,
            distributed_gap);

        std::vector<std::size_t> order(items.size());
        std::iota(order.begin(), order.end(), 0U);
        if (options.reverse)
        {
            std::reverse(order.begin(), order.end());
        }

        float cursor = leading_space;
        for (const std::size_t item_index : order)
        {
            const FlexItem& item = items[item_index];
            const LayoutAlign alignment = item.align_self == LayoutAlign::Auto
                ? normalized_alignment(options.align_items)
                : normalized_alignment(item.align_self);
            const float minimum_cross = sanitize_size(item.minimum_cross_size);
            const float maximum_cross = sanitize_maximum(
                item.maximum_cross_size,
                minimum_cross);
            const bool automatic_cross = !std::isfinite(item.cross_size) || item.cross_size < 0.0F;
            const float requested_cross = automatic_cross
                ? (alignment == LayoutAlign::Stretch ? container_cross : minimum_cross)
                : sanitize_size(item.cross_size);
            const float effective_minimum_cross = std::min(minimum_cross, container_cross);
            const float effective_maximum_cross = std::max(
                effective_minimum_cross,
                std::min(maximum_cross, container_cross));
            const float cross_size = std::clamp(
                requested_cross,
                effective_minimum_cross,
                effective_maximum_cross);
            float cross_offset = 0.0F;
            if (alignment == LayoutAlign::Center)
            {
                cross_offset = (container_cross - cross_size) * 0.5F;
            }
            else if (alignment == LayoutAlign::End)
            {
                cross_offset = container_cross - cross_size;
            }

            result.items[item_index] = options.axis == LayoutAxis::Horizontal
                ? UI::Rect{
                    container.x + cursor,
                    container.y + cross_offset,
                    sizes[item_index],
                    cross_size,
                }
                : UI::Rect{
                    container.x + cross_offset,
                    container.y + cursor,
                    cross_size,
                    sizes[item_index],
                };
            cursor += sizes[item_index] + distributed_gap;
        }
        return result;
    }

private:
    static constexpr float epsilon = 0.0001F;

    [[nodiscard]] static float sanitize_size(float value) noexcept
    {
        return std::isfinite(value) ? std::max(value, 0.0F) : 0.0F;
    }

    [[nodiscard]] static float sanitize_factor(float value) noexcept
    {
        return std::isfinite(value) ? std::max(value, 0.0F) : 0.0F;
    }

    [[nodiscard]] static float sanitize_maximum(float value, float minimum) noexcept
    {
        if (std::isnan(value))
        {
            return minimum;
        }
        return std::max(value, minimum);
    }

    [[nodiscard]] static LayoutAlign normalized_alignment(LayoutAlign alignment) noexcept
    {
        return alignment == LayoutAlign::Auto ? LayoutAlign::Stretch : alignment;
    }

    static void distribute_growth(
        std::span<const FlexItem> items,
        std::vector<float>& sizes,
        float free_space)
    {
        while (free_space > epsilon)
        {
            float total_weight = 0.0F;
            for (std::size_t index = 0; index < items.size(); ++index)
            {
                const float maximum = sanitize_maximum(
                    items[index].maximum_size,
                    sanitize_size(items[index].minimum_size));
                if (sizes[index] + epsilon < maximum)
                {
                    total_weight += sanitize_factor(items[index].grow);
                }
            }
            if (total_weight <= epsilon)
            {
                break;
            }

            float consumed = 0.0F;
            for (std::size_t index = 0; index < items.size(); ++index)
            {
                const float weight = sanitize_factor(items[index].grow);
                const float maximum = sanitize_maximum(
                    items[index].maximum_size,
                    sanitize_size(items[index].minimum_size));
                if (weight <= 0.0F || sizes[index] + epsilon >= maximum)
                {
                    continue;
                }
                const float addition = std::min(
                    free_space * weight / total_weight,
                    maximum - sizes[index]);
                sizes[index] += addition;
                consumed += addition;
            }
            if (consumed <= epsilon)
            {
                break;
            }
            free_space -= consumed;
        }
    }

    static void distribute_shrink(
        std::span<const FlexItem> items,
        std::vector<float>& sizes,
        float deficit)
    {
        while (deficit > epsilon)
        {
            float total_weight = 0.0F;
            for (std::size_t index = 0; index < items.size(); ++index)
            {
                const float minimum = sanitize_size(items[index].minimum_size);
                if (sizes[index] > minimum + epsilon)
                {
                    total_weight += sanitize_factor(items[index].shrink) *
                        std::max(sizes[index], 1.0F);
                }
            }
            if (total_weight <= epsilon)
            {
                break;
            }

            float recovered = 0.0F;
            for (std::size_t index = 0; index < items.size(); ++index)
            {
                const float minimum = sanitize_size(items[index].minimum_size);
                const float weight = sanitize_factor(items[index].shrink) *
                    std::max(sizes[index], 1.0F);
                if (weight <= 0.0F || sizes[index] <= minimum + epsilon)
                {
                    continue;
                }
                const float reduction = std::min(
                    deficit * weight / total_weight,
                    sizes[index] - minimum);
                sizes[index] -= reduction;
                recovered += reduction;
            }
            if (recovered <= epsilon)
            {
                break;
            }
            deficit -= recovered;
        }
    }

    static void calculate_justification(
        LayoutJustify justification,
        float remaining_space,
        std::size_t item_count,
        float& leading_space,
        float& gap) noexcept
    {
        if (remaining_space <= 0.0F)
        {
            return;
        }
        switch (justification)
        {
        case LayoutJustify::Start: break;
        case LayoutJustify::Center: leading_space = remaining_space * 0.5F; break;
        case LayoutJustify::End: leading_space = remaining_space; break;
        case LayoutJustify::SpaceBetween:
            if (item_count > 1)
            {
                gap += remaining_space / static_cast<float>(item_count - 1);
            }
            break;
        case LayoutJustify::SpaceAround:
            if (item_count > 0)
            {
                const float spacing = remaining_space / static_cast<float>(item_count);
                leading_space = spacing * 0.5F;
                gap += spacing;
            }
            break;
        case LayoutJustify::SpaceEvenly:
            if (item_count > 0)
            {
                const float spacing = remaining_space / static_cast<float>(item_count + 1);
                leading_space = spacing;
                gap += spacing;
            }
            break;
        }
    }
};

} // namespace Zenvra::Utility
