// Native adaptation of pixelfx ice/_lib.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#pragma once
#include <bit>
#include "stylize/pixelfx/Painter.h"
namespace eve::stylize::pixelfx::ice {
/** @brief Owning seeded six-arm geometry; no callbacks, actor or external state. */
struct FlakeDesign {
    std::array<Point, 3> branches{};
    int                  count = 0;
    bool                 plate = false, fork = false;
};
/** @brief Consume the caller's named source RNG synchronously; return a fixed-size owning design. */
inline FlakeDesign flakeDesign(Rng& R) {
    FlakeDesign d;
    d.count = R.integer(1, 3);
    for (int i = 0; i < d.count; ++i) {
        const double where    = .38 + (.4 * (i + R.range(.2, .8))) / d.count;
        const double length   = R.range(.24, .36) * (1 - i * .15);
        d.branches[size_t(i)] = {where, length};
    }
    d.plate = R.isChance(.45);
    d.fork  = R.isChance(.45);
    return d;
}
/** @brief Draw a quantized six-arm flake using borrowed ramp/design only during this call.
 * @cost At most six arms and two branch pairs per arm per draw; no allocation or cache.
 * @note Caller-thread affine; no callbacks or retained target references. */
inline void snowflake(Paint& p, double x, double y, double r, double rot, const FlakeDesign& d,
                      std::span<const uint32_t> C, double hot = 0, double alpha = 1) {
    const size_t n = C.size();
    rot            = jsRound(rot / (PI / 18)) * (PI / 18);
    if (r < 1.2) {
        p.dot(x, y, WHITE, alpha);
        return;
    }
    if (r < 2.6) {
        const bool                     pose = int(std::floor((std::fmod(rot, PI / 3) + PI / 3) / (PI / 6))) % 2 != 0;
        constexpr std::array<Point, 4> cross{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}},
            diag{{{1, 1}, {-1, -1}, {1, -1}, {-1, 1}}};
        for (const auto& q : pose ? cross : diag) p.dot(x + q.x, y + q.y, hot > .5 ? WHITE : C[n - 3], alpha);
        p.dot(x, y, WHITE, alpha);
        p.glow(x, y, 4, C[n - 3], .2 * alpha);
        return;
    }
    const uint32_t arm = hot > .5 ? WHITE : C[n - 2], br = hot > .5 ? C[n - 2] : C[n - 3];
    const bool     big   = r >= 7;
    const int      count = r < 4 ? 0 : big ? std::min(d.count, 2) : 1;
    for (int k = 0; k < 6; ++k) {
        const double g = rot + (k * TAU) / 6, ca = std::cos(g), sa = std::sin(g);
        p.line(x, y, x + ca * r, y + sa * r, arm, alpha);
        for (int j = 0; j < count; ++j) {
            const Point  b  = big ? d.branches[size_t(j)] : Point{.58, .36};
            const double bx = x + ca * r * b.x, by = y + sa * r * b.x, bl = std::max(1., r * b.y);
            for (int s : {-1, 1})
                p.line(bx, by, bx + std::cos(g + (s * PI) / 3) * bl, by + std::sin(g + (s * PI) / 3) * bl, br, alpha);
        }
        if (d.fork && big && !d.plate)
            for (int s : {-1, 1})
                p.dot(x + ca * r + std::cos(g + s * .9) * 1.2, y + sa * r + std::sin(g + s * .9) * 1.2, br, alpha);
        p.dot(x + ca * r, y + sa * r, WHITE, alpha);
    }
    if (d.plate && big) {
        const double q = r * .34;
        for (int k = 0; k < 6; ++k) {
            const double g0 = rot + PI / 6 + (k * TAU) / 6, g1 = g0 + TAU / 6;
            p.line(x + std::cos(g0) * q, y + std::sin(g0) * q, x + std::cos(g1) * q, y + std::sin(g1) * q, C[n - 3],
                   alpha);
        }
    }
    p.px(x, y, WHITE);
    p.glow(x, y, 3 + r * 1.2, C[n - 4], (.14 + .35 * hot) * alpha);
    if (hot > .3) p.glow(x, y, 2 + r * .5, WHITE, .3 * hot * alpha);
}
/** @brief Per-draw blade appearance; owning scalar values and no references. */
struct BladeOptions {
    double alpha = 1;
    bool   white = false;
    double glint = -1, jag = -1;
    int    dim = 0;
};
/** @brief Rasterize an authored faceted blade with synchronous target/ramp borrows.
 * @cost Work scales with the transformed blade bounding area per draw; no allocation or cache.
 * @note Caller-thread affine; the shading callback is synchronous and never retained. */
inline void blade(Paint& p, double bx, double by, double tx, double ty, double w, std::span<const uint32_t> C,
                  const BladeOptions& opts = {}) {
    const double dx = tx - bx, dy = ty - by, L = jsHypot2(dx, dy);
    if (L < 1.5 || w < .5) return;
    const double ux = dx / L, uy = dy / L, nx = -uy, ny = ux, hw = w / 2, pad = hw + 2;
    const auto   K = [&](int i) { return C[size_t(std::clamp(i - opts.dim, 0, int(C.size()) - 1))]; };
    p.shader(std::min(bx, tx) - pad, std::min(by, ty) - pad, std::max(bx, tx) + pad, std::max(by, ty),
             [&](double x, double y, ShadeOutput& o) {
                 const double rx = x - bx, ry = y - by, u = (rx * ux + ry * uy) / L, s = rx * nx + ry * ny;
                 if (u < 0) return;
                 const double v = std::min(u, 1.), l = hw * (1 - v) + .35,
                              r = hw * (1 - v) * (opts.jag >= 0 && v > opts.jag ? .55 : 1) + .35;
                 if (u > 1 || s < -l || s > r) {
                     const double over = u > 1 ? (u - 1) * L : 0, out = std::max(over, s < -l ? -l - s : s - r);
                     if (out < 1 && u < 1 + 1 / L) {
                         o.c = K(0);
                         o.a = opts.alpha;
                     }
                     return;
                 }
                 o.a = opts.alpha;
                 if (opts.white) {
                     o.c = WHITE;
                     return;
                 }
                 const double rd = -w * .12 * (1 - u);
                 uint32_t     c;
                 if (u > .88)
                     c = WHITE;
                 else if (std::abs(s - rd) < .55)
                     c = K(6);
                 else if (s < rd)
                     c = s + l < 1 ? K(6) : (s + l) / std::max(1., rd + l) > .55 ? K(5) : K(4);
                 else
                     c = r - s < 1.1 ? K(1) : (s - rd) / std::max(1., r - rd) > .5 ? K(2) : K(3);
                 const int32_t grit =
                     std::bit_cast<int32_t>(jsUint(std::floor(x) * 73856093) ^ jsUint(std::floor(y) * 19349663));
                 if (opts.jag >= 0 && std::abs(u - opts.jag) * L < .8 && s > rd)
                     c = K(6);
                 else if (s < rd && c != K(6) && grit % 997 < 30)
                     c = K(7);
                 if (opts.glint >= 0 && s <= rd + .6) {
                     const double g = std::abs(u - opts.glint) * L;
                     if (g < .8)
                         c = WHITE;
                     else if (g < 1.8)
                         c = K(6);
                 }
                 o.c = c;
             });
}
}  // namespace eve::stylize::pixelfx::ice
