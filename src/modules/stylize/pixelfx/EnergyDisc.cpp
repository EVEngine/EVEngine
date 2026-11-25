// eve-hot-path
// Adapted from pixelfx projectiles/energy-disc.js (MIT).
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Descriptor.h"
#include "stylize/pixelfx/PixelFxInternal.h"
#include "stylize/pixelfx/Projectile.h"
namespace eve::stylize::pixelfx {
namespace {
void energyDisc(Paint& p, double t, const PixelFxSettings& o) {
    const auto&    C = o.palette;
    const double   R = o.projectile.size, V = o.projectile.speed, D = .5;
    const int      N   = o.projectile.teeth;
    const uint32_t S   = o.seed;
    const double   rot = TAU * 3 * t / D, seg = TAU / N, left = -90;
    cycle(
        t, D, 8, S + 4, .12, .22,
        [&](double age, double life, Rng& r, int) {
            const double k    = age / life;
            const int    side = r.isChance(.5) ? -1 : 1;
            const double x0 = -2 - age * r.range(180, 260), len = r.range(8, 16) * (.5 + k),
                         y = side * (R + r.range(.5, 3));
            if (x0 - len < left) return;
            p.lineFn(x0, y, x0 - len, y,
                     k < .35  ? C[5]
                     : k < .7 ? C[4]
                              : C[3],
                     [](double u) { return u < .65 ? 1 : (1 - u) / .35; });
        },
        true);
    p.glowLine(-2, 0, -26, 0, 8, C[3], .35);
    const std::array<uint32_t, 4> SP{{WHITE, C[5], C[4], C[3]}};
    cycle(
        t, D, 20, S, .12, .3,
        [&](double age, double life, Rng& r, int) {
            const double unused = r.range(0, TAU), a = rot - unused * 0 + r.range(0, TAU), tip = R + 3.5;
            const double x0 = std::cos(a) * tip, y0 = std::sin(a) * tip, v = r.range(120, 230);
            const auto   m = projectile::wake(x0, y0, -std::sin(a) * v, std::cos(a) * v, age, V * .6, 2.4, 60);
            if (m.x > left + 8 && std::abs(m.y) < 23) projectile::spark(p, m, 1 - age / life, SP, .45, .014, 7);
        },
        true);
    p.shade(-R - 5, -R - 5, R + 5, R + 5, [&](double x, double y, int sx, int sy) {
        const double r = jsHypot2(x, y), th = std::atan2(y, x);
        if (r > R + 4.6) return;
        uint32_t c = 0;
        if (r <= R) {
            if (r < 1.8)
                c = WHITE;
            else if (r < 2.9)
                c = C[1];
            else if (r > R - 1.1)
                c = C[5];
            else {
                const double arm = std::sin(3 * (th - rot * .5) - r * .85);
                c                = arm > .65 ? C[5] : arm > .2 ? C[4] : arm > -.3 ? C[3] : C[2];
            }
        } else {
            const double ph = fract((th - rot) / seg), h = 4.2 * std::pow(ph, 1.4);
            if (r - R < h)
                c = ph > .86 ? WHITE : ph > .6 ? C[6] : ph > .35 ? C[5] : C[4];
            else if (r - R < 1.6)
                c = C[2];
        }
        if (c) p.put(sx, sy, c);
    });
    const uint32_t fr = jsUint(std::floor(t * 30 + 1e-6));
    for (uint32_t j = 0; j < 3; ++j) {
        Rng                  g(hash({fr, j, S}));
        const double         a0 = g.range(0, TAU);
        std::array<Point, 5> pts{};
        for (int i = 0; i <= 4; ++i) {
            const double rr = 2.5 + (R - 3) * i / 4, a = a0 + i * .25 + (i ? g.range(-.35, .35) : 0);
            pts[size_t(i)] = {std::cos(a) * rr, std::sin(a) * rr};
        }
        p.polyline(pts, j ? C[6] : WHITE);
    }
    p.ring(0, 0, R + .5, 1, C[6]);
    p.glow(0, 0, 30, C[3], .4);
    p.glow(0, 0, 6, C[5], .35);
    p.glow(std::cos(rot) * R, std::sin(rot) * R, 5, C[6], .4);
}
}  // namespace
const Descriptor& energyDiscDescriptor() {
    static const std::array<Option, 3> options{{{"size", 3, 16, false, 0, &PixelFxProjectileSettings::size},
                                                {"teeth", 2, 32, false, 0, &PixelFxProjectileSettings::teeth},
                                                {"speed", 1, 1000, false, 0, &PixelFxProjectileSettings::speed}}};
    static const Descriptor descriptor{{PixelFxKind::EnergyDisc, "projectiles/energy-disc", 120, 52, 92, 26, .5, true},
                                       7,
                                       [](PixelFxSettings& s) {
                                           s.seed    = 5;
                                           s.palette = {hex(0x0a0f2e), hex(0x172a78), hex(0x2550d0), hex(0x3f8cff),
                                                        hex(0x7cc8ff), hex(0xc8f0ff), WHITE};
                                           s.projectile.size  = 9;
                                           s.projectile.teeth = 8;
                                           s.projectile.speed = 220;
                                       },
                                       &energyDisc,
                                       options};
    return descriptor;
}
}  // namespace eve::stylize::pixelfx
