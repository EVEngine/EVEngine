// eve-hot-path
// Native adaptation of pixelfx status/_lib.js icon/bitmap/shaped/starShape/heartShape, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#pragma once
#include <array>
#include <cmath>
#include <optional>
#include <span>
#include <string_view>
#include "stylize/pixelfx/Painter.h"
namespace eve::stylize::pixelfx::status {
/** @brief Sample a borrowed immutable character mask synchronously; zero denotes an empty cell. */
[[nodiscard]] inline char bitmap(std::span<const std::string_view> rows, int dx, int dy, int scale = 1,
                                 bool flip = false) {
    const int height = int(rows.size()), width = int(rows[0].size());
    const int ox = (width * scale) / 2, oy = (height * scale) / 2;
    int       x = int(std::floor(double(dx + ox) / scale));
    const int y = int(std::floor(double(dy + oy) / scale));
    if (x < 0 || y < 0 || x >= width || y >= height) return 0;
    if (flip) x = width - 1 - x;
    const char c = rows[size_t(y)][size_t(x)];
    return c == '.' ? 0 : c;
}
/** @brief Draw the source outlined/shaded icon with synchronous non-retained typed shape/color callbacks.
 * @note No locks or script callbacks; inputs must be trusted compiled functions and immutable ramps.
 * @cost Raster work quadratic in authored logical extent per draw, independent of target pixel scaling. */
template <class Inside, class Color>
void icon(Paint& p, double x, double y, int extent, Inside inside, const std::array<uint32_t, 4>& ramp, double alpha,
          Color color) {
    const double X = jsRound(x), Y = jsRound(y);
    for (int dy = -extent - 1; dy <= extent + 1; ++dy)
        for (int dx = -extent - 1; dx <= extent + 1; ++dx) {
            const char ch = inside(dx, dy);
            uint32_t   c;
            if (ch) {
                const auto special = color(ch);
                if (special)
                    c = *special;
                else {
                    const bool top = !inside(dx, dy - 1), left = !inside(dx - 1, dy), bottom = !inside(dx, dy + 1),
                               right = !inside(dx + 1, dy);
                    c                = top || left ? ramp[3] : bottom || right ? ramp[1] : ramp[2];
                }
            } else if (ramp[0] &&
                       (inside(dx - 1, dy) || inside(dx + 1, dy) || inside(dx, dy - 1) || inside(dx, dy + 1)))
                c = ramp[0];
            else
                continue;
            p.dot(X + dx, Y + dy, c, alpha);
        }
}
/** @brief Draw the source shaded icon without special character recoloring; callback borrows are synchronous.
 * @cost Raster work quadratic in authored logical extent per draw, amortized by logical pixel rendering. */
template <class Inside>
void icon(Paint& p, double x, double y, int extent, Inside inside, const std::array<uint32_t, 4>& ramp,
          double alpha = 1) {
    icon(p, x, y, extent, inside, ramp, alpha, [](char) -> std::optional<uint32_t> { return std::nullopt; });
}
/** @brief Prepare a cheap, allocation-free transformed predicate for one synchronous icon draw. */
template <class Shape>
[[nodiscard]] auto shaped(Shape shape, double scale, double angle = 0) {
    const double c = std::cos(angle), n = std::sin(angle);
    return [=](int dx, int dy) -> char {
        const double x = dx + .5, y = dy + .5;
        return shape((x * c + y * n) / scale, (-x * n + y * c) / scale) ? '#' : 0;
    };
}
/** @brief Evaluate the source five-point star in unit space, with injected inner radius. */
[[nodiscard]] inline bool isInsideStar(double u, double v, double inner = .45) {
    const double a = std::atan2(v, u) + PI / 2, d = jsHypot2(u, v), segment = (2 * PI) / 5;
    const double k = 1 - std::abs(wrap(a, segment) - segment / 2) / (segment / 2);
    return d <= 1 - (1 - inner) * std::pow(k, .8);
}
/** @brief Evaluate the source two-lobe heart in unit space; NaN comparisons preserve source non-writes. */
[[nodiscard]] inline bool isInsideHeart(double u, double v) {
    return std::pow(u + .5, 2) + std::pow(v + .32, 2) <= std::pow(.55, 2) ||
           std::pow(u - .5, 2) + std::pow(v + .32, 2) <= std::pow(.55, 2) ||
           (v >= -.32 && v <= 1 && std::abs(u) <= 1.04 * std::pow(1 - (v + .32) / 1.32, .75));
}
}  // namespace eve::stylize::pixelfx::status
