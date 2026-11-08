// eve-hot-path
// Native adaptation of pixelfx water/whirlpool.js (MIT), following Sprite Fusion black-hole streaks.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Descriptor.h"
#include "stylize/pixelfx/PixelFxInternal.h"
#include "stylize/pixelfx/Water.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double D = 3, R = 60, SY = .6, K = 2.4, EYE = .16;
double           foam(double rho, double th, double ph) {
    const double s = std::log(std::max(rho, .02)), a = th + K * s + ph * TAU, b = th + K * 1.35 * s + ph * TAU * 2,
                 one   = std::pow(.5 + .5 * std::cos(7 * a + .9 * std::sin(3 * a + s * 4)), 26),
                 two   = std::pow(.5 + .5 * std::cos(11 * b + 1.3), 34) * .7,
                 three = std::pow(.5 + .5 * std::cos(17 * (th + K * .8 * s + ph * TAU * 3)), 30) * .8 *
                         std::max(0., 1 - (rho - EYE) * 2.2),
                 dash  = .55 + .45 * std::sin(s * 22 - ph * TAU * 4 + a * 2);
    return (one + two + three) * dash;
}
void draw(Paint& p, double t, const PixelFxSettings& o) {
    const auto&  C  = o.palette;
    const double ph = t / D, ry = R * SY;
    p.shader(-R, -ry, R, ry, [&](double x, double y, ShadeOutput& out) {
        const double dx = x / R, dy = y / ry, rho = jsHypot2(dx, dy);
        if (rho > 1) return;
        const double th = std::atan2(dy, dx);
        if (rho < EYE) {
            const double far = -dy / rho, wall = (rho / EYE - .35) / .65;
            if (far > 0)
                out.c = wall > .8 ? (far > .6 ? C[5] : C[4]) : wall > .45 ? C[3] : wall > .15 ? C[2] : C[0];
            else
                out.c = wall > .85 && far > -.5 ? C[2] : C[0];
            return;
        }
        const double k = (rho - EYE) / (1 - EYE), fade = 1 - smooth(.62, 1, k);
        p.blend(x, y, k < .25 ? C[0] : C[1], (.75 - .25 * k) * (1 - smooth(.8, 1, k)));
        if (k > .3 && k < .8) p.blend(x, y, C[2], .18 * std::sin(PI * (k - .3) / .5));
        if (k < .035) {
            const double g = .5 + .5 * std::cos(th * 9 - ph * TAU * 6);
            out.c          = g > .7 ? WHITE : g > .3 ? C[6] : C[4];
            return;
        }
        const double intensity = foam(rho, th, ph) * fade * std::min(1., (k - .08) * 4) *
                                 (.45 + .55 * std::sin(PI * std::min(1., k * 1.25)));
        if (intensity > .12) {
            out.c = intensity > .75 ? WHITE : intensity > .45 ? C[6] : intensity > .25 ? C[5] : C[4];
            if (intensity > .25) {
                out.l = C[5];
                out.k = .08 + .14 * std::min(1., intensity);
            }
        }
    });
    cycle(t, D, 30, o.seed, 1.2, 2.2, [&](double age, double life, Rng& rng, int) {
        const double u = age / life, rho = .8 - .62 * u * u, a0 = rng.range(0, TAU), th = a0 - K * std::log(rho),
                     ang = th - age * (1.5 + 2.2 / rho), x = std::cos(ang) * rho * R, y = std::sin(ang) * rho * ry,
                     ang2 = ang + .12 / rho, x2 = std::cos(ang2) * rho * R, y2 = std::sin(ang2) * rho * ry;
        if (u > .85 && rng.next() < (u - .85) * 6) return;
        p.line(x2, y2, x, y, u < .2 ? C[5] : u < .6 ? WHITE : C[6]);
    });
    cycle(t, D, 8, o.seed + 3, .25, .45, [&](double age, double life, Rng& rng, int) {
        const double rho = rng.range(.35, .8), th = rng.range(0, TAU) - age * 2.5, k = std::sin((PI * age) / life),
                     x = std::cos(th) * rho * R, y = std::sin(th) * rho * ry;
        if (foam(rho, th, ph) < .35) return;
        p.px(x, y, WHITE);
        if (k > .55)
            for (const Point off : std::array<Point, 4>{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}})
                p.px(x + off.x, y + off.y, C[6]);
        p.glow(x, y, 5, C[5], .4 * k);
    });
    p.glow(0, 0, R * .9, C[4], .2);
    p.glow(0, 0, R * EYE * 1.6 + 6, C[5], .28);
    p.glow(0, -ry * EYE * .8, 14, C[5], .32);
}
}  // namespace
const Descriptor& whirlpoolDescriptor() {
    static const Descriptor d{{PixelFxKind::Whirlpool, "water/whirlpool", 128, 84, 64, 42, D, true},
                              8,
                              [](PixelFxSettings& s) { water::defaults(s, 5); },
                              draw};
    return d;
}
}  // namespace eve::stylize::pixelfx
