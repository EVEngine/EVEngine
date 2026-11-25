// eve-hot-path
// Adapted from pixelfx projectiles/bullet-tracer.js (MIT).
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Descriptor.h"
#include "stylize/pixelfx/PixelFxInternal.h"
#include "stylize/pixelfx/Projectile.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr std::array<std::string_view, 5> SLUG{{"..oyyp..", ".ypwwwwp", "ywwwwwww", ".ypwwwwp", "..oyyp.."}};
void                                      bulletTracer(Paint& p, double t, const PixelFxSettings& o) {
    const auto&                   F  = o.palette;
    const auto&                   SM = o.projectile.smoke;
    const uint32_t                S = o.seed, fr = jsUint(std::floor(t * 30 + 1e-6));
    const double                  D = .6, L = o.length, V = o.projectile.speed, ph = TAU * t / D, left = -182;
    std::vector<projectile::Puff> puffs;
    cycle(
        t, D, 18, S + 7, .5, .7,
        [&](double age, double life, Rng& r, int) {
            const double k = age / life, x = -L * r.range(.45, .8) - age * 90,
                         y = .5 + std::sin(age * 6 + r.range(0, TAU)) * age * 3 - age * age * 8;
            if (x < left + 6) return;
            puffs.push_back({x, y, 1.2 + age * 4, .55 * (1 - k) * std::min(1.0, age * 8) * smooth(left, left + 30, x),
                             .95 - k * .4, 0});
        },
        true);
    projectile::cloud(p, puffs, SM, SM, 1.2, .6);
    auto heat = [&](double d) {
        return 1 - std::pow(d / L, .8) + noise(d * .12 - t * 40, S) * .12 + .08 * std::sin(d * .35 - ph * 9);
    };
    auto         yAt = [&](double d) { return .5 + std::sin(d * .09 + ph * 2) * (d / L) * .8; };
    const double HOT = L * .32;
    p.shade(-HOT - 1, -4, 1, 5, [&](double x, double y, int sx, int sy) {
        const double d = -x, u = d / HOT;
        if (u < 0 || u > 1) return;
        const double w = .7 + 2.3 * std::pow(1 - u, 1.4) * (.9 + .1 * std::sin(d * .8 - ph * 12)),
                     v = std::abs(y - yAt(d)) / w;
        if (v > 1) return;
        const double h = heat(d);
        p.put(sx, sy,
              v < .38  ? (h > .8 ? WHITE : F[7])
              : v < .7 ? (h > .75  ? F[7]
                          : h > .6 ? F[6]
                                   : F[5])
              : u < .5 ? F[5]
                       : F[4]);
    });
    for (double d = HOT; d <= L; d += .5) {
        const double u = d / L, h = heat(d);
        if (u > .55 && rand({jsUint(std::floor(d / 3)), fr, S}) < (u - .55) * 1.6) continue;
        const uint32_t c = h > .55 ? F[6] : h > .42 ? F[5] : h > .3 ? F[4] : h > .18 ? F[3] : h > .08 ? F[2] : F[1];
        p.dot(-d, yAt(d), c, u < .85 ? 1 : (1 - u) / .15);
    }
    p.glowLine(-2, 1, -L * .45, 1, 6, F[4], .35);
    p.glowLine(-2, 1, -L * .2, 1, 3, F[7], .3);
    const std::array<uint32_t, 4> SP{{F[7], F[6], F[4], F[3]}};
    cycle(
        t, D, 16, S + 2, .1, .28,
        [&](double age, double life, Rng& r, int) {
            const double x = -r.range(4, L * .5), y = .5 + r.range(-.5, 1), vx = -r.range(10, 80),
                         vy = r.range(-60, 50);
            const auto   m  = projectile::wake(x, y, vx, vy, age, V * .35, 3, 260);
            if (m.x > left + 8) projectile::spark(p, m, 1 - age / life, SP, .45, .012, 6);
        },
        true);
    std::array<uint32_t, 256> pal{};
    pal['w'] = WHITE;
    pal['p'] = F[7];
    pal['y'] = F[6];
    pal['o'] = F[5];
    p.sprite(SLUG, pal, -1, -1);
    p.glow(2, 1, 24, F[4], .5);
    p.glow(3, 1, 8, WHITE, .55);
}
}  // namespace
const Descriptor& bulletTracerDescriptor() {
    static const std::array<Option, 3> options{{{"length", 8, 180, false, 0, &PixelFxSettings::length},
                                                {"speed", 1, 2000, false, 0, &PixelFxProjectileSettings::speed},
                                                {"smoke", 1, 64, false, 5, &PixelFxProjectileSettings::smoke}}};
    static const Descriptor            descriptor{
        {PixelFxKind::BulletTracer, "projectiles/bullet-tracer", 200, 40, 184, 18, .6, true},
        9,
        [](PixelFxSettings& s) {
            s.seed             = 4;
            s.length           = 120;
            s.projectile.speed = 520;
            s.palette          = {hex(0x4a1712), hex(0x8a1f17), hex(0xc9281f), hex(0xf2561d), hex(0xff8a2a),
                                  hex(0xffc23d), hex(0xffe26b), hex(0xfff3a6), WHITE};
            s.projectile.smoke = {hex(0x2e2a36), hex(0x46404f), hex(0x625b6b), hex(0x817a8a), hex(0xa39caa)};
        },
        &bulletTracer,
        options};
    return descriptor;
}
}  // namespace eve::stylize::pixelfx
