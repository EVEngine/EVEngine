// eve-hot-path
// Native adaptation of pixelfx water/water-jet.js (MIT), following Sprite Fusion energy beam build.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Descriptor.h"
#include "stylize/pixelfx/PixelFxInternal.h"
#include "stylize/pixelfx/Water.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double D = 1, G = 520, SPEED = 260;
double           drop(double x, double L) { return std::pow(x / L, 2) * 9; }
double           thick(double x, double L) { return 2.6 + 5.4 * std::pow(x / L, 1.1); }
void             draw(Paint& p, double t, const PixelFxSettings& o) {
    const auto&  C  = o.palette;
    const double ph = t / D, L = o.length, T = ph * TAU;
    for (int X = 0; X <= L + 3; ++X) {
        const double x = X + .5, u = x / L;
        if (u > 1) break;
        const double yc = drop(x, L), rough = smooth(.25, 1, u), sd = wrap(x - ph * SPEED * 2, 130) - 65,
                     w0 = thick(x, L) + std::exp(-(sd * sd) / 90) * 1.4 * (1 - u * .5),
                     up = w0 + tileNoise(x * .18 - ph * 30, 9, 30, 0, o.seed + 1) * 2.2 * rough,
                     dn = w0 + tileNoise(x * .18 - ph * 30, 3, 30, 0, o.seed + 1) * 2.2 * rough;
        for (int Y = int(std::floor(yc - up)); Y <= std::ceil(yc + dn); ++Y) {
            const double y = Y + .5, q = (y - yc) / std::max(.8, y < yc ? up : dn), aq = std::abs(q);
            if (aq > 1) continue;
            if (u > .55 &&
                tileNoise(x * .12 - ph * 25, (y - yc) * .25, 25, 0, o.seed + 2) < -.9 + 1.6 * smooth(.55, 1, u))
                continue;
            const double streak = tileNoise(x * .06 - ph * 40, std::floor(y - yc + 20) * 1.7, 40, 0, o.seed);
            int          level  = aq < .2 ? 0 : aq < .48 ? 1 : aq < .78 ? 2 : 3;
            if (streak > .35) level = std::max(0, level - 1);
            if (q > .4) level = std::min(3, level + 1);
            const std::array<uint32_t, 4> ramp{{WHITE, C[6], C[5], C[4]}};
            p.px(X, Y, ramp[size_t(level)]);
            if (level == 0) p.light(x, y, C[5], .12);
        }
    }
    const double fl = .85 + .15 * std::sin(T * 6);
    p.circle(0, 0, 4.4 * fl, C[5]);
    p.circle(0, 0, 3.4 * fl, C[6]);
    p.circle(0, 0, 2.2 * fl, WHITE);
    for (double a = 0; a < TAU; a += .35)
        p.dot(std::cos(a + T * 2) * 5.2 * fl, std::sin(a + T * 2) * 5.2 * fl, C[5], .6);
    p.glow(0, 0, 24, C[5], .6 * fl);
    p.glow(0, 0, 8, WHITE, .4 * fl);
    for (int k = 0; k < 6; ++k) {
        const double x = L * .82 + std::fmod(k * 13 + ph * SPEED, 78),
                     y = drop(std::min(x, L), L) + (x > L ? std::pow((x - L) / 60, 2) * 10 : 0) +
                         std::sin(k * 2.1) * 1.5;
        if (x < L + 34) water::bead(p, x, y, SPEED, 40, k % 3 == 0 ? 3 : 2, C);
    }
    cycle(t, D, 70, o.seed + 3, .2, .42, [&](double age, double life, Rng& rng, int) {
        const double f = std::sqrt(rng.next()), x0 = L * (.3 + .7 * f),
                     y0 = drop(x0, L) + rng.range(-1, 1) * thick(x0, L), vx = rng.range(150, 270) * (.55 + .45 * f),
                     vy0 = rng.range(-1, 1) * 110 * f, vy = vy0 + rng.range(-10, 20), x = x0 + vx * age,
                     y = y0 + vy * age + .5 * G * age * age, k = age / life;
        if (x > L + 50 || y > 40 || y < -28 || (k > .75 && rng.next() < (k - .75) * 4)) return;
        if (rng.next() < .45)
            p.px(x, y, k < .5 ? WHITE : C[6]);
        else
            water::bead(p, x, y, vx, vy + G * age, rng.next() < .2 ? 2 : 1, C);
    });
    cycle(t, D, 12, o.seed + 4, .7, 1, [&](double age, double life, Rng& rng, int) {
        const double k = age / life, x0 = L * rng.range(.7, 1), x = x0 + age * rng.range(20, 45),
                     y0 = drop(std::min(x, L), L) + rng.range(-5, 5), y = y0 + age * rng.range(4, 14),
                     r0 = rng.range(3, 5), rad = r0 + k * rng.range(5, 9),
                     a = rng.range(.1, .18) * std::min(1., k / .15) * std::pow(1 - k, 1.5);
        p.disc(x, y, rad, C[6], [&](double d) { return a * (1 - d * d); });
    });
    for (double x = 0; x < L; x += 10) p.glow(x, drop(x, L), 14 + x * .1, C[4], .26 * (1 - (x / L) * .5));
    p.glow(L * .9, drop(L * .9, L), 26, C[5], .22);
}
}  // namespace
const Descriptor& waterJetDescriptor() {
    static const std::array<Option, 1> options{{{"length", 8, 512, false, 0, &PixelFxSettings::length}}};
    static const Descriptor            d{{PixelFxKind::WaterJet, "water/water-jet", 188, 80, 14, 32, D, true},
                                         8,
                                         [](PixelFxSettings& s) {
                                  water::defaults(s, 3);
                                  s.length = 118;
                                         },
                                         draw,
                                         options};
    return d;
}
}  // namespace eve::stylize::pixelfx
