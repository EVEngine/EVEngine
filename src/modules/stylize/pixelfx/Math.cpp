// Source-compatible numeric primitives for native pixelfx, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Math.h"

namespace eve::stylize::pixelfx {
double jsHypot2(double x, double y) noexcept {
    x = std::abs(x);
    y = std::abs(y);
    if (std::isinf(x) || std::isinf(y)) return INFINITY;
    if (std::isnan(x) || std::isnan(y)) return NAN;
    const double scale = std::max(x, y);
    if (scale == 0) return 0;
    const double a = x / scale, b = y / scale;
    return std::sqrt(a * a + b * b) * scale;
}

double jsHypot3(double x, double y, double z) noexcept {
    x = std::abs(x);
    y = std::abs(y);
    z = std::abs(z);
    if (std::isinf(x) || std::isinf(y) || std::isinf(z)) return INFINITY;
    if (std::isnan(x) || std::isnan(y) || std::isnan(z)) return NAN;
    const double scale = std::max({x, y, z});
    if (scale == 0) return 0;
    const double a = x / scale, b = y / scale, c = z / scale;
    const double aa = a * a, bb = b * b, sum = aa + bb;
    const double correction = (sum - aa) - bb;
    return std::sqrt(sum + (c * c - correction)) * scale;
}
}  // namespace eve::stylize::pixelfx
