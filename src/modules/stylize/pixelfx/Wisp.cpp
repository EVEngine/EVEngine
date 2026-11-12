// eve-hot-path
// Native adaptation of pixelfx critters/wisp.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Critter.h"
#include "stylize/pixelfx/CritterFire.h"
#include "stylize/pixelfx/Descriptor.h"
#include "stylize/pixelfx/PixelFxInternal.h"
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double D = 3.2;
struct Motion {
    double x, y, vx, vy;
};
Motion at(double t, double hover) {
    const double a = (t / D) * TAU;
    return {34 * std::sin(a), -hover - 10 * std::sin(2 * a), ((34 * TAU) / D) * std::cos(a),
            ((-20 * TAU) / D) * std::cos(2 * a)};
}
void draw(Paint& p, double time, const PixelFxSettings& o) {
    const double                  t = critter::shift(time, D, o.seed);
    const auto&                   C = o.palette;
    const auto                    S = at(t, o.critter.hover);
    const std::array<uint32_t, 9> heat{C[6], C[6], C[5], C[5], C[4], C[4], C[3], C[3], C[3]};
    const double lean = clamp(-S.vx / 80, -.9, .9), stretch = clamp(S.vy / 120, -.3, .4), X = S.x, Y = S.y;
    cycle(t, D, 30, o.seed * 7u + 1u, .3, .6, [&](double age, double life, Rng& r, int) {
        const auto   s = at(wrap(t - age, D), o.critter.hover);
        const double x = s.x + r.range(-3, 3), y = s.y - r.range(0, 4), vx = r.range(-8, 8), vy = -r.range(6, 16);
        const auto   q    = fly(x, y, vx, vy, age, -40, 2.5);
        const double size = r.range(1.2, 2.6), seed = r.next() * 100;
        critter::coolingFirePuff(p, q.x, q.y, 1 - age / life, size, seed, heat);
    });
    constexpr std::array<std::array<double, 4>, 3> tails{
        {{{-3.5, 13, 2.5, 3}}, {{3.5, 14, 2.5, 7}}, {{0, 20, 3.2, 11}}}};
    for (int li = 0; li < 4; ++li)
        for (const auto& f : tails) {
            const auto [dx, h, w, n] = f;
            const double hh          = (h + .25 * h * critter::loopNoise(t, D, 9, n, 5)) * (1 + stretch);
            critter::flameTongue(p, X + dx, Y - 2, hh, w, critter::loopNoise(t, D, 6, n, 40) * 1.5 + lean * hh * .9, t,
                                 D, n, heat, li);
        }
    constexpr double R = 7.5;
    p.shader(X - R - 2, Y - R - 2, X + R + 2, Y + R + 2, [&](double x, double y, ShadeOutput& out) {
        const double dx = x - X, dy = y - Y, ang = std::atan2(dy, dx),
                     rr = R + .7 * critter::loopNoise(t, D, 16, ang * 1.6 + 10, 3) + (dy > 0 ? -.6 : 0),
                     d  = jsHypot2(dx, dy) / rr;
        if (d > 1) return;
        out.c = d < .42 ? C[6] : d < .68 ? C[5] : d < .88 ? C[4] : C[3];
    });
    const double look  = jsRound(clamp(S.vx / 60, -1, 1));
    const bool   blink = wrap(t, D) > 2.2 && wrap(t, D) < 2.28;
    for (int side : {-1, 1}) {
        const double ex = jsRound(X) + side * 2 - (side > 0 ? 0 : 1) + look, ey = jsRound(Y) - 1;
        if (blink) {
            p.px(ex, ey + 1, C[1]);
            p.px(ex + 1, ey + 1, C[1]);
        } else {
            for (int k = 0; k < 3; ++k) {
                p.px(ex, ey + k, C[1]);
                p.px(ex + 1, ey + k, C[1]);
            }
            p.px(ex + (look > 0 ? 1 : 0), ey, C[6]);
        }
    }
    p.px(jsRound(X) + look, jsRound(Y) + 3, C[2]);
    p.px(jsRound(X) - 1 + look, jsRound(Y) + 3, C[2]);
    Particles ps;
    ps.time   = t;
    ps.sparks = {C[6], C[5], C[4], C[3]};
    cycle(t, D, 26, o.seed * 7u + 2u, .8, 1.6, [&](double age, double life, Rng& r, int) {
        const auto   s    = at(wrap(t - age, D), o.critter.hover);
        const double seed = r.next() * 100, x = s.x + r.range(-5, 5), y = s.y - r.range(4, 12), vx = r.range(-20, 20),
                     vy   = -r.range(20, 45);
        const auto   q    = fly(x, y, vx, vy, age, -15, 1.6);
        const double size = r.isChance(.3) ? 2 : 1;
        ps.list.push_back({ParticleKind::Ember, q.x + std::sin(age * 2.3 + seed) * 3 * std::min(1., age), q.y, 0, 0,
                           life - age, life, size, seed, 0, C[4], ParticleFlag::None});
    });
    ps.draw(p);
    const double fl = .85 + .15 * critter::loopNoise(t, D, 22, 0, 9);
    p.glow(X + lean * 3, Y - 6, 46 * fl, C[4], .38);
    p.glow(X, Y - 1, 13, C[5], .35 * fl);
}
void back(Paint& p, double time, const PixelFxSettings& o) {
    const double t    = critter::shift(time, D, o.seed);
    const auto&  C    = o.palette;
    const auto   s    = at(t, o.critter.hover);
    const double near = clamp(1 - (-s.y - 24) / 30);
    critter::floorShadow(p, s.x, 0, 7, 1.2, C[0], .4);
    for (int x = -24; x <= 24; x += 6)
        p.glow(s.x + x, -1, 16, C[4], (.08 + .1 * near) * (1 - std::abs(double(x)) / 32));
}
}  // namespace
const Descriptor& wispDescriptor() {
    static const std::array<Option, 1> options{{{"hover", 0, 256, false, 0, &PixelFxCritterSettings::hover}}};
    static const Descriptor            d{{PixelFxKind::Wisp, "critters/wisp", 128, 104, 64, 96, D, true},
                                         7,
                                         [](PixelFxSettings& s) {
                                  s.seed          = 1;
                                  s.critter.hover = 36;
                                  s.palette       = {hex(0x0a1f2e), hex(0x0f3d52), hex(0x14707e), hex(0x1fa8a8),
                                                     hex(0x4fe0c8), hex(0xa8fff0), WHITE};
                                         },
                                         draw,
                                         options,
                                         nullptr,
                                         back};
    return d;
}
}  // namespace eve::stylize::pixelfx
