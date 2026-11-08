// eve-hot-path
// Native adaptation of pixelfx ice/frost-breath.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Descriptor.h"
#include "stylize/pixelfx/PixelFxInternal.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double D = 1.2, SINK = 9;
void puff(Paint& p, std::span<const uint32_t> C, double x, double y, double r, double a, double k, bool front) {
    const auto body = front ? C[5] : C[4], cap = front ? C[7] : C[6];
    p.disc(x, y, r, body, [a](double d) { return a * .6 * (1 - d * d); });
    p.disc(x + r * .06, y + r * .12, r * .68, mix(body, C[3], .3), [a](double d) { return a * .5 * (1 - d * d); });
    if (k < .55)
        p.disc(x - r * .25, y - r * .3, r * .5, cap, [a, k](double d) { return a * .7 * (1 - k / .55) * (1 - d * d); });
    p.glow(x, y, r * 1.7, C[4], .1 * a);
}
void gust(Paint& p, double t, const PixelFxSettings& o) {
    const auto&  C  = o.palette;
    const double ph = t / D, sp = std::tan(o.ice.spread) * 1.08;
    p.shade(2, -46, 170, 46, [&](double x, double y, int sx, int sy) {
        const double u = x / 170, hw = 2.5 + x * sp, yc = 7 * u * u, v = (y - yc) / hw;
        if (v * v >= 1 || u >= 1) return;
        const double n    = tileFbm(x * .055 - ph * 10, (y - yc) * .15 + 7, 10, 0, o.seed, 2),
                     edge = (1 - v * v) * (u < .08 ? u / .08 : 1) * std::pow(1 - u, .8);
        const double d    = clamp(.5 + n * 1.3 + (1 - std::abs(v)) * .35 * std::pow(1 - u, 1.5)) * edge;
        if (d < .08) return;
        p.blendPx(sx, sy,
                  d > .72   ? C[7]
                  : d > .52 ? C[6]
                  : d > .32 ? C[5]
                            : C[4],
                  std::min(.78, d * .95) * clamp((d - .08) / .3));
        if (d > .3) p.add(sx, sy, d > .6 ? C[6] : C[5], .2 * d);
    });
}
void back(Paint& p, double t, const PixelFxSettings& o) {
    const auto&  C  = o.palette;
    const double sp = o.ice.spread;
    gust(p, t, o);
    cycle(t, D, 12, o.seed, 1, 1.25, [&](double age, double life, Rng& r, int) {
        const double th = r.range(-sp, sp), v = r.range(170, 230) * o.ice.power, y0 = r.range(-1, 1);
        const auto   q = fly(3, y0, v * std::cos(th), v * std::sin(th), age, SINK, 1.4);
        const double k = age / life, rad = 2 + 12 * std::pow(k, .75);
        puff(p, C, q.x, std::min(q.y, 40 - rad), rad, .75 * std::pow(1 - k, 1.2) * std::min(1.0, age / .08), k, false);
    });
}
void front(Paint& p, double t, const PixelFxSettings& o) {
    const auto&    C  = o.palette;
    const uint32_t S  = o.seed;
    const double   sp = o.ice.spread, ph = t / D;
    p.glowLine(4, 0, 120, 6, 24, C[4], .26);
    p.glowLine(2, 0, 50, 1, 9, C[6], .3);
    p.glow(60, 4, 44, C[3], .16);
    cycle(t, D, 10, S + 1, .7, .95, [&](double age, double life, Rng& r, int) {
        const double th = r.range(-sp * .8, sp * .8), v = r.range(190, 260) * o.ice.power, y0 = r.range(-1, 1);
        const auto   q = fly(3, y0, v * std::cos(th), v * std::sin(th), age, SINK, 1.6);
        const double k = age / life, rad = 1.5 + 8 * std::pow(k, .7);
        puff(p, C, q.x, std::min(q.y, 40 - rad), rad, .85 * std::pow(1 - k, 1.3) * std::min(1.0, age / .06), k, true);
    });
    cycle(t, D, 18, S + 2, .8, 1.1, [&](double age, double life, Rng& r, int) {
        const double th = r.range(-sp * 1.1, sp * 1.1), v = r.range(120, 190);
        const auto   q = fly(4, 0, v * std::cos(th), v * std::sin(th), age, SINK * 1.4, 1.5);
        const double k = age / life, sw = std::sin(age * 9 + r.range(0, TAU)) * 1.5;
        if (k < .85 || std::fmod(age * 30, 2) < 1) p.px(q.x, q.y + sw, r.isChance(.5) ? WHITE : C[6]);
    });
    cycle(t, D, 26, S + 3, .22, .36, [&](double age, double life, Rng& r, int) {
        const double th = r.range(-sp * .9, sp * .9), v = r.range(280, 380), y0 = r.range(-2, 2), vx = v * std::cos(th),
                     vy = v * std::sin(th);
        const auto   q = fly(6, y0, vx, vy, age, 0, .9), vel = flyV(vx, vy, age, 0, .9);
        const double speed = jsHypot2(vel.x, vel.y), L = 4 + speed * .03, ux = vel.x / speed, uy = vel.y / speed,
                     k = age / life;
        p.lineFn(
            q.x - ux * L, q.y - uy * L, q.x, q.y, [&](double u) { return u > .8   ? WHITE
                                                                         : u > .4 ? C[6]
                                                                                  : C[4]; },
            [k](double u) { return (u > .35 ? 1 : u / .35) * (k < .75 ? 1 : (1 - k) / .25); });
        p.glow(q.x, q.y, 3, C[5], .3 * (1 - k));
    });
    cycle(t, D, 9, S + 4, .65, .9, [&](double age, double life, Rng& r, int) {
        const double th = r.range(-sp, sp), v = r.range(150, 230);
        const auto   q = fly(5, 0, v * std::cos(th), v * std::sin(th), age, SINK, 1.1);
        const double k = age / life, a0 = r.range(0, TAU), va = r.range(9, 15);
        const int    side = r.sign();
        const double a = a0 + age * va * side, L = r.range(1.5, 2.6), ca = std::cos(a), sa = std::sin(a);
        if (k > .85 && std::fmod(age * 30, 2) < 1) return;
        p.line(q.x - ca * L, q.y - sa * L, q.x + ca * L, q.y + sa * L, C[5]);
        p.px(q.x + ca * L, q.y + sa * L, WHITE);
        const double gl = std::cos(a * 2);
        if (gl > .92) {
            for (const auto d : std::array<Point, 4>{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}})
                p.px(q.x + d.x, q.y + d.y, C[6]);
            p.px(q.x, q.y, WHITE);
            p.glow(q.x, q.y, 6, C[5], .5);
        } else
            p.glow(q.x, q.y, 3, C[5], .2);
    });
    for (uint32_t i = 0; i < 3; ++i) {
        const double L = 9 + 6 * loopNoise(ph, 1.2, S + i * 7), dy = (double(i) - 1) * 1.4;
        p.lineFn(
            2, dy * .5, 2 + L, dy, [&](double u) { return u < .5 ? WHITE : C[6]; },
            [](double u) { return u < .6 ? 1 : 1 - (u - .6) / .4; });
    }
    p.ellipse(2.5, 0, 4.6, 3, C[6]);
    p.ellipse(2, 0, 3, 1.8, WHITE);
    const double fl = .85 + .15 * std::sin(ph * TAU * 3);
    p.glow(2, 0, 24, C[5], .7 * fl);
    p.glow(2, 0, 9, WHITE, .5 * fl);
}
}  // namespace
const Descriptor& frostBreathDescriptor() {
    static const std::array<Option, 2> options{{{"spread", 0, 1, false, 0, &PixelFxIceSettings::spread},
                                                {"power", 0, 4, false, 0, &PixelFxIceSettings::power}}};
    static const Descriptor            d{{PixelFxKind::FrostBreath, "ice/frost-breath", 184, 104, 12, 50, D, true},
                                         8,
                                         [](PixelFxSettings& s) {
                                  s.seed       = 11;
                                  s.ice.spread = .21;
                                  s.ice.power  = 1;
                                  s.palette    = {hex(0x151a3d), hex(0x212f6b), hex(0x2c58aa), hex(0x3a8bd8),
                                                  hex(0x62c8f4), hex(0xa6ecff), hex(0xdcfbff), WHITE};
                                         },
                                         front,
                                         options,
                                         nullptr,
                                         back};
    return d;
}
}  // namespace eve::stylize::pixelfx
