// eve-hot-path
// Native adaptation of pixelfx nature/_lib.js drawLeaf, MIT.
// Copyright (c) 2026 Patch; see LICENSE.txt and NOTICE.txt.
#pragma once
#include "common/Assert.h"
#include "stylize/pixelfx/Painter.h"
namespace eve::stylize::pixelfx::nature {
/** @brief Draw the source turning leaf, shaded underside, midrib, dark rim and trailing stem.
 * @note Caller-thread painter/palette borrows only; no state or callbacks retained. Palette has four stops.
 * @cost Per draw visits the leaf's squared length bounding box; no allocation. */
inline void drawLeaf(Paint& p, double x, double y, double length, double width, double angle, double flip,
                     std::span<const uint32_t> palette, double alpha = 1) {
    EV_ASSERT((palette.size() >= 4 && length > 0));
    const double ca = std::cos(angle), sa = std::sin(angle), sq = std::cos(flip),
                 w     = std::max(.55, std::abs(sq) * width);
    const bool   under = sq < 0;
    const int    r     = int(std::ceil(length / 2 + 2));
    const double X = jsRound(x), Y = jsRound(y);
    const auto   inside = [&](int i, int j) {
        const double u = (i + .5) * ca + (j + .5) * sa + length / 2, v = -(i + .5) * sa + (j + .5) * ca;
        if (u < 0 || u > length) return 0;
        const double half = w * std::pow(std::sin((PI * u) / length), .75);
        return std::abs(v) <= half ? (v < 0 ? 1 : 2) : 0;
    };
    for (int j = -r; j <= r; ++j)
        for (int i = -r; i <= r; ++i) {
            const int s = inside(i, j);
            if (!s) {
                if (inside(i - 1, j) || inside(i + 1, j) || inside(i, j - 1) || inside(i, j + 1))
                    p.dot(X + i, Y + j, palette[0], alpha);
                continue;
            }
            const double u = (i + .5) * ca + (j + .5) * sa + length / 2, v = -(i + .5) * sa + (j + .5) * ca;
            const bool   rib = std::abs(v) < .5 && u > length * .12 && u < length * .88 && w > 1.2;
            p.dot(X + i, Y + j, palette[rib ? 1 : (s == 1) != under ? 3 : 2], alpha);
        }
    p.dot(X - ca * (length / 2 + 1), Y - sa * (length / 2 + 1), palette[1], alpha);
}
}  // namespace eve::stylize::pixelfx::nature
