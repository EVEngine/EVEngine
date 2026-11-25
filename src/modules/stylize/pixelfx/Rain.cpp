// eve-hot-path
// Native adaptation of pixelfx weather/rain.js (MIT), including Sprite Fusion rain.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include <optional>
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Weather.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double      D = 8, SLANT = -.2;
constexpr PixelFxInfo SOURCE{PixelFxKind::Rain, "weather/rain", 320, 180, 160, 170, D, true};
const double          UX = SLANT / jsHypot2(SLANT, 1), UY = 1 / jsHypot2(SLANT, 1);
struct Layer {
    int                n, m0, m1;
    double             len;
    std::array<int, 3> col;
    double             end0, end1, size0, size1;
    bool               ground, near;
};
constexpr std::array<Layer, 4> LAYERS{{{200, 10, 13, 2, {2, 2, 3}, -80, -6, 0, 0, false, false},
                                       {150, 14, 17, 4, {2, 3, 4}, -45, -2, 0, 0, false, false},
                                       {96, 19, 23, 7, {3, 4, 5}, 0, 1.4, .15, .4, true, false},
                                       {42, 25, 29, 13, {3, 5, 7}, 2, 6.5, .6, 1, true, true}}};
struct Pool {
    double x0, x1, y;
};
struct Rain final : AnalyticKernel {
    weather::Field      gustField, mistField;
    std::array<Pool, 5> pools{};
    explicit Rain(uint32_t S) : gustField(S + 21, weather::LOW_OCTAVES), mistField(S + 9, weather::OCTAVES) {
        for (uint32_t i = 0; i < 5; ++i) {
            const double x = -150 + double(i) * 66 + rand({S, i, 61}) * 30, w = 16 + rand({S, i, 63}) * 26;
            pools[i] = {x, x + w, 3 + std::floor(rand({S, i, 62}) * 3)};
        }
    }
    std::optional<Pool> poolAt(double x, double y) const {
        for (const auto& p : pools)
            if (x >= p.x0 + 2 && x <= p.x1 - 2 && std::abs(y - p.y) < 1.5) return p;
        return std::nullopt;
    }
    void rings(Paint& q, double x, const Pool& pool, double age, std::span<const uint32_t> C) const {
        const double X = jsRound(x), y = pool.y;
        if (age < .05) q.px(X, y, C[7]);
        for (int r = 0; r < 2; ++r) {
            const double a = age - r * .16, u = a / .5;
            if (u <= 0 || u >= 1) continue;
            const int  w = int(jsRound(1 + 7 * std::sqrt(u)));
            const auto c = u < .4 && !r ? C[6] : C[5];
            for (int side : {-1, 1})
                for (int d = 0; d < (u < .6 ? 2 : 1); ++d) {
                    const double px = X + side * (w - d);
                    if (px > pool.x0 && px < pool.x1) q.px(px, y, c);
                }
        }
    }
    void layer(Paint& q, double t, const PixelFxSettings& o, size_t L) const {
        const auto     Ly = LAYERS[L];
        const uint32_t sd = o.seed * 11 + uint32_t(L);
        const auto&    C  = o.palette;
        const auto     c0 = C[size_t(Ly.col[0])], c1 = C[size_t(Ly.col[1])], c2 = C[size_t(Ly.col[2])];
        const int      n     = int(jsRound(Ly.n * o.weather.density));
        const double   sweep = (256 * t) / D;
        weather::trips(t, D, n, sd, Ly.m0, Ly.m1, [&](double u, double k, int m, uint32_t i) {
            const auto j = jsUint(wrap(k, m));
            if (L < 2 &&
                rand({sd, i, j, 7}) >
                    .35 + 1.3 * (gustField.sample((-166 + rand({sd, i, j, 2}) * 332) * .8 + sweep, 9 + double(L) * 40) -
                                 .32))
                return;
            const double end = Ly.end0 + rand({sd, i, j, 1}) * (Ly.end1 - Ly.end0), y0 = -172 - Ly.len,
                         y = y0 + (end - y0) * u, x = -166 + rand({sd, i, j, 2}) * 332 - (end - y) * SLANT,
                         a = Ly.ground ? 1 : std::min(1., (1 - u) * 5);
            if (Ly.len <= 2) {
                q.dot(x, y, c2, a);
                q.dot(x - UX, y - UY, c1, a * .8);
            } else {
                q.lineFn(x - UX * Ly.len, y - UY * Ly.len, x - UX * Ly.len * .45, y - UY * Ly.len * .45, c0,
                         [&](double v) { return a * (.35 + .65 * v); });
                q.line(x - UX * Ly.len * .45, y - UY * Ly.len * .45, x - UX, y - UY, c1, a);
                q.dot(x, y, c2, a);
            }
            if (Ly.near) q.glow(x, y - 2, 4, C[5], .12);
            if (!Ly.ground) return;
            const double dur = D / m;
            for (int back = 1; back < 4; ++back) {
                const double age = u * dur + (back - 1) * dur;
                if (age > .7) break;
                const auto   jp = jsUint(wrap(k - back, m));
                const double z  = Ly.end0 + rand({sd, i, jp, 1}) * (Ly.end1 - Ly.end0),
                             xp = -166 + rand({sd, i, jp, 2}) * 332;
                if (rand({sd, i, jp, 5}) > (Ly.near ? 1 : .7)) continue;
                const double size = Ly.size0 + ((z - Ly.end0) / (Ly.end1 - Ly.end0)) * (Ly.size1 - Ly.size0);
                const auto   pool = Ly.near ? poolAt(xp, z) : std::nullopt;
                if (pool)
                    rings(q, xp, *pool, age, C);
                else if (weather::crown(q, xp, z, age, size, {C[6], C[4], WHITE}, rand({sd, i, jp, 4})) ==
                             weather::CrownState::Visible &&
                         Ly.near && age < .1)
                    q.glow(xp, z - 1, 5, C[5], .3 * (1 - age / .1));
            }
        });
    }
    void floor(Paint& q, double t, std::span<const uint32_t> C) const {
        weather::pass(q, SOURCE, 0, 9.5, [&](double x, double y, weather::Output& out) {
            if (y < 1) {
                out.c = C[4];
                out.l = C[5];
                out.k = .08;
                return;
            }
            if (y < 2) {
                out.c = C[1];
                return;
            }
            out.c     = C[0];
            out.a     = .55;
            out.blend = true;
            for (const auto& p : pools)
                if (x >= p.x0 && x < p.x1 && std::floor(y) == p.y) {
                    const bool edge = x < p.x0 + 2 || x >= p.x1 - 2;
                    out.c           = edge ? C[3] : C[4];
                    out.a           = 1;
                    out.blend       = false;
                    out.l           = C[5];
                    out.k           = .06;
                }
        });
        for (size_t i = 0; i < pools.size(); ++i) {
            const auto   p = pools[i];
            const double gx =
                jsRound(p.x0 + 3 + (p.x1 - p.x0 - 6) * (.5 + .5 * std::sin((PI * 2 * t) / D + double(i) * 1.9)));
            q.px(gx, p.y, C[6]);
            q.glow(gx, p.y, 4, C[5], .2);
        }
    }
    void mist(Paint& p, double t, std::span<const uint32_t> C) const {
        const double   drift = (256 * t) / D;
        const uint32_t crest = mix(C[4], C[5], .5);
        const auto     dens  = [&](double x, double y) {
            const double prof = smooth(-22, -4, y) * (1 - smooth(7, 10, y));
            return prof * (.62 + (mistField.sample(x * .45 + drift, y * 4 + 30) - .5) * 2);
        };
        weather::pass(p, SOURCE, -20, 9, [&](double x, double y, weather::Output& out) {
            const double d = dens(x, y);
            if (d < .28) return;
            const double th = weather::bayer(x, y) * .06;
            out.blend       = true;
            if (d > .46 + th && dens(x, y - 1) < .43) {
                out.c = crest;
                out.a = .5;
            } else {
                out.c = d > .58 + th ? C[4] : C[3];
                out.a = clamp((d - .28) * 1.6) * .5;
            }
            out.l = C[4];
            out.k = (d - .3) * .1;
        });
    }
    void drawBack(Paint& p, double t, const PixelFxSettings& o) const override {
        t                   = jsRound(t * 3e4) / 3e4;
        auto           q    = weather::clip(p, SOURCE);
        const auto&    C    = o.palette;
        const uint32_t haze = mix(C[0], C[1], .6);
        weather::pass(p, SOURCE, -170, -.5, [&](double, double y, weather::Output& out) {
            out.c     = haze;
            out.a     = .3 + .26 * smooth(-170, 0, y);
            out.blend = true;
        });
        floor(q, t, C);
        for (size_t L = 0; L < 3; ++L) layer(q, t, o, L);
        mist(p, t, C);
    }
    void draw(Paint& p, double t, const PixelFxSettings& o) const override {
        t      = jsRound(t * 3e4) / 3e4;
        auto q = weather::clip(p, SOURCE);
        layer(q, t, o, 3);
    }
};
}  // namespace
const Descriptor& rainDescriptor() {
    static const std::array<Option, 1> options{{{"density", 0, 4, false, 0, &PixelFxWeatherSettings::density}}};
    static const Descriptor            d{SOURCE,
                                         8,
                                         [](PixelFxSettings& s) {
                                  s.seed            = 4;
                                  s.weather.density = 1;
                                  s.palette         = {hex(0x0e1322), hex(0x1a2440), hex(0x283a5e), hex(0x3a5482),
                                                       hex(0x5674a8), hex(0x84a6d6), hex(0xc0dcf8), hex(0xf2f8ff)};
                                         },
                                         nullptr,
                                         options,
                                         nullptr,
                                         nullptr,
                                         [](const PixelFxSettings& s) -> std::unique_ptr<const AnalyticKernel> {
                                  return std::make_unique<Rain>(s.seed);
                                         }};
    return d;
}
}  // namespace eve::stylize::pixelfx
