// eve-hot-path
// Native adaptation of pixelfx critters/snake.js, MIT.
// Copyright (c) 2026 Patch; see LICENSE.txt.
#include "stylize/pixelfx/Critter.h"
#include "stylize/pixelfx/Descriptor.h"
#include "stylize/pixelfx/PixelFxInternal.h"
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double D = 2.4, HEAD = 30, LEN = 70, K = TAU / 30, W = (TAU * 2) / D;
double           lift(double u, double t) {
    const double wave = std::pow(std::max(0., std::sin(K * u - W * t)), .85),
                 env  = smooth(8, 18, u) * (1 - smooth(LEN - 14, LEN - 2, u));
    const double neck = u < 12 ? 6 * std::pow(1 - u / 12, 2) : 0;
    return 7 * wave * env + neck;
}
double radius(double u) { return u < 6 ? 3.3 : lerp(3.4, .6, std::pow((u - 6) / (LEN - 6), 1.3)); }
double bob(double t) { return .8 * std::sin(W * t); }
double tongue(double t) {
    for (double t0 : {.3, 1.5}) {
        const double k = (t - t0) / .5;
        if (k >= 0 && k < 1) return std::pow(std::max(0., std::sin(k * PI * 3)), .6);
    }
    return 0;
}
void snake(critter::Grid<88, 26>& g, std::span<const uint32_t> C, std::span<const uint32_t> B, double t) {
    const double hb = bob(t);
    for (double u = LEN; u >= 2; u -= .5) {
        const double x = HEAD - u, r0 = radius(u), cy = -r0 - lift(u, t) + (u < 12 ? hb * (1 - u / 12) : 0),
                     hump = lift(u, t), slope = lift(u + .5, t) - lift(u - .5, t),
                     r = r0 * std::sqrt(1 + slope * slope);
        for (double y = std::floor(cy - r); y <= std::ceil(cy + r); ++y) {
            const double v = (y + .5 - cy) / r;
            if (std::abs(v) > 1) continue;
            auto         c  = v < -.5 ? C[4] : v < .1 ? C[3] : v < .45 ? C[2] : v < .8 ? B[1] : B[0];
            const double dm = std::abs(std::fmod(u + 2, 7) - 3.5) / 3.5 + std::abs(v + .25) / .55;
            if (dm < .85 && v < .35 && u > 4) c = dm < .5 ? C[1] : C[2];
            if (v < -.62 && hump > 4.5 && dm >= .85) c = hump > 6 ? C[6] : C[5];
            if (v > .45 && v < .8 && std::fmod(std::floor(u), 3) == 0) c = B[2];
            g.set(x, y, c);
        }
    }
    const double hx = HEAD + 2, hy = -9.5 + hb;
    g.shade(hx - 5, hy - 4, hx + 6, hy + 4, [&](double x, double y) -> uint32_t {
        const double dx = (x - hx) / (x > hx ? 5.6 : 4.6), dy = (y - hy) / (3.6 - std::max(0., x - hx) * .18);
        if (dx * dx + dy * dy > 1) return 0;
        if (dy > .55) return B[1];
        return dy < -.3 ? (dx < .3 ? C[5] : C[4]) : C[3];
    });
    g.line(hx - 1, hy + 1.5, hx + 5, hy + 1.2, C[1]);
    g.set(hx + 1, hy - 2, C[0]);
    g.set(hx + 2, hy - 2, B[2]);
    g.set(hx + 1, hy - 1, B[2]);
    g.set(hx + 2, hy - 1, C[0]);
    g.set(hx + 1, hy - 3, C[6]);
    g.set(hx + 5, hy - 1, C[1]);
    g.outline(C[0]);
}
void draw(Paint& p, double time, const PixelFxSettings& o) {
    const double t = critter::shift(time, D, o.seed), hb = bob(t);
    const auto&  C = o.palette;
    const auto&  B = o.critter.belly;
    const double e = tongue(t), tx = HEAD + 7.5, ty = -8.2 + hb;
    if (e > .05) {
        const double L = 7 * e, wig = std::sin(t * 60) * .6 * e;
        p.line(tx, ty, tx + L, ty + wig, hex(0xe8304a));
        if (L > 3) {
            p.px(tx + L + 1, ty + wig - 1, hex(0xe8304a));
            p.px(tx + L + 1, ty + wig + 1, hex(0xe8304a));
        }
    }
    critter::Grid<88, 26> g(-44, -24);
    snake(g, C, B, t);
    g.draw(p);
    Particles ps;
    ps.time = t;
    cycle(t, D, 26, o.seed * 7u + 1u, .6, 1.1, [&](double age, double life, Rng& r, int) {
        const double born = wrap(t - age, D), m = r.integer(0, 2), u = (PI + W * born + m * TAU) / K;
        if (u < 18 || u > LEN - 10) return;
        const double x = HEAD - u + r.range(-2, 2) - r.range(6, 16) * age, y = -.5 - age * r.range(3, 8),
                     size = r.range(1.8, 3.4);
        ps.list.push_back(
            {ParticleKind::Smoke, x, y, 0, 0, life - age, life, size, 0, 0, hex(0x8a7f92), ParticleFlag::None});
    });
    ps.draw(p);
    p.glow(HEAD + 3.5, -11 + hb, 4, B[2], .35);
}
void back(Paint& p, double time, const PixelFxSettings& o) {
    const double t = critter::shift(time, D, o.seed);
    const auto&  C = o.palette;
    for (double u = 2; u <= LEN; u += 2) {
        const double h = lift(u, t), r = radius(u);
        critter::floorShadow(p, HEAD - u, -.2, 2.2 + r * .4, 1.2, C[0], .5 * (1 - clamp(h / 10)) + .1);
    }
    critter::floorShadow(p, HEAD + 2, -.2, 5, 1.2, C[0], .35);
}
}  // namespace
const Descriptor& snakeDescriptor() {
    static const std::array<Option, 1> options{{{"belly", 1, 64, false, 3, &PixelFxCritterSettings::belly}}};
    static const Descriptor            d{{PixelFxKind::Snake, "critters/snake", 136, 56, 68, 48, D, true},
                                         7,
                                         [](PixelFxSettings& s) {
                                  s.seed          = 1;
                                  s.palette       = {hex(0x081410), hex(0x123a1e), hex(0x1d5e2a), hex(0x2f8a36),
                                                     hex(0x5cb848), hex(0xa8e070), hex(0xe8ffc0)};
                                  s.critter.belly = {hex(0x4a3a14), hex(0xb08a34), hex(0xf0d878)};
                                         },
                                         draw,
                                         options,
                                         nullptr,
                                         back};
    return d;
}
}  // namespace eve::stylize::pixelfx
