// eve-hot-path
// Native adaptation of pixelfx transitions/_lib.js polygon helpers, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#pragma once
#include <type_traits>
#include "stylize/pixelfx/ScreenTransition.h"
namespace eve::stylize::pixelfx::screen {
/** @brief Clip an owning polygon to a directed half-plane in source edge/insertion order.
 * @note Input is borrowed only synchronously; no callbacks or retained pointers.
 * @cost Copies bounded authored vertices once per cold geometry preparation. */
inline std::vector<Point> halfPlane(std::span<const Point> pts, double cx, double cy, double nx, double ny) {
    std::vector<Point> out;
    out.reserve(pts.size() + 2);
    for (size_t i = 0; i < pts.size(); ++i) {
        const auto   a = pts[i], b = pts[(i + 1) % pts.size()];
        const double sa = (a.x - cx) * nx + (a.y - cy) * ny, sb = (b.x - cx) * nx + (b.y - cy) * ny;
        if (sa >= 0) out.push_back(a);
        if ((sa >= 0) != (sb >= 0)) {
            const double k = sa / (sa - sb);
            out.push_back({a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k});
        }
    }
    return out;
}
/** @brief Source pixel-center convex fill, clipped in authored screen space; zero colors skip.
 * @note Painter/vertices and trusted compiled sampler are borrowed synchronously on the owner thread.
 * @cost Linear in covered screen pixels times authored vertex count per draw; no allocation. */
template <class Color>
void fillConvex(Paint& p, std::span<const Point> pts, Color color, double alpha = 1) {
    if (pts.empty()) return;
    double y0 = INFINITY, y1 = -INFINITY;
    for (const auto& q : pts) {
        y0 = std::min(y0, q.y);
        y1 = std::max(y1, q.y);
    }
    const int r0 = std::max(0, int(std::ceil(y0 - .5))), r1 = std::min(H - 1, int(std::floor(y1 - .5)));
    for (int y = r0; y <= r1; ++y) {
        const double yc = y + .5;
        double       xl = INFINITY, xr = -INFINITY;
        for (size_t i = 0, j = pts.size() - 1; i < pts.size(); j = i++) {
            const auto a = pts[i], b = pts[j];
            if ((a.y > yc) == (b.y > yc)) continue;
            const double x = a.x + ((yc - a.y) * (b.x - a.x)) / (b.y - a.y);
            xl             = std::min(xl, x);
            xr             = std::max(xr, x);
        }
        if (xl > xr) continue;
        const int  c0 = std::max(0, int(std::ceil(xl - .5))), c1 = std::min(W - 1, int(std::floor(xr - .5)));
        const bool unrotated = p.sn == 0 && p.cs == 1;
        const int  b = int(std::floor(p.tx + (p.fx > 0 ? .5 : -.5))), sy = int(std::floor(p.ty + yc));
        for (int x = c0; x <= c1; ++x) {
            const auto col = [&]() -> uint32_t {
                if constexpr (std::is_invocable_v<Color, double, double>)
                    return color(x + .5, yc);
                else
                    return uint32_t(color);
            }();
            if (!col) continue;
            if (unrotated)
                p.put(b + int(p.fx) * x, sy, col, alpha);
            else
                p.dot(x, y, col, alpha);
        }
    }
}
}  // namespace eve::stylize::pixelfx::screen
