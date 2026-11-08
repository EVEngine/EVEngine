// Native adaptation of pixelfx pickups/_lib.js brilliant/drawGem, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#pragma once
#include "common/Assert.h"
#include "stylize/pixelfx/Pickup.h"
namespace eve::stylize::pixelfx::pickup {
/** @brief Owning immutable convex-solid facet; outward normal, offset and source material kind. */
struct GemPlane {
    std::array<double, 3> n;
    double                d;
    int                   kind;
};
/** @brief Prepared owning convex solid; no input/painter references retained. */
struct GemSolid {
    std::vector<GemPlane> planes;
    double                r, top, bottom;
};
/** @brief Build the source brilliant-cut solid from trusted finite dimensions.
 * @cost Allocates linear in facet count once per cold preparation; retain across frames.
 * @note Caller-thread only; n must be1..32 with positive finite dimensions. */
[[nodiscard]] inline GemSolid brilliant(double r, double crown, double table, double pavilion, double girdle,
                                        int n = 8) {
    EV_ASSERT((n >= 1 && n <= 32));
    GemSolid     g{{{{0, 1, 0}, crown + girdle / 2, 0}}, r, crown + girdle / 2, pavilion + girdle / 2};
    const double cs = std::atan2(crown, r * (1 - table)), ps = std::atan2(pavilion, r);
    g.planes.reserve(size_t(1 + n * 6));
    for (int i = 0; i < n * 2; ++i) {
        const double a    = i * PI / n;
        const bool   half = i % 2 != 0;
        const double c = half ? std::min(1.45, cs + .22) : cs, pv = half ? ps - .1 : ps;
        g.planes.push_back({{std::sin(c) * std::cos(a), std::cos(c), std::sin(c) * std::sin(a)},
                            r * std::sin(c) * (half ? 1.012 : 1) + girdle / 2 * std::cos(c),
                            half ? 2 : 1});
        g.planes.push_back({{std::cos(a), 0, std::sin(a)}, r, 3});
        g.planes.push_back({{std::sin(pv) * std::cos(a), -std::cos(pv), std::sin(pv) * std::sin(a)},
                            r * std::sin(pv) * (half ? 1.03 : 1) + girdle / 2 * std::cos(pv),
                            half ? 5 : 4});
    }
    return g;
}
/** @brief Source gem material controls borrowed/copied only for one draw. */
struct GemOptions {
    double tilt  = .32;
    bool   white = false;
    double alpha = 1;
    bool   edges = true;
    double gain  = 1;
};
/** @brief Source brightest facet location and score; score-1 means no shaded hit. */
struct GemHighlight {
    double x, y, k;
};
/** @brief Draw an owning prepared convex solid with call-local facet hits and edge recoloring.
 * @cost Linear in sampled sprite cells times facets per draw; reuses prepared solid geometry.
 * @note Synchronous borrows only, no callbacks retained; source meshes fit within the scratch dimensions. */
template <int W, int H>
[[nodiscard]] GemHighlight drawGem(Layer<W, H>& L, Paint& p, const GemSolid& g, double x, double y, double spin,
                                   std::span<const uint32_t> C, const GemOptions& o = {}) {
    const double              cs = std::cos(spin), sn = std::sin(spin), ct = std::cos(o.tilt), st = std::sin(o.tilt);
    std::array<GemPlane, 193> planes{};
    EV_ASSERT(g.planes.size() <= planes.size());
    for (size_t i = 0; i < g.planes.size(); ++i) {
        const auto& q = g.planes[i];
        planes[i]     = {{{q.n[0] * cs - q.n[2] * sn, q.n[1], q.n[0] * sn + q.n[2] * cs}}, q.d, q.kind};
    }
    constexpr double               Lx = -.55, Ly = .72, Lz = .42;
    const std::array<double, 3>    viewDirection{0, -st, -ct}, V{0, st, ct};
    const double                   Hl = jsHypot3(Lx + V[0], Ly + V[1], Lz + V[2]);
    const std::array<double, 3>    hv{(Lx + V[0]) / Hl, (Ly + V[1]) / Hl, (Lz + V[2]) / Hl};
    const double                   X = jsRound(x), Y = jsRound(y), R = std::ceil(g.r) + 2, top = std::ceil(g.top + 2),
                                   bot = std::ceil(g.bottom + 2);
    GemHighlight                   hi{X, Y, -1};
    std::array<int, size_t(W) * H> ids{};
    ids.fill(-1);
    L.begin(X - R, Y - top);
    L.fill(X - R, Y - top, X + R, Y + bot, [&](double px, double py) -> uint32_t {
        const double                sx = px - X, sy = -(py - Y);
        const std::array<double, 3> origin{sx, sy * ct, -sy * st};
        double                      t0 = -1e9, t1 = 1e9;
        int                         hit = -1;
        for (size_t i = 0; i < g.planes.size(); ++i) {
            const auto&  q  = planes[i];
            const double nd = q.n[0] * viewDirection[0] + q.n[1] * viewDirection[1] + q.n[2] * viewDirection[2],
                         no = q.n[0] * origin[0] + q.n[1] * origin[1] + q.n[2] * origin[2];
            if (std::abs(nd) < 1e-6) {
                if (no > q.d) return 0;
                continue;
            }
            const double tt = (q.d - no) / nd;
            if (nd < 0) {
                if (tt > t0) {
                    t0  = tt;
                    hit = int(i);
                }
            } else if (tt < t1)
                t1 = tt;
        }
        if (hit < 0 || t0 > t1) return 0;
        if (o.white) return WHITE;
        const int ix = int(std::floor(px)) + L.info.originX, iy = int(std::floor(py)) + L.info.originY;
        EV_ASSERT((ix >= 0 && iy >= 0 && ix < W && iy < H));
        ids[size_t(iy * W + ix)]          = hit;
        const auto&                 plane = planes[size_t(hit)];
        const auto&                 n     = plane.n;
        const bool                  pav   = plane.kind == 4 || plane.kind == 5 || plane.kind == 8;
        const std::array<double, 3> m     = pav ? std::array<double, 3>{n[0], -n[1] * .6, n[2]} : n;
        const double                ml    = jsHypot3(m[0], m[1], m[2]);
        const double lam                  = std::max(0.0, (m[0] * Lx + m[1] * Ly + m[2] * Lz) / ml),
                     spec = std::pow(std::max(0.0, (m[0] * hv[0] + m[1] * hv[1] + m[2] * hv[2]) / ml), 18);
        double       b    = pav               ? .02 + .72 * std::pow(lam, 2.4)
                            : plane.kind == 3 ? .12 + .4 * lam
                                              : .04 + .66 * std::pow(lam, 1.8);
        if (plane.kind == 6)
            b = .16 + .66 * std::pow(std::max(0.0, (n[0] * Lx + n[2] * Lz) / jsHypot2(Lx, Lz)), 1.3);
        else if (plane.kind == 7)
            b = .22 + .62 * std::pow(lam, 1.2);
        else if (plane.kind == 9)
            b = .62 + .25 * lam;
        else if (plane.kind == 10)
            b = .3 + .5 * lam + (rand({jsUint(std::floor(px - X)), jsUint(std::floor(py - Y)), 43}) - .5) * .08;
        if (plane.kind != 9) b *= o.gain;
        if (plane.kind == 0) b -= .04;
        const double brightness = b + spec * .6;
        if (brightness > hi.k) hi = {px, py, brightness};
        if (spec > .8) return pav ? C[6] : WHITE;
        return C[size_t(clamp(jsRound(1 + brightness * 5.6), 1, 6))];
    });
    if (!o.white && o.edges) {
        std::array<uint32_t, size_t(W) * H> changes{};
        for (int iy = 0; iy < H; ++iy)
            for (int ix = 0; ix < W; ++ix) {
                const size_t k  = size_t(iy * W + ix);
                const int    id = ids[k];
                if (id < 0) continue;
                const int right = ix + 1 < W ? ids[k + 1] : -1, down = iy + 1 < H ? ids[k + W] : -1;
                if ((right >= 0 && right != id) || (down >= 0 && down != id)) {
                    const auto color = L.colors[k];
                    const auto found = std::find(C.begin(), C.end(), color);
                    const int  index = found == C.end() ? -1 : int(found - C.begin());
                    const int  kind  = planes[size_t(id)].kind;
                    const bool pav   = kind == 4 || kind == 5 || kind == 8;
                    if (index > 0) changes[k] = C[size_t(pav ? std::max(1, index - 1) : std::min(7, index + 1))];
                }
            }
        for (size_t k = 0; k < changes.size(); ++k)
            if (changes[k]) L.colors[k] = changes[k];
    }
    L.outline(C[0]);
    L.draw(p, 0, 0, o.alpha);
    return hi;
}
}  // namespace eve::stylize::pixelfx::pickup
