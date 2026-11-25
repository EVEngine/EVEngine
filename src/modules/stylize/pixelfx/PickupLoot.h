// Native adaptation of pixelfx pickups/_lib.js coin and gemlet, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#pragma once
#include "stylize/pixelfx/Pickup.h"
namespace eve::stylize::pixelfx::pickup {
/** @brief Stamp a source tumbling coin using caller-owned scratch and synchronous target/material borrows. */
template <int W, int H>
void coin(Layer<W, H>& L, Paint& p, double x, double y, double r, double ax, double spin, std::span<const uint32_t> C,
          bool white = false, double alpha = 1, bool snap = true) {
    const double X = snap ? jsRound(x) : x, Y = snap ? jsRound(y) : y, cs = std::cos(spin), sn = std::sin(spin),
                 b = std::max(.5, r * std::abs(cs)), th = 1.2 * sn * (cs < 0 ? -1 : 1), ca = std::cos(ax),
                 sa = std::sin(ax), face = std::abs(cs), R = std::ceil(r) + 2;
    L.begin(std::floor(X) - R, std::floor(Y) - R);
    L.fill(std::floor(X) - R, std::floor(Y) - R, std::floor(X) + R, std::floor(Y) + R,
           [&](double px, double py) -> uint32_t {
               const double dx = px - X, dy = py - Y, u = dx * ca + dy * sa, w = -dx * sa + dy * ca,
                            ef = jsHypot2(u / r, w / b), er = jsHypot2(u / r, (w - th) / b);
               if (white && (ef <= 1 || er <= 1)) return WHITE;
               if (ef <= 1) {
                   const double lit = -(dx * .62 + dy * .78) / std::max(1., jsHypot2(dx, dy));
                   const int    dim = face < .45 ? 1 : 0;
                   if (b < 1.1) return lit > 0 ? C[6] : C[4];
                   if (ef > .72) return C[size_t((lit > .3 ? 6 : lit > -.35 ? 5 : 3) - dim)];
                   if (b > 2.2 && ef > .42) return C[size_t((lit > .2 ? 4 : 5) - dim)];
                   if (face > .6 && lit > .5) return C[7];
                   return C[size_t(5 - dim)];
               }
               if (er <= 1) return std::abs(w - th) < std::abs(w) ? C[2] : C[3];
               return 0;
           });
    L.outline(C[1]);
    L.draw(p, 0, 0, alpha);
}
/** @brief Stamp a source cut gem using caller-owned scratch and synchronous target/material borrows. */
template <int W, int H>
void gemlet(Layer<W, H>& L, Paint& p, double x, double y, double r, double angle, std::span<const uint32_t> G,
            bool white = false, double alpha = 1) {
    const double X = jsRound(x), Y = jsRound(y), ca = std::cos(angle), sa = std::sin(angle), R = std::ceil(r * 1.4) + 2,
                 ct = r * .75, tw = r * .5, pv = r * 1.3;
    L.begin(X - R, Y - R);
    L.fill(X - R, Y - R, X + R - 1, Y + R - 1, [&](double px, double py) -> uint32_t {
        const double dx = px - X, dy = py - Y, u = dx * ca + dy * sa, w = -dx * sa + dy * ca;
        int          facet;
        if (w < 0) {
            if (w < -ct || std::abs(u) > r - ((-w) / ct) * (r - tw)) return 0;
            facet = w < -ct * .45 && std::abs(u) < tw * .95 ? 0 : u < 0 ? 1 : 2;
        } else {
            if (w > pv || std::abs(u) > r * (1 - w / pv)) return 0;
            facet = std::abs(u) < .5 ? 4 : u < 0 ? 3 : 5;
        }
        if (white) return WHITE;
        constexpr std::array<Point, 6> normals{{{0, -1}, {-.7, -.7}, {.7, -.7}, {-.6, .8}, {0, 1}, {.6, .8}}};
        const auto                     N = normals[size_t(facet)];
        const double nx = N.x * ca - N.y * sa, ny = N.x * sa + N.y * ca, light = -(nx * .62 + ny * .78);
        if (facet == 0 && light > .3 && u < 0) return WHITE;
        return G[G.size() - size_t(light > .6 ? 1 : light > .2 ? 2 : light > -.2 ? 3 : light > -.6 ? 4 : 5)];
    });
    L.outline(G[0]);
    L.draw(p, 0, 0, alpha);
}
}  // namespace eve::stylize::pixelfx::pickup
