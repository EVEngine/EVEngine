// eve-hot-path
// Native adaptation of pixelfx nature/_lib.js spike helpers, MIT.
// Copyright (c) 2026 Patch; see LICENSE.txt and NOTICE.txt.
#pragma once
#include <limits>
#include "stylize/pixelfx/NatureGeometry.h"
namespace eve::stylize::pixelfx::nature {
/** @brief Owning edge notch sampled once with the effect RNG. */
struct SpikeNotch {
    double u;
    int    side;
    double depth;
};
/** @brief Owning source strata/notches, retaining no RNG or renderer references. */
struct SpikeLook {
    std::vector<SpikeNotch> notches;
    std::vector<double>     strata;
    double                  slope;
    uint32_t                seed;
};
/** @brief Prepare the source spike's fixed look with injected RNG.
 * @cost Bounded at five notches and four strata, once per spike lifetime. */
inline SpikeLook spikeLook(Rng& R) {
    SpikeLook s;
    const int n = R.integer(3, 5);
    for (int i = 0; i < n; ++i) s.notches.push_back({R.range(.1, .8), R.sign(), R.range(.8, 1.8)});
    const int m = R.integer(2, 4);
    for (int i = 0; i < m; ++i) s.strata.push_back(R.range(.12, .8));
    s.slope = R.range(-.25, .25);
    s.seed  = uint32_t(R.integer(1, 9999));
    return s;
}
/** @brief Prepare a source crack in normalized height/width coordinates.
 * @cost At most ten owning points, sampled once per prepared spike. */
inline std::vector<Point> spikeCrack(Rng& R, double u, double v, int direction) {
    std::vector<Point> points{{u, v}};
    for (int i = 0; i < 9; ++i) {
        u += R.range(.03, .08);
        const double a = direction * R.range(.05, .28), b = R.range(-.12, .12);
        v += a + b;
        if (u > .95 || std::abs(v) > 1) break;
        points.push_back({u, v});
    }
    return points;
}
/** @brief Prepare a source floor crack from injected RNG; side is -1 or +1.
 * @cost Linear in crack length, amortized over the effect lifetime. */
inline std::vector<Point> floorCrack(Rng& R, int side, double length) {
    EV_ASSERT((side == -1 || side == 1));
    std::vector<Point> points{{0, 0}};
    for (double x = 0; std::abs(x) < length;) {
        x += side * R.range(2, 5);
        points.push_back({x, double(R.integer(0, 2))});
    }
    return points;
}
/** @brief Synchronous borrowed draw options; cracks/ramp are never retained. */
struct SpikeDrawOptions {
    double alpha = 1, glint = -1, clip = 1, floor = std::numeric_limits<double>::infinity(), crack = 0, heat = 0;
    bool   white = false;
    int    half  = 0;
    std::span<const std::vector<Point>> cracks;
    std::span<const uint32_t>           glow;
};
/** @brief Draw source notched faceted spike, strata, clipping, half split and heated cracks.
 * @note Caller-thread borrows only; local float row scratch preserves source Float32 quantization.
 * @cost Per draw linear in the spike bounding rectangle and crack points, capped at400 rows. */
inline void drawSpike(Paint& p, double x, double base, double h, double w, double lean, const SpikeLook& sp,
                      std::span<const uint32_t> S, const SpikeDrawOptions& o = {}) {
    if (h < 1.5) return;
    EV_ASSERT((S.size() >= 8));
    const auto hw = [&](double u, int side) {
        double v = (w / 2) * std::pow(std::max(0., 1 - u), .85) + .3;
        for (const auto& n : sp.notches)
            if (n.side == side && u > n.u && u < n.u + .07) v -= n.depth * (1 - u * .5);
        return std::max(0., v);
    };
    const int              y0 = int(std::floor(base - h)) - 2, nr = std::min(400, int(std::ceil(base)) - y0 + 2);
    std::array<float, 400> left{}, right{}, height{};
    for (int k = 0; k < nr; ++k) {
        const double u    = (base - (y0 + k + .5)) / h;
        height[size_t(k)] = float(u);
        if (u < 0 || u > 1 || y0 + k + .5 > o.floor) {
            left[size_t(k)]  = std::numeric_limits<float>::infinity();
            right[size_t(k)] = -std::numeric_limits<float>::infinity();
        } else {
            left[size_t(k)]  = float(x + lean * u - hw(u, -1));
            right[size_t(k)] = float(x + lean * u + hw(u, 1));
        }
    }
    const auto inside = [&](int px, int py) {
        const int    k  = py - y0;
        const double fx = px + .5;
        if (k < 0 || k >= nr || fx < left[size_t(k)] || fx > right[size_t(k)]) return false;
        if (o.clip < 1 && height[size_t(k)] > o.clip + (rand({uint32_t(px), 77u}) - .5) * .07) return false;
        if (!o.half) return true;
        const double q = ((fx - left[size_t(k)]) / std::max(.5, double(right[size_t(k)]) - left[size_t(k)])) * 2 - 1;
        return o.half < 0 ? q < -.18 : q >= -.18;
    };
    const int x0 = int(std::floor(x - w / 2 - std::abs(lean) - 2)), x1 = int(std::ceil(x + w / 2 + std::abs(lean) + 2));
    for (int py = int(std::floor(base - h)) - 1; py < int(std::ceil(base)); ++py)
        for (int px = x0; px <= x1; ++px) {
            if (!inside(px, py)) {
                if (py + .5 <= o.floor && (inside(px - 1, py) || inside(px + 1, py) || inside(px, py + 1) ||
                                           (inside(px, py - 1) && py < base - 1)))
                    p.dot(px, py, S[0], o.alpha);
                continue;
            }
            if (o.white) {
                p.dot(px, py, WHITE, o.alpha);
                continue;
            }
            const size_t row = size_t(py - y0);
            const double u = height[row], c = x + lean * u, l = left[row], r = right[row], fx = px + .5;
            uint32_t     band    = 0;
            bool         stratum = false;
            for (const double s : sp.strata) {
                const double d = u * h - s * h - sp.slope * (fx - c);
                if (d > 0) ++band;
                if (std::abs(d) < .5) stratum = true;
            }
            const double jog = (rand({band, sp.seed}) - .5) * .3, q = ((fx - l) / std::max(.5, r - l)) * 2 - 1,
                         qr = -.18 + jog;
            int          i;
            if (u > .93)
                i = q < qr + .3 ? 7 : 5;
            else if (std::abs(q - qr) < std::max(.09, .55 / std::max(1., r - l)))
                i = 7;
            else if (q < qr)
                i = fx - l < 1 ? 7 : q < -.5 + jog ? 6 : 5;
            else
                i = r - fx < 1 ? 3 : q < .38 + jog ? 4 : 2;
            if (stratum && i < 7) --i;
            const double fleck = rand({uint32_t(px * 7 + 3), uint32_t(py * 13 + 5)});
            if (fleck < .035)
                i = q < qr ? 7 : i - 1;
            else if (fleck > .965)
                --i;
            if (o.glint >= 0 && q < qr + .1) {
                const double g = std::abs(u - o.glint);
                if (g < .035)
                    i = 8;
                else if (g < .07)
                    i = std::max(i, 7);
            }
            p.dot(px, py, i >= 8 ? WHITE : S[size_t(std::clamp(i, 0, int(S.size()) - 1))], o.alpha);
        }
    if (o.crack > 0)
        for (const auto& path : o.cracks) {
            const size_t m = size_t(std::max(1., std::floor(double(path.size()) * o.crack)));
            EV_ASSERT((m <= path.size()));
            for (size_t k = 1; k < m; ++k) {
                const auto a = path[k - 1], b = path[k];
                const auto at = [&](Point q) {
                    return Point{x + lean * q.x + q.y * hw(q.x, q.y < 0 ? -1 : 1), base - q.x * h};
                };
                const auto A = at(a), B = at(b);
                if (b.x > o.clip) break;
                if (!inside(int(std::floor(A.x)), int(std::floor(A.y))) ||
                    !inside(int(std::floor(B.x)), int(std::floor(B.y))))
                    continue;
                if (o.heat > .05 && !o.glow.empty()) {
                    EV_ASSERT((o.glow.size() >= 5));
                    p.line(A.x + 1, A.y, B.x + 1, B.y, S[0], o.alpha);
                    p.line(A.x, A.y, B.x, B.y,
                           o.glow[o.heat > .7   ? 4
                                  : o.heat > .4 ? 3
                                  : o.heat > .2 ? 2
                                                : 1],
                           o.alpha);
                    if (k % 2 == 0) p.glow(B.x, B.y, 7, o.glow[2], .35 * o.heat);
                } else {
                    p.line(A.x, A.y, B.x, B.y, S[0], o.alpha);
                    p.dot(B.x + 1, B.y - 1, S[6], o.alpha * .7);
                }
            }
        }
}
}  // namespace eve::stylize::pixelfx::nature
