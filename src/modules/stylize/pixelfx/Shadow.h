// eve-hot-path
// Native adaptation of pixelfx shadow/_lib.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
// Sprite Fusion black-hole, Copyright (c) 2026 Hugo Duprez; see NOTICE.txt.
#pragma once
#include <limits>
#include "stylize/pixelfx/Painter.h"
namespace eve::stylize::pixelfx::shadow {
/** @brief One source pool ripple, owned by the emitting effect. */
struct PoolRipple {
    double x, t0, life, width;
};
/** @brief Paint the source slanted floor pool, wet lip, sheen and ordered ripple intersections.
 * @note Palette, ripple span and painter are synchronous caller-thread borrows; no state survives the draw.
 * @cost Visits the pool bounding rectangle times active ripple count per draw; no geometry allocation. */
inline void floorPool(Paint& p, std::span<const uint32_t> C, double rx, double t, std::span<const PoolRipple> ripples) {
    if (rx < 1) return;
    const double ry = rx * .17 + .6;
    p.shader(-rx - 3, -ry - 2, rx + 3, ry + 2, [&](double x, double y, ShadeOutput& out) {
        const double dx = x / rx, dy = (y - .5) / ry, d = std::sqrt(dx * dx + dy * dy);
        if (d > 1.18) return;
        if (d > 1) {
            if (dy < 0 && d < 1.09) {
                out.c = std::abs(dx) < .55 ? C[6] : std::abs(dx) < .85 ? C[5] : C[4];
                return;
            }
            if (dy >= 0 && d < 1.09) {
                out.c = C[2];
                return;
            }
            if (y > -.5 && noise(x * .7, 9) > .15) out.c = C[1];
            return;
        }
        for (const auto& r : ripples) {
            const double age = t - r.t0, rr = age * 46 * r.width + 1, ex = x - r.x, ey = (y - .5) / .17,
                         root = std::sqrt(ex * ex + ey * ey), e = root ? root : 1;
            const double g = std::sqrt(ex * ex + std::pow(ey / .17, 2)) / e;
            if (std::abs(e - rr) < .55 * g && age < r.life) {
                out.c = age < r.life * .5 ? C[4] : C[3];
                return;
            }
        }
        if (dy < -.55 && dy > -.85 && noise(x * .25 + t * 1.3, 4) > .2) {
            out.c = noise(x * .6 - t * 2, 6) > .55 ? C[5] : C[2];
            return;
        }
        if (dy < -.35 && d > .72) {
            out.c = d > .9 ? C[3] : C[2];
            return;
        }
        out.c = d > .86 ? C[1] : C[0];
    });
}
/** @brief Apply source inward/swirl acceleration in reverse list order, removing swallowed cells.
 * @note List owns its particles; weight is borrowed synchronously and must not mutate the list.
 * @cost Linear in live particles per simulation tick; no allocation except container erasure. */
template <class List, class Weight>
size_t attract(List& list, double x, double y, double gravity, double reach, double swirl, double r0, double dt,
               Weight weight, double vmax = std::numeric_limits<double>::infinity()) {
    size_t eaten = 0;
    for (size_t i = list.size(); i-- > 0;) {
        auto&        q  = list[i];
        const double wt = weight(q);
        if (!wt || q.delay > 0) continue;
        const double dx = x - q.x, dy = y - q.y, d2 = dx * dx + dy * dy;
        if (d2 > reach * reach) continue;
        const double root = std::sqrt(d2), d = root ? root : 1;
        if (d < r0) {
            list.erase(list.begin() + static_cast<typename List::difference_type>(i));
            ++eaten;
            continue;
        }
        const double a  = (gravity / (d2 + r0 * r0)) * std::min(1., (reach - d) / (reach * .25)) * wt * dt;
        const double ux = dx / d, uy = dy / d;
        q.vx += (ux - uy * swirl) * a;
        q.vy += (uy + ux * swirl) * a;
        const double drag = std::min(.2, (1 - d / reach) * 1.6 * dt);
        q.vx -= q.vx * drag;
        q.vy -= q.vy * drag;
        const double v = jsHypot2(q.vx, q.vy);
        if (v > vmax) {
            q.vx *= vmax / v;
            q.vy *= vmax / v;
        }
    }
    return eaten;
}
/** @brief Visit source annulus cell centers row-by-row; callback is a synchronous borrow.
 * @cost Visits the band area plus row margins per draw, jumping over the inner hole. */
template <class F>
void annulus(double x, double y, double r0, double r1, F fn) {
    const double r02 = r0 > 0 ? r0 * r0 : -1, r12 = r1 * r1;
    for (int py = int(std::floor(y - r1)); py <= int(std::ceil(y + r1)); ++py) {
        const double cy = py + .5, dy = cy - y, dy2 = dy * dy;
        if (dy2 >= r12) continue;
        const double xo = std::sqrt(r12 - dy2), xi = dy2 < r02 ? std::sqrt(r02 - dy2) : 0;
        for (int px = int(std::floor(x - xo)), end = int(std::ceil(x + xo)); px <= end; ++px) {
            const double cx = px + .5, dx = cx - x;
            if (xi > 0 && dx > -xi && dx < xi) {
                px = std::max(px, int(std::floor(x + xi)) - 1);
                continue;
            }
            const double d2 = dx * dx + dy2;
            if (d2 < r12 && d2 >= r02) fn(cx, cy, std::sqrt(d2));
        }
    }
}
/** @brief Paint the source thin ring by visiting only its annulus; callbacks are synchronous borrows.
 * @cost Linear in annulus cell area per draw, jumping over the inner hole. */
template <class C, class A = double>
void ringFast(Paint& p, double x, double y, double r, double w, C color, A alpha = 1) {
    if (r <= 0) return;
    const double r0 = std::max(0., r - w), width = r - r0;
    annulus(x, y, r0, r, [&](double cx, double cy, double d) {
        const double v     = (d - r0) / (width ? width : 1);
        const double angle = std::is_arithmetic_v<C> && std::is_arithmetic_v<A> ? 0 : std::atan2(cy - y, cx - x);
        p.dot(cx, cy, sample(color, v, angle), sample(alpha, v, angle));
    });
}
/** @brief Add source light hugging a circular rim; optional predicate is a synchronous borrow.
 * @cost Visits only the annulus cells per explicit draw, jumping over the inner hole. */
template <class Keep = std::nullptr_t>
void ringLight(Paint& p, double x, double y, double radius, double width, uint32_t color, double strength,
               Keep keep = nullptr) {
    if (width < 1 || strength <= .004) return;
    annulus(x, y, std::max(0., radius - width), radius + width, [&](double cx, double cy, double d) {
        if constexpr (!std::is_same_v<Keep, std::nullptr_t>)
            if (!keep(cx, cy)) return;
        const double f = 1 - std::abs(d - radius) / width;
        p.light(cx, cy, color, f * f * strength);
    });
}
/** @brief Add source halo light outside a black disc; painter is borrowed synchronously.
 * @cost Visits only annulus cells per explicit draw without retained geometry. */
inline void halo(Paint& p, double x, double y, double r0, double r1, uint32_t c, double k) {
    if (r1 <= r0 || k <= .004) return;
    const double w = r1 - r0;
    annulus(x, y, std::max(0., r0), r1, [&](double cx, double cy, double d) {
        const double f = 1 - (d - r0) / w;
        p.light(cx, cy, c, f * f * k);
    });
}
/** @brief Remove source light in a transformed local box after all other ink; predicate is a synchronous borrow.
 * @note Caller-thread, obeys destination clipping; no canvas or callback reference survives.
 * @cost Visits the clipped transformed box once per explicit draw. */
template <class Dark>
void unlight(Paint& p, double x0, double y0, double x1, double y1, Dark dark, double keep = 0) {
    p.shade(x0, y0, x1, y1, [&](double x, double y, int sx, int sy) {
        if (!dark(x, y) || !p.isInsideClip(sx, sy)) return;
        auto& d = p.lights[p.index(sx, sy)];
        if (!d) return;
        if (keep <= 0)
            d = 0;
        else
            d = 0xff000000u | ((jsUint(double((d >> 16) & 255u) * keep) & 255u) << 16) |
                ((jsUint(double((d >> 8) & 255u) * keep) & 255u) << 8) | (jsUint(double(d & 255u) * keep) & 255u);
    });
}
}  // namespace eve::stylize::pixelfx::shadow
