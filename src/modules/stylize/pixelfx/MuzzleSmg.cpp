// eve-hot-path
// Adapted from pixelfx projectiles/muzzle-smg.js (MIT).
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Descriptor.h"
#include "stylize/pixelfx/Projectile.h"
namespace eve::stylize::pixelfx {
namespace {
template <class F>
void shots(double t, double D, double life, F fn) {
    for (uint32_t s = 0; s < 12; ++s)
        for (double age = wrap(t - (double(s) * D) / 12 + 1e-7, D); age < life; age += D) fn(s, age);
}
void muzzleSmg(Paint& p, double t, const PixelFxSettings& o) {
    const auto&                   F  = o.palette;
    const auto&                   B  = o.projectile.brass;
    const auto&                   SM = o.projectile.smoke;
    const uint32_t                S  = o.seed;
    const double                  D = 1, MY = -o.projectile.height, top = -65;
    const std::array<uint32_t, 4> SP{{F[7], F[6], F[4], F[3]}};
    double                        pop = 0;
    shots(t, D, .2, [&](uint32_t, double age) { pop = std::max(pop, std::exp(-(age + .008) / .035)); });
    std::vector<projectile::Puff> puffs;
    shots(t, D, 2.2, [&](uint32_t s, double age) {
        for (uint32_t j = 0; j < 3; ++j) {
            Rng          r(hash({S, s, j, 3}));
            const double life = r.range(1.5, 2.2), delay = double(j) * .03;
            if (age < delay || age - delay >= life) continue;
            const double a = age - delay, k = a / life;
            const bool   port  = j == 2;
            const double vx    = port ? r.range(-12, 6) : r.range(45, 100) * (1 - double(j) * .4),
                         vy    = port ? -r.range(14, 26) : -r.range(4, 16);
            const double drift = r.range(3, 7), phase = r.range(0, TAU);
            const double x    = (port ? -7 : 3) + (vx * (1 - std::exp(-3.2 * a))) / 3.2 - drift * a * a +
                                std::sin(a * 2.1 + phase) * a * 2.5;
            const double lift = r.range(9, 16),
                         y    = (port ? MY - 2 : MY) + (vy * (1 - std::exp(-2.4 * a))) / 2.4 - .5 * lift * a * a;
            const double rad  = (port ? 1 : 1.2) + r.range(2, 4.5) * (1 - std::exp(-1.4 * a));
            if (y - rad < top + 1) continue;
            const double edge = std::min(1.0, (y - rad - top) / 12);
            puffs.push_back({x, y, rad, std::min(1.0, a * 10) * std::pow(1 - k, 1.6) * (port ? .24 : .34) * edge,
                             clamp(.92 - k * .6), pop * std::exp(-jsHypot2(x, y - MY) / 9)});
        }
    });
    const std::array<uint32_t, 7> warm{{SM[0], mix(F[1], SM[1], .5), mix(F[2], SM[2], .4), F[3], F[4], F[5], F[7]}};
    projectile::cloud(p, puffs, SM, warm, 1.4, .7, 1.3);
    shots(t, D, D, [&](uint32_t s, double age) {
        Rng          r(hash({S, s, 7}));
        const double vx = -r.range(18, 48), vy = -r.range(85, 135);
        const int    side = r.sign();
        const double spin = side * r.range(20, 32), angle = r.range(0, TAU);
        const auto   k = projectile::casing(-7, MY - 1, vx, vy, spin, age, 850, -.5, .42, angle);
        projectile::drawCasing(p, k, B, clamp((D - age) / .25));
    });
    shots(t, D, 2.0 / 30, [&](uint32_t s, double age) {
        Rng                      r(hash({S, s}));
        const int                f    = age < 1.0 / 30 ? 1 : 2;
        const double             tilt = r.range(-.06, .06);
        projectile::FlashOptions options;
        options.size                    = r.range(.95, 1.2);
        options.spike                   = {12, 21};
        options.diagonal                = {6, 11};
        options.cross                   = {4, 7.5};
        options.back                    = {2.5, 5};
        const auto                    L = projectile::flashShape(r, f, options);
        const std::array<uint32_t, 4> C =
            f == 1 ? std::array<uint32_t, 4>{WHITE, F[7], F[6], F[3]} : std::array<uint32_t, 4>{F[7], F[6], F[4], F[2]};
        projectile::flash(p, 0, MY, tilt, L, C, s * 7 + uint32_t(f));
    });
    shots(t, D, .3, [&](uint32_t s, double age) {
        for (uint32_t j = 0; j < 5; ++j) {
            Rng          r(hash({S, s, j, 11}));
            const double life = r.range(.05, .16) * (j > 2 ? 1.8 : 1);
            if (age >= life) continue;
            const double a = r.range(-.3, .3), sp = r.range(170, 380) * (j > 2 ? .5 : 1);
            const auto   pos = fly(2, MY, std::cos(a) * sp, std::sin(a) * sp, age, 420, 2.2),
                         v   = flyV(std::cos(a) * sp, std::sin(a) * sp, age, 420, 2.2);
            if (j > 2)
                p.px(pos.x, pos.y, age / life < .5 ? F[6] : F[4]);
            else
                projectile::spark(p, {pos.x, pos.y, v.x, v.y}, 1 - age / life, SP, .45, .011);
        }
    });
    shots(t, D, .15, [&](uint32_t, double age) {
        const double k = std::exp(-(age + .008) / .03);
        p.glow(4, MY, 26, F[4], .8 * k);
        p.glow(2, MY, 7, WHITE, .45 * k);
        p.glow(2, 0, 12, F[4], .16 * k);
    });
}
}  // namespace
const Descriptor& muzzleSmgDescriptor() {
    static const std::array<Option, 3> options{{{"height", 1, 60, false, 0, &PixelFxProjectileSettings::height},
                                                {"brass", 1, 64, false, 4, &PixelFxProjectileSettings::brass},
                                                {"smoke", 1, 64, false, 7, &PixelFxProjectileSettings::smoke}}};
    static const Descriptor            descriptor{
        {PixelFxKind::MuzzleSmg, "projectiles/muzzle-smg", 112, 72, 56, 66, 1, true},
        9,
        [](PixelFxSettings& s) {
            s.seed              = 6;
            s.projectile.height = 26;
            s.palette           = {hex(0x4a1712), hex(0x8a1f17), hex(0xc9281f), hex(0xf2561d), hex(0xff8a2a),
                                   hex(0xffc23d), hex(0xffe26b), hex(0xfff3a6), WHITE};
            s.projectile.brass  = {hex(0x4a2e10), hex(0x8c5c1c), hex(0xd9a441), hex(0xffe39a)};
            s.projectile.smoke  = {hex(0x26222e), hex(0x3b3644), hex(0x544e5e), hex(0x6f6979),
                                   hex(0x8c8596), hex(0xaca5b5), hex(0xcbc4d2)};
        },
        &muzzleSmg,
        options};
    return descriptor;
}
}  // namespace eve::stylize::pixelfx
