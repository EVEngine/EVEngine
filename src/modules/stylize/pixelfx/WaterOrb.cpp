// eve-hot-path
// Native adaptation of pixelfx water/water-orb.js (MIT), following Sprite Fusion energy orb recipes.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Descriptor.h"
#include "stylize/pixelfx/PixelFxInternal.h"
#include "stylize/pixelfx/Water.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double D = 3, SY = .3, G = 700, DRIP = 1.5, LET = .5;
Point            center(double ph) { return {0, -52 + std::sin(ph * TAU) * 2}; }
double           skin(double a, double R, double ph) {
    const double time = std::fmod(ph * D, DRIP) / DRIP,
                 sag  = std::pow(std::max(0., std::sin(a)), 6) * 2.2 * smooth(.1, LET, time) * (time < LET ? 1 : 0);
    return R + std::sin(a * 3 + ph * TAU * 2) * .6 + std::sin(a * 5 - ph * TAU * 3) * .35 + sag;
}
struct Mote {
    double x, y, z;
    int    i;
};
std::array<Mote, 7> motes(double ph, double R) {
    std::array<Mote, 7> out{};
    for (int i = 0; i < 7; ++i) {
        const double a = (double(i) / 7) * TAU + ph * TAU * (i % 2 ? 1 : -1), r = R + 5 + (i % 3) * 3,
                     tilt = (i - 3) * .22, x = std::cos(a) * r, z = std::sin(a);
        out[size_t(i)] = {x * std::cos(tilt), x * std::sin(tilt) + z * r * .28, z, i};
    }
    return out;
}
void back(Paint& p, double t, const PixelFxSettings& o) {
    const auto&  C  = o.palette;
    const double ph = t / D, R = o.water.radius;
    const auto   pos = center(ph);
    p.glow(pos.x, 0, 22, C[5], .32);
    p.shader(pos.x - 9, -2, pos.x + 9, 2, [&](double x, double y, ShadeOutput& out) {
        const double d = std::pow(x - pos.x, 2) / 64 + std::pow(y * 2.5, 2) / 9;
        if (d > 1) return;
        out.c = d < .25 ? WHITE : d < .6 ? C[6] : C[5];
        out.a = 1.2 - d;
    });
    for (const auto& m : motes(ph, R))
        if (m.z < 0) p.px(pos.x + m.x, pos.y + m.y, C[4]);
    p.glow(pos.x, pos.y, R * 3.2, C[4], .3 + .05 * std::sin(ph * TAU * 2));
}
void front(Paint& p, double t, const PixelFxSettings& o) {
    const auto&  C  = o.palette;
    const double ph = t / D, R = o.water.radius, T = ph * TAU;
    const auto   pos = center(ph);
    const double cx = pos.x, cy = pos.y;
    p.shader(cx - R - 4, cy - R - 4, cx + R + 4, cy + R + 6, [&](double x, double y, ShadeOutput& out) {
        const double dx = x - cx, dy = y - cy, d = jsHypot2(dx, dy), a = std::atan2(dy, dx), r = skin(a, R, ph);
        if (d > r) return;
        const double q = d / r, lit = (-dx - dy) / std::numbers::sqrt2 / std::max(.5, d);
        if (q > .9) {
            out.c = lit > .35 ? WHITE : lit > -.2 ? C[5] : C[3];
            return;
        }
        const double gather = std::max(0., (dx + dy) / std::numbers::sqrt2 / r) * q;
        p.blend(x, y,
                gather > .45   ? C[4]
                : gather > .25 ? C[3]
                : q > .7       ? C[2]
                               : C[1],
                gather > .25 ? .7 : .35 + .3 * q);
    });
    for (int k = 0; k < 3; ++k) {
        const double rr = R * (.38 + .2 * k), tilt = -.5 + k * .35, spin = T * (k % 2 ? -2 : 3) + k * 2;
        for (double s = 0; s < 1; s += .012) {
            const double a = spin + s * TAU, x = std::cos(a) * rr, z = std::sin(a),
                         y = x * std::sin(tilt) * .6 + z * rr * .62, dash = std::fmod(s * 3 + k * .3, 1);
            if (dash > .62) continue;
            p.px(cx + x * std::cos(tilt), cy + y, z > 0 ? (dash < .1 ? WHITE : dash < .4 ? C[6] : C[5]) : C[3]);
        }
    }
    cycle(t, D, 8, o.seed, 1, 1.8, [&](double age, double life, Rng& rng, int) {
        const double k = age / life, xx = rng.range(-.5, .5) * R, angle = rng.range(0, TAU),
                     x = xx + std::sin(age * 4 + angle) * 1.5, y = R * .6 - k * R * 1.2;
        if (jsHypot2(x, y) > R * .8) return;
        p.px(cx + x, cy + y, k < .8 ? C[6] : WHITE);
        if (rng.next() < .35) p.px(cx + x + 1, cy + y, C[5]);
    });
    p.arc(cx, cy, R * .74, 1.8, PI * 1.12, PI * 1.4,
          [&](double u, double v) { return u > .25 && u < .75 && v > .4 ? WHITE : C[6]; });
    p.px(cx - R * .45, cy - R * .62, WHITE);
    p.px(cx + R * .48, cy + R * .5, C[6]);
    p.px(cx + R * .48 + 1, cy + R * .5, C[5]);
    for (const auto& m : motes(ph, R))
        if (m.z >= 0) {
            p.px(cx + m.x, cy + m.y, WHITE);
            p.px(cx + m.x - (m.i % 2 ? -1 : 1), cy + m.y, C[5]);
            p.glow(cx + m.x, cy + m.y, 4, C[5], .35);
        }
    const double dt = std::fmod(t, DRIP) / DRIP, fall = std::fmod(t, DRIP) - DRIP * LET, y0 = cy + R + 3,
                 land = std::sqrt((2 * -y0) / G);
    if (fall < 0 && dt > .2) {
        const double s = smooth(.2, LET, dt);
        p.circle(cx, cy + R + 1 + s * 1.5, .8 + s * 1.2, C[5]);
        p.px(cx - .5, cy + R + s * 1.5, C[6]);
    } else if (fall >= 0 && fall < land)
        water::bead(p, cx, y0 + .5 * G * fall * fall, 0, G * fall, 2, C);
    else if (fall >= land) {
        const double since = fall - land;
        if (since < .08) p.px(cx, 0, WHITE);
        water::ring(p, cx, 0, 1.5 + since * 18, SY, 1.1 * (1 - since / .4), C, {1, 3, 1, since < .05, 0, 3});
    }
    const double fx = cx + R * .36, fy = cy + R * .4, pulse = .8 + .2 * std::sin(T * 4);
    p.circle(fx, fy, 1.6, C[6]);
    p.px(fx, fy, WHITE);
    p.glow(fx, fy, 9, C[5], .45 * pulse);
    p.glow(cx, cy, R * 1.4, C[5], .22);
    p.glow(cx - R * .4, cy - R * .5, 6, WHITE, .25);
}
}  // namespace
const Descriptor& waterOrbDescriptor() {
    static const std::array<Option, 1> options{{{"radius", 2, 40, false, 0, &PixelFxWaterSettings::radius}}};
    static const Descriptor            d{{PixelFxKind::WaterOrb, "water/water-orb", 100, 108, 50, 100, D, true},
                                         8,
                                         [](PixelFxSettings& s) {
                                  water::defaults(s, 2);
                                  s.water.radius = 19;
                                         },
                                         front,
                                         options,
                                         nullptr,
                                         back};
    return d;
}
}  // namespace eve::stylize::pixelfx
