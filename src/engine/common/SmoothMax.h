#pragma once

#include "common/Assert.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace eve::math {

/**
 * @brief C1 polynomial maximum for blending two height fields.
 * @param a Finite first height.
 * @param b Finite second height, in the same coordinate system and units.
 * @param width Finite nonnegative height-difference band; zero selects ordinary max.
 * @return max(a,b) outside the band, otherwise max(a,b) + width*h*h/4,
 * where h = 1-abs(a-b)/width. Equal heights rise by width/4. Overflow returns +infinity.
 * @thread Stateless and thread-safe; retains nothing and invokes no callbacks.
 * @note Constant work per sample, no allocations or RNG. Symmetric but not associative.
 * Results are deterministic on one toolchain; compare across platforms with tolerance.
 * Preconditions are checked with EV_PARAM_CHECK in assertion-enabled builds.
 */
[[nodiscard]] inline float smoothMax(float a, float b, float width) {
    EV_PARAM_CHECK(std::isfinite(a));
    EV_PARAM_CHECK(std::isfinite(b));
    EV_PARAM_CHECK(std::isfinite(width));
    EV_PARAM_CHECK(width >= 0.f);
    const double top        = std::max(a, b);
    const double difference = std::abs(static_cast<double>(a) - static_cast<double>(b));
    if (width == 0.f || difference >= width) return static_cast<float>(top);
    const double h      = 1.0 - difference / width;
    const double result = top + (static_cast<double>(width) * 0.25) * h * h;
    if (result > std::numeric_limits<float>::max()) return std::numeric_limits<float>::infinity();
    return static_cast<float>(result);
}

}  // namespace eve::math
