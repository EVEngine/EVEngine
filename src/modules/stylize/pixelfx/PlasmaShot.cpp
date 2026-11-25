// eve-hot-path
// Adapted from pixelfx projectiles/plasma-shot.js (MIT).
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Descriptor.h"
#include "stylize/pixelfx/PixelFxInternal.h"
#include "stylize/pixelfx/Projectile.h"
namespace eve::stylize::pixelfx {
namespace {
struct Ball {
    double x, y, r, heat, core, lifeFraction = 0;
};
void plasmaShot(Paint& p, double t, const PixelFxSettings& o) {
    const auto&    C = o.palette;
    const double   R = o.projectile.size, V = o.projectile.speed, D = .8, w = TAU * t / D;
    const uint32_t S = o.seed, fr = jsUint(std::floor(t * 30 + 1e-6));
    const double   left = -106;
    const double   T0   = -R * 3.2;
    for (double x = T0; x > T0 - 44; x -= 1) {
        const double u = (T0 - x) / 44, y = std::sin(x * .2 + w * 2) * (.3 + ((T0 - x) / 44) * 1.8);
        p.dot(x, y, u < .2 ? C[6] : u < .45 ? C[5] : C[4], u < .55 ? 1 : (1 - u) / .45);
        if (u < .3) p.px(x, y + 1, u < .15 ? C[5] : C[4]);
    }
    std::vector<Ball>           balls{{0, 0, R, 1, .8}};
    const std::array<double, 3> speeds{{2, -3, 5}};
    for (int i = 0; i < 3; ++i) {
        const double a = w * speeds[size_t(i)] + (i * TAU) / 3, d = R * (.5 + .1 * std::sin(w * 3 + i * 2));
        balls.push_back({std::cos(a) * d - .4, std::sin(a) * d * .8, R * .32, 1, 1});
    }
    for (int i = 1; i <= 6; ++i) {
        const double x = -R * (.5 + i * .42), wag = std::sin(w * 2 - i * .9) * (.3 + i * .28), r = R * (.62 - i * .08);
        balls.push_back({x, wag, r, .96 - i * .05, std::sqrt((5.2 - i * .65) * .3) / r});
    }
    std::vector<Ball> drops;
    cycle(t, D, 11, S, .34, .5, [&](double age, double life, Rng& r, int) {
        const double k = age / life, pull = r.range(450, 800),
                     x   = -R * 2.9 - .5 * pull * age * age - r.range(0, 16) * age;
        const double y   = std::sin(w * 2 - 5.4 + age * 12) * 2 + r.range(-30, 30) * age * (.4 + age),
                     rad = R * r.range(.3, .48) * (1 - k * .6);
        drops.push_back({x, y, rad, .97 * (1 - k * .75), std::sqrt(std::max(0.0, 1 - k * 1.3) * 7 * .3) / rad, k});
    });
    balls.insert(balls.end(), drops.begin(), drops.end());
    p.shade(-R * 2.6 - 60, -R * 2 - 8, R * 1.6, R * 2 + 8, [&](double x, double y, int sx, int sy) {
        double field = 0, heat = 0, hh = 0;
        for (const auto& b : balls) {
            const double dx = x - b.x, dy = y - b.y, d2 = dx * dx + dy * dy, q = d2 / (4 * b.r * b.r);
            if (q < 1) {
                const double c = ((1 - q) * (1 - q)) / .5625;
                field += c;
                heat += c * b.heat;
            }
            if (b.core != 0) hh += (b.core * b.core * b.r * b.r) / (d2 + .3);
        }
        if (field < 1) return;
        const int lvl = hh > 10       ? 7
                        : hh > 4.6    ? 6
                        : hh > 2.2    ? 5
                        : field > 1.4 ? 4
                                      : 3,
                  cap = int(jsRound(2 + (heat / field) * 5.4));
        p.put(sx, sy, C[size_t(std::max(2, std::min(x > R * .3 && lvl < 5 ? lvl + 1 : lvl, cap)))]);
    });
    for (uint32_t j = 0; j < 3; ++j) {
        Rng                  g(hash({fr, j, S}));
        const double         a0 = g.range(-2.3, 2.3), span0 = g.range(.45, .9);
        const int            side = g.sign();
        const double         span = span0 * side, hgt = g.range(1.5, 3.5);
        std::array<Point, 7> pts{};
        for (int i = 0; i <= 6; ++i) {
            const double a  = a0 + (span * i) / 6,
                         rr = R + .6 + hgt * std::sin((PI * i) / 6) + (i % 6 ? g.range(-.5, .5) : 0);
            pts[size_t(i)]  = {std::cos(a) * rr - .4, std::sin(a) * rr * .92};
        }
        p.polyline(pts, j ? C[5] : C[6]);
        p.px(pts[3].x, pts[3].y, WHITE);
        p.glow(pts[3].x, pts[3].y, 6, C[5], .4);
    }
    for (uint32_t j = 0; j < 4; ++j) {
        Rng          g(hash({fr, j + 9, S}));
        const double a = g.range(-2.2, 2.2), l = g.range(1.5, 4), r0 = R + 1, x0 = std::cos(a) * r0 - .4,
                     y0 = std::sin(a) * r0 * .92;
        p.line(x0, y0, x0 + std::cos(a) * l, y0 + std::sin(a) * l, j % 2 ? C[4] : C[5]);
    }
    const std::array<uint32_t, 4> SP{{WHITE, C[6], C[5], C[3]}};
    cycle(
        t, D, 14, S + 5, .12, .3,
        [&](double age, double life, Rng& r, int) {
            const double a = r.range(-2.4, 2.4), sp = r.range(60, 130);
            const auto   m = projectile::wake(std::cos(a) * (R + 1), std::sin(a) * (R + 1), std::cos(a) * sp,
                                              std::sin(a) * sp, age, V * .7, 4, 0);
            if (m.x > left + 4 && std::abs(m.y) < 26) projectile::spark(p, m, 1 - age / life, SP, .4, .015, 6);
        },
        true);
    const double pulse = .85 + .15 * std::sin(w * 4);
    p.glow(-2, 0, 34, C[3], .8 * pulse);
    p.glow(0, 0, 16, C[4], .35 * pulse);
    p.glow(1, 0, 7, C[6], .3 * pulse);
    p.glowLine(-R, 0, -R * 4.6, 0, 7, C[3], .3);
    for (const auto& d : drops) {
        if (d.x < left + 4) continue;
        if (d.lifeFraction < .6 && d.x < -R * 3.5) p.px(d.x, d.y, d.lifeFraction < .3 ? C[7] : C[6]);
        p.glow(d.x, d.y, 3 + d.r * 2, C[2], .6 * (1 - d.lifeFraction));
    }
}
}  // namespace
const Descriptor& plasmaShotDescriptor() {
    static const std::array<Option, 2> options{{{"size", 2, 12, false, 0, &PixelFxProjectileSettings::size},
                                                {"speed", 1, 1000, false, 0, &PixelFxProjectileSettings::speed}}};
    static const Descriptor descriptor{{PixelFxKind::PlasmaShot, "projectiles/plasma-shot", 144, 60, 108, 30, .8, true},
                                       8,
                                       [](PixelFxSettings& s) {
                                           s.seed             = 3;
                                           s.projectile.size  = 6;
                                           s.projectile.speed = 200;
                                           s.palette = {hex(0x061a1c), hex(0x0b3b34), hex(0x0f6a4a), hex(0x18a05a),
                                                        hex(0x3fd46a), hex(0x8cf57e), hex(0xd6ffc4), WHITE};
                                       },
                                       &plasmaShot,
                                       options};
    return descriptor;
}
}  // namespace eve::stylize::pixelfx
