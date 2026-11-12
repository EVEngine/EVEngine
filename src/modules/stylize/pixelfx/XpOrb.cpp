// eve-hot-path
// Native adaptation of pixelfx pickups/xp-orb.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Descriptor.h"
#include "stylize/pixelfx/Pickup.h"
#include "stylize/pixelfx/PixelFxInternal.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double D = 1.8, TAIL = .48;
Point            orbAt(double t) {
    const double a = t / D * TAU;
    return {16 * std::sin(a), -20 + 9 * std::sin(a) * std::cos(a) - 1.2 * std::cos(a * 2)};
}
void back(Paint& p, double t, const PixelFxSettings& o) {
    const auto& C = o.palette;
    const auto  q = orbAt(t);
    pickup::blobShadow(p, q.x, 3.5, .55);
    pickup::pool(p, q.x, .5, 14, 3, C[5], .3);
    cycle(t, D, 16, o.seed, .35, .7, [&](double age, double life, Rng& r, int) {
        const auto   f = orbAt(t - age);
        const double k = 1 - age / life, w = r.range(20, 30), phase = r.range(0, TAU), tw = std::sin(age * w + phase),
                     dx = r.range(-2, 2), vx = r.range(-6, 6), dy = r.range(-2, 2), vy = r.range(4, 10),
                     x = f.x + dx + age * vx, y = f.y + dy + age * vy;
        if (tw * k > -.2) p.px(x, y, tw > .6 && k > .5 ? WHITE : k > .5 ? C[6] : C[5]);
        p.glow(x + .5, y + .5, 3, C[5], .3 * k);
    });
}
void front(Paint& p, double t, const PixelFxSettings& o) {
    const auto&  C    = o.palette;
    const auto   q    = orbAt(t);
    const double beat = .5 + .5 * std::sin(t / D * TAU * 3);
    for (int i = 60; i >= 1; --i) {
        const double u = double(i) / 60;
        const auto   a = orbAt(t - u * TAIL);
        const double r = 3.1 * std::pow(1 - u, 1.1);
        const auto   c = u < .25 ? C[5] : u < .55 ? C[4] : C[3];
        if (r > .6)
            p.circle(a.x, a.y, r, c, u < .7 ? 1 : (1 - u) * 3.3);
        else
            p.dot(a.x, a.y, c, (1 - u) * 3);
        if (i % 6 == 0) p.glow(a.x, a.y, 7 * (1 - u) + 2, C[5], .24 * (1 - u));
    }
    for (int i = 36; i >= 1; --i) {
        const double u = double(i) / 60;
        const auto   a = orbAt(t - u * TAIL);
        p.dot(a.x, a.y, u < .2 ? WHITE : C[6], (.6 - u) * 4);
    }
    const double X = jsRound(q.x), Y = jsRound(q.y);
    p.circle(X, Y, 4.4, hex(0x0a1406));
    p.circle(X, Y, 3.6, [&](double d, double a) {
        return d > .72  ? (std::cos(a + 2.4) > .3 ? C[5] : C[3])
               : d > .4 ? (std::cos(a + 2.4) > -.2 ? C[6] : C[5])
                        : WHITE;
    });
    p.px(X - 2, Y - 2, WHITE);
    p.px(X - 1, Y - 2, C[7]);
    p.glow(q.x, q.y, 16 + 3 * beat, C[5], .36 + .14 * beat);
    const double g = std::fmod(t / D * 3, 1);
    if (g < .12) pickup::glint(p, X - 2, Y - 2, 2.4 * std::sin(g / .12 * PI), WHITE, .4);
}
}  // namespace
const Descriptor& xpOrbDescriptor() {
    static const Descriptor d{{PixelFxKind::XpOrb, "pickups/xp-orb", 72, 56, 36, 48, D, true},
                              8,
                              [](PixelFxSettings& s) {
                                  s.seed    = 2;
                                  s.palette = {hex(0x0c1806), hex(0x1e3a0a), hex(0x36620e), hex(0x5c9614),
                                               hex(0x8cd024), hex(0xc4f060), hex(0xeffcc0), WHITE};
                              },
                              front,
                              {},
                              nullptr,
                              back};
    return d;
}
}  // namespace eve::stylize::pixelfx
