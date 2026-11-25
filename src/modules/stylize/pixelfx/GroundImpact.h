// Native adaptation of pixelfx impacts/_lib.js ground helpers, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#pragma once
#include "stylize/pixelfx/Impact.h"
namespace eve::stylize::pixelfx::impact {
inline const uint32_t SOOT = hex(0x0d0a12);
struct CrackPoint {
    double x, y, distance;
};
struct Crack {
    std::vector<CrackPoint>              points;
    double                               length, delay;
    std::vector<std::vector<CrackPoint>> kids;
};
struct CrackRecipe {
    double minLength = 14, maxLength = 40, dive = .85, maxY = 22, forks = .5, minX = 1, maxX = 5;
};
/** @brief Prepare one owning jagged walk with injected RNG, retaining no input references.
 * @cost Allocates a bounded point sequence once per crack preparation. */
inline std::vector<CrackPoint> walk(Rng& rng, double x, double y, double angle, double length, double maxY,
                                    double d0 = 0, double jitter = .55, double minStep = 2, double maxStep = 4) {
    EV_PARAM_CHECK((length > 0 && length <= 128 && minStep > 0 && maxStep >= minStep),
                   "Compiled crack walk bounds must be finite and positive");
    std::vector<CrackPoint> out{{x, y, d0}};
    double                  d = 0, current = angle;
    while (d < length - .01) {
        const double step = std::min(length - d, rng.range(minStep, maxStep));
        current += rng.range(-jitter, jitter) + (angle - current) * .45;
        x += std::cos(current) * step;
        y = clamp(y + std::sin(current) * step, .5, maxY);
        d += step;
        out.push_back({x, y, d0 + d});
    }
    return out;
}
/** @brief Own source crack recipes, main paths and forks prepared from a caller RNG.
 * @cost Allocates bounded point sequences per configuration preparation, reused across frames. */
inline std::vector<Crack> makeCracks(Rng& rng, int count, const CrackRecipe& o) {
    EV_PARAM_CHECK((count > 0 && count <= 128), "Compiled crack count must be bounded");
    std::vector<Crack> out;
    out.reserve(size_t(count));
    for (int i = 0; i < count; ++i) {
        const int    side = i % 2 ? 1 : -1;
        const double dip  = .06 + std::pow(rng.next(), 1.7) * o.dive,
                     L    = rng.range(o.minLength, o.maxLength) * (1 - (.5 * dip) / (o.dive + .06)),
                     a = side > 0 ? dip : PI - dip, x = side * rng.range(o.minX, o.maxX), y = rng.range(.5, 1.5);
        auto         points = walk(rng, x, y, a, L, o.maxY);
        const double delay  = rng.range(0, .035);
        Crack        crack{std::move(points), L, delay, {}};
        for (size_t j = 2; j + 1 < crack.points.size(); ++j)
            if (rng.isChance(o.forks * .4)) {
                const auto&  p         = crack.points[j];
                const int    s         = rng.sign();
                const double direction = a + s * rng.range(.45, 1),
                             length    = rng.range(3, 9) * (1 - (.5 * double(j)) / double(crack.points.size()));
                crack.kids.push_back(walk(rng, p.x, p.y, direction, length, o.maxY, p.distance, .7, 1.5, 3));
            }
        out.push_back(std::move(crack));
    }
    return out;
}
/** @brief Rasterize prepared cracks with synchronous compiled growth/heat/painter borrows. */
template <class Growth, class Heat>
void drawCracks(Paint& p, std::span<const Crack> cracks, Growth growth, Heat heat, const std::array<uint32_t, 4>& hot,
                uint32_t dark, uint32_t lip, double alpha = 1) {
    if (alpha <= 0) return;
    const auto color = [&](double d) {
        const double h = heat(d);
        return h > .75 ? hot[0] : h > .5 ? hot[1] : h > .3 ? hot[2] : h > .12 ? hot[3] : dark;
    };
    for (const auto& c : cracks) {
        const double G = growth(c);
        if (G <= 0) continue;
        const auto path = [&](const std::vector<CrackPoint>& points) {
            for (size_t i = 1; i < points.size(); ++i) {
                const auto& a = points[i - 1];
                const auto& b = points[i];
                if (a.distance >= G) break;
                const double f  = b.distance > G ? (G - a.distance) / (b.distance - a.distance) : 1,
                             ex = a.x + (b.x - a.x) * f, ey = a.y + (b.y - a.y) * f;
                p.lineFn(a.x, a.y + 1, ex, ey + 1, lip, [&](double u) {
                    return rand({jsUint(jsRound(a.x + (ex - a.x) * u)), jsUint(jsRound(a.y + (ey - a.y) * u)), 5}) < .3
                               ? alpha * .8
                               : 0;
                });
                p.lineFn(
                    a.x, a.y, ex, ey, [&](double u) { return color(a.distance + (b.distance - a.distance) * f * u); },
                    alpha);
                const double h = heat(a.distance);
                if (h > .12) p.glow((a.x + ex) / 2, (a.y + ey) / 2, 4 + h * 4, hot[2], .22 * h * alpha);
            }
        };
        path(c.points);
        for (const auto& kid : c.kids) path(kid);
    }
}
/** @brief Draw a source standing shock crescent through synchronous material/painter borrows. */
inline void frontCrescent(Paint& p, double x, double direction, double h, double width,
                          const std::array<uint32_t, 3>& colors, double alpha = 1) {
    if (h < 1 || width < .3 || alpha <= 0) return;
    const double ry = h * .8, cy = -h * .2, rx = std::max(2., h * .55), x0 = direction > 0 ? x - rx - width - 1 : x - 1,
                 x1 = direction > 0 ? x + 1 : x + rx + width + 1;
    p.shade(x0, -h - 1, x1, 0, [&](double px, double py, int sx, int sy) {
        if (py > 0) return;
        const double ey = (py - cy) / ry;
        if (ey <= -1 || ey >= 1) return;
        const double e2 = 1 - ey * ey, edge = rx * std::sqrt(e2) - rx, wt = width * std::pow(e2, .45),
                     q = (edge - (px - x) * direction) / std::max(.5, wt);
        if (q < 0 || q > 1) return;
        p.put(sx, sy,
              q < .28   ? colors[0]
              : q < .62 ? colors[1]
                        : colors[2],
              alpha * (ey < -.8 ? .7 : 1) * (q > .7 ? 1.25 - q * .8 : 1));
    });
}
/** @brief Draw a source lumpy dust band with explicit half-plane and breakup parameters. */
inline void dustRing(Paint& p, double cx, double cy, double r, double width, double alpha, uint32_t color,
                     double squash = 1, double phase = 0, double breakup = 0, double lit = .35, double maxY = INFINITY,
                     int half = 0) {
    if (alpha <= .01 || r <= 0) return;
    const double W     = width * 1.6;
    const auto   light = mix(color, WHITE, lit);
    for (double y = std::floor(cy - (r + W) * squash) - 1;
         y <= std::min(maxY - 1, std::ceil(cy + (r + W) * squash) + 1); ++y)
        for (double x = std::floor(cx - r - W) - 1; x <= std::ceil(cx + r + W) + 1; ++x) {
            const double dx = x + .5 - cx, dy = (y + .5 - cy) / squash, d = jsHypot2(dx, dy), g = std::atan2(dy, dx);
            if ((half < 0 && dy >= 0) || (half > 0 && dy < 0)) continue;
            const double lumpy = .6 + .3 * std::sin(5 * g + phase) + .18 * std::sin(11 * g + phase * 2.1) +
                                 .12 * std::sin(17 * g + phase * .7),
                         wt = width * lumpy, off = std::abs(d - r - width * .25 * std::sin(7 * g + phase));
            if (off >= wt) continue;
            const double tear = breakup > 0 ? clamp((.55 + .45 * std::sin(6 * g + phase * 1.7) - breakup) / .25) : 1,
                         a    = alpha * (1 - std::pow(off / wt, 2)) * tear;
            if (a > .01) p.blend(x, y, dy < 0 && d > r - wt * .3 ? light : color, a);
        }
}
}  // namespace eve::stylize::pixelfx::impact
