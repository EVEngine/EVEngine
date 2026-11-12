// eve-hot-path
// Native adaptation of pixelfx ice/blizzard-vortex.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Descriptor.h"
#include "stylize/pixelfx/PixelFxInternal.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double D = 2, TILT = .24;
double           radius(double h, double H) { return 4 + 28 * std::pow(clamp(h / H), 1.35); }
double           axis(double h, double H, double ph) {
    return (5 * std::sin(TAU * ph + h * .05) + 1.5 * std::sin(TAU * ph * 2 - h * .11)) * (h / H) - 2 * (h / H);
}
void streaks(Paint& p, double t, const PixelFxSettings& o, bool front) {
    const auto&  C = o.palette;
    const double H = o.ice.height, ph = t / D;
    cycle(t, D, 58, o.seed + 9, .5, .9, [&](double age, double life, Rng& r, int) {
        const double h0 = r.range(2, H), w = r.range(8, 12) * (1.45 - h0 / H), th = r.range(0, TAU) + age * w,
                     k    = age / life;
        const double fade = std::min(1.0, age / .08) * (k > .7 ? (1 - k) / .3 : 1), L = r.range(.7, 1.5),
                     m = std::ceil(L / .07);
        for (int j = 0; j <= m; ++j) {
            const double a = th - j * .07, s = std::sin(a);
            if ((s >= 0) != front) continue;
            const double h = std::min(H + 4, h0 + age * 16 - j * .25),
                         R = radius(std::min(h, H), H) * (1 + std::min(.3, std::max(0.0, h - H * .84) * .03)),
                         u = j / m;
            const double x = axis(std::min(h, H), H, ph) + std::cos(a) * R, y = -h + s * R * TILT;
            const auto   c = front ? (u < .15 ? WHITE : u < .5 ? C[6] : C[5]) : u < .3 ? C[5] : u < .6 ? C[4] : C[3];
            p.dot(x, y, c, fade * (u < .6 ? 1 : 1.5 - u * 1.2) * (front ? 1 : .85));
        }
    });
}
void body(Paint& p, std::span<const uint32_t> C, double ph, double H) {
    p.shade(-44, -H - 6, 44, 2, [&](double x, double y, int sx, int sy) {
        const double h = -y;
        if (h < 0 || h > H + 4) return;
        const double R = radius(h, H), dx = x - axis(h, H, ph), u = dx / R;
        if (u * u >= 1) return;
        const double k = h / H, rim = std::pow(std::abs(u), 3),
                     swirl = std::sin(std::asin(u) * 3 - TAU * ph * 2 + h * .2) * .5 + .5;
        const double a     = (.07 + .24 * rim + .12 * swirl) * clamp((1 - std::abs(u)) / .18) *
                             (k > .7 ? std::max(0.0, (1 - k) / .3) : 1) * (h < 4 ? h / 4 : 1);
        if (a < .03) return;
        p.blendPx(sx, sy, rim > .5 ? C[6] : swirl > .6 ? C[5] : C[4], a);
        if (rim > .3 || swirl > .75) p.add(sx, sy, C[5], .16 * std::max(rim, swirl - .5) * (1 - k * .5));
    });
}
void orbiters(Paint& p, double t, const PixelFxSettings& o, bool front) {
    const auto&  C = o.palette;
    const double H = o.ice.height, ph = t / D;
    cycle(t, D, 76, o.seed, .9, 1.6, [&](double age, double life, Rng& r, int i) {
        const double h0 = r.range(0, H * .6), h = h0 + age * r.range(14, 30), k = age / life;
        if (h > H + 8) return;
        const double w = r.range(7, 11) * (1.5 - h / H), th = r.range(0, TAU) + age * w,
                     out = 1 + r.range(.05, .3) + std::min(.35, std::max(0.0, h - H * .82) * .04);
        const double R = radius(std::min(h, H), H) * out, s = std::sin(th);
        if ((s >= 0) != front) return;
        const double x = axis(std::min(h, H), H, ph) + std::cos(th) * R, y = -h + s * R * TILT;
        const bool   big  = i % 5 == 0;
        const double fade = std::min(1.0, age / .1) * (k > .8 ? (1 - k) / .2 : 1);
        if (fade <= 0) return;
        const double th2 = th - .25, x2 = axis(std::min(h, H), H, ph) + std::cos(th2) * R,
                     y2 = -h + 2 + std::sin(th2) * R * TILT;
        if (front) {
            p.lineFn(
                x2, y2, x, y, [&](double u) { return u > .6 ? C[6] : C[4]; }, [fade](double u) { return u * fade; });
            p.dot(x, y, big ? WHITE : r.isChance(.5) ? WHITE : C[6], fade);
            if (big) {
                for (const auto d : std::array<Point, 4>{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}})
                    p.dot(x + d.x, y + d.y, C[6], fade);
                p.glow(x, y, 5, C[5], .35 * fade);
            }
        } else {
            p.lineFn(x2, y2, x, y, C[3], [fade](double u) { return u * fade * .8; });
            p.dot(x, y, big ? C[5] : C[4], fade);
        }
    });
}
void foot(Paint& p, double t, const PixelFxSettings& o, bool front) {
    const auto&  C  = o.palette;
    const double ph = t / D;
    cycle(t, D, 12, o.seed + 5, .8, 1.2, [&](double age, double life, Rng& r, int) {
        const double th0 = r.range(0, TAU), speed = r.range(4, 6), th = th0 + age * speed, R = 9 + age * r.range(8, 16),
                     s = std::sin(th), k = age / life;
        if ((s >= 0) != front) return;
        const double x = axis(0, 1, ph) + std::cos(th) * R, y = -2 - age * r.range(4, 10) + s * R * .2, rad = 2 + 4 * k,
                     a = .5 * std::pow(1 - k, 1.2) * std::min(1.0, age / .1);
        p.disc(x, y, rad, front ? C[6] : C[5], [a](double d) { return a * (1 - d * d); });
        p.disc(x - rad * .25, y - rad * .3, rad * .5, WHITE,
               [a, k](double d) { return a * .5 * (1 - d * d) * (1 - k); });
    });
}
void back(Paint& p, double t, const PixelFxSettings& o) {
    const auto& C = o.palette;
    for (int x = -30; x <= 30; ++x) {
        const double d = std::abs(x) / 30., hgt = jsRound((1 - d * d) * (1.5 + rand({jsUint(x), 3}) * 1.5));
        for (int j = 0; j < hgt; ++j) p.px(x, -j, j == hgt - 1 ? WHITE : C[5]);
        p.dot(x, 0, C[6], 1 - d * .5);
        p.dot(x, 1, C[4], .8 - d * .6);
    }
    p.glow(0, -2, 40, C[4], .22);
    foot(p, t, o, false);
    streaks(p, t, o, false);
    orbiters(p, t, o, false);
}
void front(Paint& p, double t, const PixelFxSettings& o) {
    const auto&  C = o.palette;
    const double H = o.ice.height, ph = t / D;
    body(p, C, ph, H);
    p.glow(axis(H * .45, H, ph), -H * .45, 40, C[4], .3);
    p.glow(axis(H * .15, H, ph), -H * .15, 18, C[5], .4 + .1 * std::sin(TAU * ph * 4));
    p.glow(axis(H * .05, H, ph), -3, 10, WHITE, .3);
    streaks(p, t, o, true);
    orbiters(p, t, o, true);
    foot(p, t, o, true);
    cycle(t, D, 10, o.seed + 3, .9, 1.3, [&](double age, double life, Rng& r, int) {
        const double th = r.range(0, TAU), R = radius(H, H) * (.6 + age * .9), k = age / life, rad = 3 + 6 * k;
        const double Rc = std::min(R, 36.0), x = axis(H, H, ph) + std::cos(th) * Rc,
                     y = -H - 2 - age * 4 + std::sin(th) * Rc * TILT, a = .24 * (1 - k) * std::min(1.0, age / .15);
        p.disc(x, y, rad * .8, C[6], [a](double d) { return a * (1 - d * d); });
        p.glow(x, y, rad * 1.5, C[4], .08 * (1 - k));
    });
}
}  // namespace
const Descriptor& blizzardVortexDescriptor() {
    static const std::array<Option, 1> options{{{"height", 4, 256, false, 0, &PixelFxIceSettings::height}}};
    static const Descriptor d{{PixelFxKind::BlizzardVortex, "ice/blizzard-vortex", 128, 116, 64, 102, D, true},
                              8,
                              [](PixelFxSettings& s) {
                                  s.seed       = 17;
                                  s.ice.height = 80;
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
