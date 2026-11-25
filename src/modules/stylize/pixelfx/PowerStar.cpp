// eve-hot-path
// Native adaptation of pixelfx pickups/power-star.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Descriptor.h"
#include "stylize/pixelfx/Pickup.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double                D = 1.6, HOVER = 25, RO = 11.5, RI = 5.2;
constexpr std::array<double, 2> BEATS{.25, .75};
double                          swell(double ph) {
    double sum = 0;
    for (double b : BEATS) {
        const double u = wrap(ph - b, 1);
        sum += std::exp(-u * 14) * (1 - u * 2.5 > 0 ? 1 : 0);
    }
    return sum;
}
void star(Paint& p, std::span<const uint32_t> C, double cx, double cy, double rot, double sc) {
    pickup::Layer<44, 44> L;
    const double          LA = std::atan2(-.8, -.6);
    L.begin(std::floor(cx) - 21, std::floor(cy) - 21);
    L.fill(std::floor(cx) - 21, std::floor(cy) - 21, std::floor(cx) + 20, std::floor(cy) + 20,
           [&](double px, double py) -> uint32_t {
               const double dx = (px - cx) / sc, dy = (py - cy) / sc, r = jsHypot2(dx, dy);
               if (r > RO + .5) return 0;
               const double a = wrap(std::atan2(dy, dx) - rot + PI / 2, TAU), k = std::floor(a / (PI / 5));
               const bool   half = std::fmod(k, 2) != 0;
               const double tip = (std::floor(k / 2) + (half ? 1 : 0)) * (2 * PI / 5), tA = tip - PI / 2 + rot,
                            iA = tA + (half ? -1 : 1) * (PI / 5), tx = std::cos(tA) * RO, ty = std::sin(tA) * RO,
                            ix = std::cos(iA) * RI, iy = std::sin(iA) * RI, ex = ix - tx, ey = iy - ty,
                            side = (dx - tx) * ey - (dy - ty) * ex, cside = -tx * ey + ty * ex;
               if (side * cside < 0) return 0;
               const double n = tA + (half ? -1 : 1) * (PI / 2), lit = std::cos(n - LA);
               const bool   ridge = std::abs(std::sin(std::atan2(dy, dx) - tA)) * r < .6;
               if (r < 1.2) return WHITE;
               const double toLight = std::cos(tA - LA);
               if (ridge) return toLight > .3 ? WHITE : toLight > -.4 ? C[6] : C[5];
               return C[lit > .6 ? 6 : lit > .15 ? 5 : lit > -.35 ? 4 : lit > -.75 ? 3 : 2];
           });
    L.outline(hex(0x1a0e04));
    L.draw(p);
}
void back(Paint& p, double t, const PixelFxSettings& o) {
    const auto&  C  = o.palette;
    const double ph = t / D, y = -HOVER - 1.5 * std::sin(ph * TAU * 2), sw = swell(ph), spin = ph * (TAU / 10 * 2);
    pickup::blobShadow(p, 0, 9 + 2 * sw, .7);
    for (int i = 0; i < 10; ++i) {
        const double a = spin + i * TAU / 10, len = (i % 2 ? 22 : 34) * (1 + .18 * sw), ca = std::cos(a),
                     sa = std::sin(a), w = i % 2 ? .06 : .09;
        p.shader(-len - 2, y - len - 2, len + 2, y + len + 2, [&](double x, double yy, ShadeOutput& out) {
            const double rx = x, ry = yy - y, d = rx * ca + ry * sa;
            if (d < 8 || d > len) return;
            const double q = std::abs(rx * sa - ry * ca), hw = .5 + d * w;
            if (q > hw + 1.5) return;
            out.l = q < hw * .5 ? C[6] : C[5];
            out.k = (.32 + .22 * sw) * (q < hw ? 1 : 1 - (q - hw) / 1.5) * std::pow(1 - d / len, 1.2) *
                    std::min(1.0, (d - 8) / 4);
        });
        if (i % 2 == 0) {
            const double d0 = RO + 3, d1 = std::min(len * .88, sa > .05 ? (-2 - y) / sa : INFINITY);
            if (d1 > d0)
                p.lineFn(
                    ca * d0, y + sa * d0, ca * d1, y + sa * d1, [&](double u) { return u < .35 ? C[6] : C[5]; },
                    [sw](double u) { return (u < .5 ? 1 : 1.6 - u * 1.4) * (.75 + .35 * sw); });
        }
    }
}
void front(Paint& p, double t, const PixelFxSettings& o) {
    const auto&  C  = o.palette;
    const double ph = t / D, y = -HOVER - 1.5 * std::sin(ph * TAU * 2), sw = swell(ph), rot = .22 * std::sin(ph * TAU),
                 sc = 1 + .13 * sw;
    p.glowRing(0, y, RO * sc + 3, 13, C[4], .32 + .25 * sw);
    p.glow(0, y, 6, C[6], .12 + .2 * sw);
    star(p, C, 0, y, rot, sc);
    for (double b : BEATS) {
        const double age = wrap(ph - b, 1) * D;
        if (age > .45) continue;
        for (uint32_t i = 0; i < 5; ++i) {
            const double a = rot + i * TAU / 5 - PI / 2, sp = 70 + rand({i, jsUint(b * 10), 3}) * 30,
                         vx = std::cos(a) * sp - std::sin(a) * 35, vy = std::sin(a) * sp + std::cos(a) * 35;
            const auto   q = fly(std::cos(a) * RO * sc, y + std::sin(a) * RO * sc, vx, vy, age, 60, 4);
            const double k = 1 - age / .45;
            pickup::glint(p, q.x, q.y, jsRound(2 * k), k > .5 ? WHITE : C[6], .45 * k);
        }
    }
    const auto f = jsUint(std::floor(ph * 24));
    for (uint32_t i = 0; i < 18; ++i) {
        if (rand({f + i * 3, i, o.seed}) > .32) continue;
        const double a = rand({i, 1, o.seed}) * TAU, d = 13 + rand({i, 2, o.seed}) * 20, sy = y + std::sin(a) * d * .85;
        if (sy < -2) p.px(std::cos(a) * d, sy, rand({f, i, 5}) < .5 ? WHITE : C[6]);
    }
    for (uint32_t i = 0; i < 4; ++i) {
        const double life = .3, age = wrap(t - (i + .3) / 4 * D, D);
        if (age > life) continue;
        const double a = rand({i, 7, o.seed}) * TAU, d = 15 + rand({i, 8, o.seed}) * 12, k = std::sin(age / life * PI);
        pickup::glint(p, std::cos(a) * d, y + std::sin(a) * d * .85, jsRound(2.5 * k), i % 2 ? C[6] : WHITE, .4 * k);
    }
    const double g = std::fmod(ph + .1, .5) / .5;
    if (g < .14) pickup::glint(p, -2 + rot * 4, y - 6, 3 * std::sin(g / .14 * PI), WHITE, .5);
}
}  // namespace
const Descriptor& powerStarDescriptor() {
    static const Descriptor d{{PixelFxKind::PowerStar, "pickups/power-star", 88, 84, 44, 76, D, true},
                              8,
                              [](PixelFxSettings& s) {
                                  s.seed    = 9;
                                  s.palette = {hex(0x2a1604), hex(0x5e3208), hex(0xa05c0c), hex(0xe09414),
                                               hex(0xffc626), hex(0xffe866), hex(0xfff8c4), WHITE};
                              },
                              front,
                              {},
                              nullptr,
                              back};
    return d;
}
}  // namespace eve::stylize::pixelfx
