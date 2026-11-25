// eve-hot-path
// Native adaptation of pixelfx weather/cherry-blossoms.js (MIT), following Sprite Fusion falling leaves.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Weather.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double      D = 10, FLOOR = 1;
constexpr PixelFxInfo SOURCE{PixelFxKind::CherryBlossoms, "weather/cherry-blossoms", 320, 180, 160, 170, D, true};
double                gust(double t) { return 22 * std::sin((TAU * t) / D + .4) + 7 * std::sin((TAU * 2 * t) / D - 1); }
template <class P>
void petal(P& q, double x, double y, double len, double ang, double flip, std::span<const uint32_t> C, double alpha) {
    const double                  fs = .3 + .7 * std::abs(flip), c = std::cos(ang), s = std::sin(ang), e = len + 1;
    const bool                    back = flip < -.2;
    const std::array<uint32_t, 3> face =
        back ? std::array<uint32_t, 3>{{C[1], C[2], C[3]}} : std::array<uint32_t, 3>{{C[2], C[3], C[4]}};
    const auto tip = back ? C[3] : C[5];
    q.shader(x - e, y - e, x + e, y + e, [&](double px, double py, ShadeOutput& out) {
        const double dx = px - x, dy = py - y, u = (dx * c + dy * s) / len, v = (-dx * s + dy * c) / (len * fs);
        if (u < -1 || u > 1) return;
        const double hw = .55 * std::sqrt(1 - u * u) * (1 + .35 * u);
        if (std::abs(v) > hw || (u > .7 && std::abs(v) < .12)) return;
        out.c = u > .45 && v < .1 ? tip : u < -.45 ? face[0] : v < 0 ? face[2] : face[1];
        out.a = alpha;
    });
}
struct CherryBlossoms final : AnalyticKernel {
    weather::RecordedRaster scatter;
    explicit CherryBlossoms(const PixelFxSettings& o) {
        for (uint32_t i = 0; i < 46; ++i)
            petal(scatter, -164 + rand({o.seed, i, 71}) * 328, FLOOR + std::floor(rand({o.seed, i, 72}) * 3) * 2,
                  2.6 + rand({o.seed, i, 73}), (rand({o.seed, i, 74}) - .5) * 1.2, .5, o.palette, 1);
    }
    void petals(Paint& q, double t, const PixelFxSettings& o, bool near) const {
        const uint32_t sd = o.seed * 7 + (near ? 1 : 0);
        const auto&    C  = o.palette;
        weather::trips(t, D, near ? 54 : 90, sd, 1, 2, [&](double u, double k, int m, uint32_t i) {
            const auto   j   = jsUint(wrap(k, m));
            const auto   R   = [&](uint32_t c) { return rand({sd, i, j, c}); };
            const double dur = D / m, tf = dur * .84, a = std::min(u * dur, tf), rest = u * dur - tf,
                         born = t - u * dur, x0 = -200 + R(1) * 330, y0 = -184, vy = (FLOOR + (near ? 2 : 0) - y0) / tf,
                         vx = 9 + R(2) * 10, rho = (near ? 3 : 2) + R(3) * 3, w = 2.2 + R(4) * 2.8, ph = R(5) * TAU,
                         gk = near ? 1 : .6;
            const auto   at = [&](double aa) -> Point {
                return {x0 + vx * aa + (gust(born + aa) - gust(born)) * gk + rho * std::cos(w * aa + ph),
                        y0 + vy * aa + rho * .6 * std::sin(w * aa + ph)};
            };
            const double len = (near ? 3.6 : 2.6) + R(6) * 1.1;
            if (rest > 0) {
                const auto   a2   = at(tf);
                const double fade = 1 - rest / (dur - tf);
                if (fade > .15) petal(q, a2.x, a2.y, len * (.35 + .65 * fade), (R(7) - .5) * 1.2, .5, C, 1);
                return;
            }
            const auto   a2 = at(a);
            const double x = a2.x, y = a2.y, flip = std::cos(R(8) * TAU + a * (3 + R(9) * 5)),
                         ang = R(10) * TAU + a * (R(11) - .5) * 3;
            petal(q, x, y, len, ang, flip, C, 1);
            if (flip > .9) {
                const double g = (flip - .9) / .1;
                q.px(x, y - 1, WHITE);
                q.glow(x, y, near ? 7 : 5, C[4], (near ? .32 : .22) * g);
            }
        });
    }
    void blossoms(Paint& q, double t, const PixelFxSettings& o) const {
        const uint32_t sd = o.seed * 31;
        weather::trips(t, D, 7, sd, 1, 1, [&](double u, double, int, uint32_t i) {
            const auto   R = [&](uint32_t c) { return rand({sd, i, c}); };
            const double y = -190 + (FLOOR + 2 + 190) * std::min(1., u / .9), born = t - u * D;
            if (u > .9) return;
            const double x   = -190 + R(1) * 330 + 14 * u * D * .1 + (gust(t) - gust(born)) +
                               6 * std::sin(u * TAU * 2 + R(2) * 6),
                         rot = R(3) * TAU + u * D * (R(4) - .5) * 2, r = 3.6 + R(5) * 1.2;
            for (int p = 0; p < 5; ++p) {
                const double an = rot + (p * TAU) / 5;
                petal(q, x + std::cos(an) * r * .75, y + std::sin(an) * r * .75, r * .7, an, 1, o.palette, 1);
            }
            q.px(x, y, hex(0xffe08a));
            q.glow(x, y, 8, o.palette[4], .2);
        });
    }
    void drawBack(Paint& p, double t, const PixelFxSettings& o) const override {
        auto       q   = weather::clip(p, SOURCE);
        const auto top = hex(0x0e0a14), low = mix(hex(0x1a0e1a), o.palette[0], .4);
        weather::rows(p, SOURCE, -170, 9, [&](double y, std::span<weather::RowPixel> row) {
            const double k = (y + 170) / 180;
            const auto   c = mix(top, low, k * k);
            for (auto& v : row) {
                v.c     = c;
                v.a     = float(.3 + .12 * k);
                v.blend = true;
            }
        });
        scatter.draw(q);
        petals(q, t, o, false);
    }
    void draw(Paint& p, double t, const PixelFxSettings& o) const override {
        auto q = weather::clip(p, SOURCE);
        petals(q, t, o, true);
        blossoms(q, t, o);
    }
};
}  // namespace
const Descriptor& cherryBlossomsDescriptor() {
    static const Descriptor d{SOURCE,
                              6,
                              [](PixelFxSettings& s) {
                                  s.seed    = 12;
                                  s.palette = {hex(0x4a1832), hex(0x8e3460), hex(0xd26a94),
                                               hex(0xf2a2c0), hex(0xffd2e2), hex(0xfff4f8)};
                              },
                              nullptr,
                              {},
                              nullptr,
                              nullptr,
                              [](const PixelFxSettings& s) -> std::unique_ptr<const AnalyticKernel> {
                                  return std::make_unique<CherryBlossoms>(s);
                              }};
    return d;
}
}  // namespace eve::stylize::pixelfx
