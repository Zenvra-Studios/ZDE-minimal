#ifndef ZENVRA_UTILITY_MATH_H
#define ZENVRA_UTILITY_MATH_H

#include <cmath>

namespace Zenvra::Utility
{
    /**
     * @brief Rounds a floating-point value to the nearest integer.
     * @param value The floating-point value to round.
     * @return The rounded integer value.
     */
    inline int round_to_int(float value)
    {
        return static_cast<int>(std::lround(value));
    }
} // namespace Zenvra::Utility

#endif // ZENVRA_UTILITY_MATH_H
