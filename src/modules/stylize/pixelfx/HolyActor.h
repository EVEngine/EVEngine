// Native adaptation of pixelfx holy/_lib.js actor silhouettes and floor ovals, MIT.
// Copyright (c) 2026 Patch; see LICENSE.txt.
#pragma once
#include "stylize/pixelfx/Painter.h"
namespace eve::stylize::pixelfx::holy {
/** @brief Query source mask coordinates through a synchronous actor borrow, including fractional-index semantics. */
inline bool containsActorCell(const PixelFxActor& a, double x, double y) {
    const double i = std::floor(x) - a.x, j = std::floor(y) - a.y;
    if (i < 0 || j < 0 || i >= a.width || j >= a.height) return false;
    const double index = j * a.width + i;
    return index >= 0 && index < double(a.mask.size()) && index == std::floor(index) && a.mask[size_t(index)] > 0;
}
/** @brief Owning immutable derived outline; row-major points match source shapeOf order, with no retained actor
 * reference. */
struct ActorShape {
    double             x, y, top, bottom;
    std::vector<Point> edge, out;
};
/** @brief Prepare source four-neighbor rim and outside ring from the caller's owning mask snapshot.
 * @cost Linear in bounded actor cell count per configuration preparation; resulting points are reused per frame. */
inline ActorShape prepareShape(const PixelFxActor& a) {
    ActorShape shape{a.x, a.y, a.y, a.y + a.height, {}, {}};
    const auto inside = [&](int i, int j) {
        return i >= 0 && j >= 0 && i < a.width && j < a.height && a.mask[size_t(j) * size_t(a.width) + size_t(i)] > 0;
    };
    for (int j = -1; j <= a.height; ++j)
        for (int i = -1; i <= a.width; ++i) {
            const int count =
                int(inside(i - 1, j)) + int(inside(i + 1, j)) + int(inside(i, j - 1)) + int(inside(i, j + 1));
            if (inside(i, j)) {
                if (count < 4) shape.edge.push_back({a.x + i, a.y + j});
            } else if (count)
                shape.out.push_back({a.x + i, a.y + j});
        }
    return shape;
}
/** @brief Draw a source floor oval with synchronous color, alpha and angle-filter borrows.
 * @note Nonfinite radii emit no cells, matching source loops with NaN bounds. */
template <class C, class A, class Keep>
void oval(Paint& p, double cx, double cy, double rx, double ry, const C& color, const A& alpha, Keep keep) {
    if (rx < .5 || !std::isfinite(rx) || !std::isfinite(ry)) return;
    const double n = std::ceil(std::max(rx, ry) * TAU * 1.5) + 6;
    for (double j = 0; j < n; ++j) {
        const double g = (j / n) * TAU;
        if (!keep(g)) continue;
        p.dot(cx + std::cos(g) * rx, cy + std::sin(g) * ry, sample(color, g), sample(alpha, g));
    }
}
/** @brief Recolor nonzero source actor pixels through synchronous compiled color/alpha callbacks. */
template <class F, class A>
void recolor(Paint& p, const PixelFxActor& actor, F color, const A& alpha) {
    for (int j = 0; j < actor.height; ++j)
        for (int i = 0; i < actor.width; ++i) {
            const auto c = actor.pixels[size_t(j) * size_t(actor.width) + size_t(i)];
            if (!c) continue;
            const double x = actor.x + i, y = actor.y + j;
            const auto   value = color(c, x, y);
            if (value) p.dot(x, y, value, sample(alpha, x, y));
        }
}
}  // namespace eve::stylize::pixelfx::holy
