#pragma once

#include "Utility/Flex.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numeric>
#include <span>
#include <vector>

namespace Zenvra::Utility
{

enum class GridTrackUnit
{
    Fixed,
    Fraction,
    Auto,
};

struct GridTrack
{
    GridTrackUnit unit = GridTrackUnit::Fraction;
    float value = 1.0F;
    float minimum_size = 0.0F;
    float maximum_size = std::numeric_limits<float>::infinity();

    [[nodiscard]] static constexpr GridTrack fixed(float size) noexcept
    {
        return GridTrack{
            .unit = GridTrackUnit::Fixed,
            .value = size,
            .minimum_size = size,
            .maximum_size = size,
        };
    }

    [[nodiscard]] static constexpr GridTrack fraction(
        float fraction_value = 1.0F,
        float minimum = 0.0F,
        float maximum = std::numeric_limits<float>::infinity()) noexcept
    {
        return GridTrack{
            .unit = GridTrackUnit::Fraction,
            .value = fraction_value,
            .minimum_size = minimum,
            .maximum_size = maximum,
        };
    }

    [[nodiscard]] static constexpr GridTrack automatic(
        float minimum = 0.0F,
        float maximum = std::numeric_limits<float>::infinity()) noexcept
    {
        return GridTrack{
            .unit = GridTrackUnit::Auto,
            .value = 0.0F,
            .minimum_size = minimum,
            .maximum_size = maximum,
        };
    }
};

struct GridItem
{
    std::size_t column = 0;
    std::size_t row = 0;
    std::size_t column_span = 1;
    std::size_t row_span = 1;
    float preferred_width = -1.0F;
    float preferred_height = -1.0F;
    LayoutAlign horizontal_alignment = LayoutAlign::Auto;
    LayoutAlign vertical_alignment = LayoutAlign::Auto;
};

struct GridOptions
{
    float column_gap = 0.0F;
    float row_gap = 0.0F;
    LayoutAlign horizontal_alignment = LayoutAlign::Stretch;
    LayoutAlign vertical_alignment = LayoutAlign::Stretch;
};

struct GridLayoutResult
{
    std::vector<UI::Rect> items;
    std::vector<float> column_positions;
    std::vector<float> column_sizes;
    std::vector<float> row_positions;
    std::vector<float> row_sizes;
    float horizontal_overflow = 0.0F;
    float vertical_overflow = 0.0F;
};

class Grid final
{
public:
    [[nodiscard]] static GridLayoutResult calculate(
        const UI::Rect& container,
        std::span<const GridTrack> columns,
        std::span<const GridTrack> rows,
        std::span<const GridItem> items,
        const GridOptions& options = {})
    {
        GridLayoutResult result;
        result.items.resize(items.size());
        const float column_gap = sanitize_size(options.column_gap);
        const float row_gap = sanitize_size(options.row_gap);

        std::vector<Intrinsic> horizontal_intrinsics;
        std::vector<Intrinsic> vertical_intrinsics;
        horizontal_intrinsics.reserve(items.size());
        vertical_intrinsics.reserve(items.size());
        for (const GridItem& item : items)
        {
            horizontal_intrinsics.push_back(Intrinsic{
                .start = item.column,
                .span = std::max(item.column_span, std::size_t{1}),
                .preferred_size = item.preferred_width,
            });
            vertical_intrinsics.push_back(Intrinsic{
                .start = item.row,
                .span = std::max(item.row_span, std::size_t{1}),
                .preferred_size = item.preferred_height,
            });
        }

        const TrackResult horizontal = calculate_tracks(
            sanitize_size(container.width), columns, horizontal_intrinsics, column_gap);
        const TrackResult vertical = calculate_tracks(
            sanitize_size(container.height), rows, vertical_intrinsics, row_gap);
        result.column_sizes = horizontal.sizes;
        result.row_sizes = vertical.sizes;
        result.column_positions = calculate_positions(container.x, horizontal.sizes, column_gap);
        result.row_positions = calculate_positions(container.y, vertical.sizes, row_gap);
        result.horizontal_overflow = horizontal.overflow;
        result.vertical_overflow = vertical.overflow;

        for (std::size_t index = 0; index < items.size(); ++index)
        {
            const GridItem& item = items[index];
            if (item.column >= columns.size() || item.row >= rows.size())
            {
                continue;
            }
            const std::size_t column_span = std::min(
                std::max(item.column_span, std::size_t{1}),
                columns.size() - item.column);
            const std::size_t row_span = std::min(
                std::max(item.row_span, std::size_t{1}),
                rows.size() - item.row);
            const UI::Rect cell{
                result.column_positions[item.column],
                result.row_positions[item.row],
                sum_span(result.column_sizes, item.column, column_span, column_gap),
                sum_span(result.row_sizes, item.row, row_span, row_gap),
            };
            result.items[index] = align_item(cell, item, options);
        }
        return result;
    }

private:
    struct Intrinsic
    {
        std::size_t start = 0;
        std::size_t span = 1;
        float preferred_size = -1.0F;
    };

