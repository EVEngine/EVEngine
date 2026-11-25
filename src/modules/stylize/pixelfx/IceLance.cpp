// eve-hot-path
// Native adaptation of pixelfx ice/ice-lance.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Descriptor.h"
#include "stylize/pixelfx/Ice.h"
#include "stylize/pixelfx/PixelFxInternal.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double D = .8;
void             spear(Paint& p, std::span<const uint32_t> C, double L, double pulse, double glint) {
    constexpr double T = 16, H = 3.5;
    p.shader(-L - 1, -H - 2, 1, H + 2, [&](double x, double y, ShadeOutput& o) {
        if (x > 0 || x < -L) return;
        const double h = x > -T ? H * (-x / T) + .3 : x < -L + 3 ? H * (.7 + .3 * ((x + L) / 3)) : H;
        if (std::abs(y) > h + .5) {
            if (std::abs(y) < h + 1.5) o.c = C[1];
            return;
        }
        const double v = y / std::max(.5, h);
        if (x > -1.5) {
            o.c = WHITE;
            return;
        }
        if (v > -.3 && v < .35 && std::abs(x - pulse) < 2.5) {
            o.c = WHITE;
            return;
        }
        if (v < -.3 && std::abs(x - glint) < 1) {
            o.c = WHITE;
            return;
        }
        o.c = v < -.72 ? WHITE : v < -.36 ? C[6] : v < -.18 ? C[7] : v < .38 ? C[4] : v < .78 ? C[2] : C[1];
    });
}
void draw(Paint& p, double t, const PixelFxSettings& o) {
    const auto&  C = o.palette;
    const double L = o.length, ph = t / D;
    p.shade(-L - 100, -12, -L + 4, 12, [&](double x, double y, int sx, int sy) {
        const double u = (-L + 4 - x) / 104,
                     w = 4.5 * std::pow(1 - u, .7) + .5 + 1.5 * std::sin(u * 9 - ph * TAU * 2) * u, v = std::abs(y) / w;
        if (v >= 1) return;
        const double a = (1 - v * v) * std::pow(1 - u, 1.4);
        if (a < .04) return;
        p.blendPx(sx, sy, v < .25 && u < .4 ? C[6] : v < .6 ? C[5] : C[4], a * .7);
        if (a > .3) p.add(sx, sy, C[5], .12 * a);
    });
    p.glowLine(-L - 60, 0, 0, 0, 14, C[4], .22);
    cycle(t, D, 14, o.seed, .35, .55, [&](double age, double life, Rng& r, int) {
        const double x0 = r.range(-L + 2, -8), y0 = r.range(-2.5, 2.5), vx = -r.range(150, 230),
                     vy = r.signedRange(8, 30);
        const auto   q  = fly(x0, y0, vx, vy, age, 0, 2.2);
        const double k = age / life, rad = 1.2 + 5.5 * k, a = .5 * std::pow(1 - k, 1.3);
        p.disc(q.x, q.y, rad, k < .3 ? C[6] : C[5], [&](double d) { return a * (1 - d * d); });
        p.glow(q.x, q.y, rad * 1.6, C[4], .1 * (1 - k));
    });
    cycle(t, D, 9, o.seed + 1, .4, .6, [&](double age, double life, Rng& r, int) {
        const double x0 = r.range(-L, -6), y0 = r.range(-3, 3), vx = -r.range(170, 260), vy = r.range(-35, 35);
        const auto   q = fly(x0, y0, vx, vy, age, 60, 1.2);
        const double k = age / life, rot = r.range(0, TAU), speed = r.range(10, 18);
        const int    side = r.sign();
        const double a = rot + age * speed * side, l = r.range(1.2, 2.2), ca = std::cos(a), sa = std::sin(a);
        if (k > .85 && std::fmod(age * 30, 2) < 1) return;
        p.line(q.x - ca * l, q.y - sa * l, q.x + ca * l, q.y + sa * l, C[5]);
        p.px(q.x + ca * l, q.y + sa * l, WHITE);
        if (std::cos(a * 2) > .9) {
            constexpr std::array<Point, 4> cross{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
            for (const auto& d : cross) p.px(q.x + d.x, q.y + d.y, C[6]);
            p.glow(q.x, q.y, 6, C[5], .45);
        }
    });
    cycle(t, D, 12, o.seed + 2, .16, .26, [&](double age, double life, Rng& r, int) {
        const double y = r.signedRange(4, 13), x0 = r.range(-20, 0), speed = r.range(280, 380), x = x0 - age * speed,
                     l = r.range(6, 16), k = age / life;
        p.lineFn(
            x, y, x + l, y, [&](double u) { return u > .7 ? C[6] : C[4]; },
            [&](double u) { return u * (1 - k) * 1.4; });
    });
    for (int i = 0; i < 3; ++i) {
        const double q = std::fmod(ph * 3 + double(i) / 3, 1), x = -4 - q * (L + 20), ry = 4.5 + q * 8,
                     rx = 1.2 + q * 2.6;
        if (q > .8) continue;
        for (int k = 0; k < 40; ++k) {
            const double an = (double(k) / 40) * TAU, px = x + std::cos(an) * rx, py = std::sin(an) * ry;
            if (std::abs(py) < 4.6 && px > -L - 2) continue;
            p.px(px, py, std::cos(an) > 0 ? (q < .35 ? WHITE : C[6]) : q < .4 ? C[5] : C[4]);
        }
        p.glowRing(x, 0, ry * .75, 3, C[5], .14 * (1 - q));
    }
    for (const auto& q : std::array<std::array<double, 3>, 3>{{{{-1, 10, 4}}, {{1, 9, 4}}, {{-.2, 6, 3}}}})
        ice::blade(p, -L + 3, q[0] * 1.5, -L + 3 - q[1], q[0] * (2 + q[1] * .5), q[2], C);
    const double pulse = -L + std::fmod(ph * 2, 1) * (L + 6), glint = -std::fmod(ph * 2 + .5, 1) * L * 1.2;
    spear(p, C, L, pulse, glint);
    p.glowLine(-L, 0, 0, 0, 10, C[4], .1);
    p.glow(pulse, 0, 8, C[6], .3);
    p.glow(0, 0, 8, WHITE, .4);
    p.glow(-L * .4, 0, 30, C[3], .16);
}
}  // namespace
const Descriptor& iceLanceDescriptor() {
    static const std::array<Option, 1> options{{{"length", 16, 256, false, 0, &PixelFxSettings::length}}};
    static const Descriptor            d{{PixelFxKind::IceLance, "ice/ice-lance", 176, 64, 146, 32, D, true},
                                         8,
                                         [](PixelFxSettings& s) {
                                  s.seed    = 13;
                                  s.length  = 40;
                                  s.palette = {hex(0x151a3d), hex(0x212f6b), hex(0x2c58aa), hex(0x3a8bd8),
                                               hex(0x62c8f4), hex(0xa6ecff), hex(0xdcfbff), WHITE};
                                         },
                                         draw,
                                         options};
    return d;
}
}  // namespace eve::stylize::pixelfx
