#pragma once

namespace Zenvra::UI
{

struct Rect
{
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;

    [[nodiscard]] constexpr float right() const noexcept
    {
        return x + width;
    }

    [[nodiscard]] constexpr float bottom() const noexcept
    {
        return y + height;
    }

    [[nodiscard]] constexpr bool contains(float point_x, float point_y) const noexcept
    {
        return point_x >= x && point_x < right() && point_y >= y && point_y < bottom();
    }

    [[nodiscard]] constexpr bool is_empty() const noexcept
    {
        return width <= 0.0F || height <= 0.0F;
    }
};

} // namespace Zenvra::UI