    struct TrackResult
    {
        std::vector<float> sizes;
        float overflow = 0.0F;
    };

    static constexpr float epsilon = 0.0001F;

    [[nodiscard]] static float sanitize_size(float value) noexcept
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

    [[nodiscard]] static TrackResult calculate_tracks(
        float available_size,
        std::span<const GridTrack> tracks,
        std::span<const Intrinsic> intrinsics,
        float gap)
    {
        TrackResult result;
        result.sizes.resize(tracks.size(), 0.0F);
        if (tracks.empty())
        {
            return result;
        }
        for (std::size_t index = 0; index < tracks.size(); ++index)
        {
            const float minimum = sanitize_size(tracks[index].minimum_size);
            const float maximum = sanitize_maximum(tracks[index].maximum_size, minimum);
            const float requested = tracks[index].unit == GridTrackUnit::Fixed
                ? sanitize_size(tracks[index].value)
                : minimum;
            result.sizes[index] = std::clamp(requested, minimum, maximum);
        }

        for (const Intrinsic& intrinsic : intrinsics)
        {
            if (intrinsic.preferred_size < 0.0F || intrinsic.start >= tracks.size())
            {
                continue;
            }
            const std::size_t span = std::min(intrinsic.span, tracks.size() - intrinsic.start);
            const float current = sum_span(result.sizes, intrinsic.start, span, gap);
            float missing = std::max(sanitize_size(intrinsic.preferred_size) - current, 0.0F);
            if (missing <= epsilon)
            {
                continue;
            }
            distribute_intrinsic(tracks, result.sizes, intrinsic.start, span, missing,
                GridTrackUnit::Auto);
            const float after_auto = sum_span(result.sizes, intrinsic.start, span, gap);
            missing = std::max(sanitize_size(intrinsic.preferred_size) - after_auto, 0.0F);
            distribute_intrinsic(tracks, result.sizes, intrinsic.start, span, missing,
                GridTrackUnit::Fraction);
        }

        const float total_gap = gap * static_cast<float>(tracks.size() - 1);
        const float track_space = std::max(available_size - total_gap, 0.0F);
        float occupied = std::accumulate(result.sizes.begin(), result.sizes.end(), 0.0F);
        if (occupied < track_space)
        {
            distribute_fraction_space(tracks, result.sizes, track_space - occupied);
        }
        else if (occupied > track_space)
        {
            shrink_tracks(tracks, result.sizes, occupied - track_space, false);
            occupied = std::accumulate(result.sizes.begin(), result.sizes.end(), 0.0F);
            if (occupied > track_space + epsilon)
            {
                shrink_tracks(tracks, result.sizes, occupied - track_space, true);
            }
        }
        occupied = std::accumulate(result.sizes.begin(), result.sizes.end(), 0.0F) + total_gap;
        result.overflow = std::max(occupied - available_size, 0.0F);
        return result;
    }

    static void distribute_intrinsic(
        std::span<const GridTrack> tracks,
        std::vector<float>& sizes,
        std::size_t start,
        std::size_t span,
        float missing,
        GridTrackUnit target_unit)
    {
        while (missing > epsilon)
        {
            std::size_t candidate_count = 0;
            for (std::size_t offset = 0; offset < span; ++offset)
            {
                const std::size_t index = start + offset;
                const float maximum = sanitize_maximum(
                    tracks[index].maximum_size,
                    sanitize_size(tracks[index].minimum_size));
                if (tracks[index].unit == target_unit && sizes[index] + epsilon < maximum)
                {
                    ++candidate_count;
                }
            }
            if (candidate_count == 0)
            {
                break;
            }
            float consumed = 0.0F;
            const float share = missing / static_cast<float>(candidate_count);
            for (std::size_t offset = 0; offset < span; ++offset)
            {
                const std::size_t index = start + offset;
                const float maximum = sanitize_maximum(
                    tracks[index].maximum_size,
                    sanitize_size(tracks[index].minimum_size));
                if (tracks[index].unit != target_unit || sizes[index] + epsilon >= maximum)
                {
                    continue;
                }
                const float addition = std::min(share, maximum - sizes[index]);
                sizes[index] += addition;
                consumed += addition;
            }
            if (consumed <= epsilon)
            {
                break;
            }
            missing -= consumed;
        }
    }

