#pragma once
#include <array>
namespace eve::fluids::ocean_detail {
inline std::array<double, 4> weights(double t) {
    const double t2 = t * t, t3 = t2 * t;
    return {-0.5 * t + t2 - 0.5 * t3, 1.0 - 2.5 * t2 + 1.5 * t3, 0.5 * t + 2.0 * t2 - 1.5 * t3, -0.5 * t2 + 0.5 * t3};
}
inline std::array<double, 4> derivatives(double t) {
    const double t2 = t * t;
    return {-0.5 + 2.0 * t - 1.5 * t2, -5.0 * t + 4.5 * t2, 0.5 + 4.0 * t - 4.5 * t2, -t + 1.5 * t2};
}
}  // namespace eve::fluids::ocean_detail