    static void distribute_fraction_space(
        std::span<const GridTrack> tracks,
        std::vector<float>& sizes,
        float free_space)
    {
        while (free_space > epsilon)
        {
            float total_fraction = 0.0F;
            for (std::size_t index = 0; index < tracks.size(); ++index)
            {
                const float maximum = sanitize_maximum(
                    tracks[index].maximum_size,
                    sanitize_size(tracks[index].minimum_size));
                if (tracks[index].unit == GridTrackUnit::Fraction &&
                    sizes[index] + epsilon < maximum)
                {
                    total_fraction += sanitize_size(tracks[index].value);
                }
            }
            if (total_fraction <= epsilon)
            {
                break;
            }
            float consumed = 0.0F;
            for (std::size_t index = 0; index < tracks.size(); ++index)
            {
                const float fraction = sanitize_size(tracks[index].value);
                const float maximum = sanitize_maximum(
                    tracks[index].maximum_size,
                    sanitize_size(tracks[index].minimum_size));
                if (tracks[index].unit != GridTrackUnit::Fraction ||
                    fraction <= 0.0F || sizes[index] + epsilon >= maximum)
                {
                    continue;
                }
                const float addition = std::min(
                    free_space * fraction / total_fraction,
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

    static void shrink_tracks(
        std::span<const GridTrack> tracks,
        std::vector<float>& sizes,
        float deficit,
        bool include_fixed)
    {
        while (deficit > epsilon)
        {
            float total_weight = 0.0F;
            for (std::size_t index = 0; index < tracks.size(); ++index)
            {
                const float minimum = sanitize_size(tracks[index].minimum_size);
                if ((include_fixed || tracks[index].unit != GridTrackUnit::Fixed) &&
                    sizes[index] > minimum + epsilon)
                {
                    total_weight += std::max(sizes[index], 1.0F);
                }
            }
            if (total_weight <= epsilon)
            {
                break;
            }
            float recovered = 0.0F;
            for (std::size_t index = 0; index < tracks.size(); ++index)
            {
                const float minimum = sanitize_size(tracks[index].minimum_size);
                if ((!include_fixed && tracks[index].unit == GridTrackUnit::Fixed) ||
                    sizes[index] <= minimum + epsilon)
                {
                    continue;
                }
                const float weight = std::max(sizes[index], 1.0F);
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

    [[nodiscard]] static std::vector<float> calculate_positions(
        float origin,
        std::span<const float> sizes,
        float gap)
    {
        std::vector<float> positions(sizes.size(), origin);
        float cursor = origin;
        for (std::size_t index = 0; index < sizes.size(); ++index)
        {
            positions[index] = cursor;
            cursor += sizes[index] + gap;
        }
        return positions;
    }

    [[nodiscard]] static float sum_span(
        std::span<const float> sizes,
        std::size_t start,
        std::size_t span,
        float gap) noexcept
    {
        float total = gap * static_cast<float>(span > 0 ? span - 1 : 0);
        for (std::size_t offset = 0; offset < span && start + offset < sizes.size(); ++offset)
        {
            total += sizes[start + offset];
        }
        return total;
    }

    [[nodiscard]] static UI::Rect align_item(
        const UI::Rect& cell,
        const GridItem& item,
        const GridOptions& options) noexcept
    {
        const LayoutAlign horizontal = item.horizontal_alignment == LayoutAlign::Auto
            ? options.horizontal_alignment
            : item.horizontal_alignment;
        const LayoutAlign vertical = item.vertical_alignment == LayoutAlign::Auto
            ? options.vertical_alignment
            : item.vertical_alignment;
        const float width = horizontal == LayoutAlign::Stretch || item.preferred_width < 0.0F
            ? cell.width
            : std::min(sanitize_size(item.preferred_width), cell.width);
        const float height = vertical == LayoutAlign::Stretch || item.preferred_height < 0.0F
            ? cell.height
            : std::min(sanitize_size(item.preferred_height), cell.height);
        float x = cell.x;
        float y = cell.y;
        if (horizontal == LayoutAlign::Center) x += (cell.width - width) * 0.5F;
        else if (horizontal == LayoutAlign::End) x += cell.width - width;
        if (vertical == LayoutAlign::Center) y += (cell.height - height) * 0.5F;
        else if (vertical == LayoutAlign::End) y += cell.height - height;
        return UI::Rect{x, y, width, height};
    }
};

} // namespace Zenvra::Utility
