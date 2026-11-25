// eve-hot-path
// Native adaptation of pixelfx pickups/key-glint.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Descriptor.h"
#include "stylize/pixelfx/Pickup.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double D = 2, HOVER = 19, MID = 8, TIP = 17.5, G0 = .18, G1 = .5;
Point            toWorld(double u, double v, double cx, double cy, double a) {
    return {cx + (u - MID) * std::cos(a) - v * std::sin(a), cy + (u - MID) * std::sin(a) + v * std::cos(a)};
}
int part(double u, double v) {
    const double rb = jsHypot2(u, v);
    if (rb <= 4.6 && rb >= 2.1) return 1;
    if (u >= 4.3 && u <= 6 && std::abs(v) <= 1.9) return 2;
    if (u >= 6 && u <= TIP && std::abs(v) <= 1) return 3;
    if (u >= 12.6 && u <= TIP - .3 && v > 1 && v <= 4.6 && !(u > 14.3 && u < 15.4 && v > 2.7)) return 4;
    return 0;
}
void key(Paint& p, std::span<const uint32_t> C, double cx, double cy, double a, double gu) {
    const double          ca = std::cos(a), sa = std::sin(a);
    pickup::Layer<48, 40> L;
    L.begin(std::floor(cx) - 24, std::floor(cy) - 20);
    L.fill(std::floor(cx) - 24, std::floor(cy) - 20, std::floor(cx) + 23, std::floor(cy) + 19,
           [&](double px, double py) -> uint32_t {
               const double dx = px - cx, dy = py - cy, u = dx * ca + dy * sa + MID, v = -dx * sa + dy * ca;
               const int    k = part(u, v);
               if (!k) return 0;
               double ny;
               if (k == 1) {
                   const double rb = jsHypot2(u, v), out = rb > 3.4 ? 1 : -1, wx = u / rb * out, wy = v / rb * out;
                   ny = -((wx * ca - wy * sa) * -.6 + (wx * sa + wy * ca) * -.8);
               } else
                   ny = v < -.35 ? .8 : v > .35 && k != 4 ? -.6 : k == 4 ? (v > 3.6 ? -.5 : .1) : .2;
               int c = ny > .55 ? 6 : ny > .15 ? 4 : ny > -.3 ? 3 : 2;
               if (k == 2) c = std::min(7, c + 1);
               const double g = std::abs(u + v * .45 - gu);
               if (g < .8) return WHITE;
               if (g < 2) c = std::min(7, c + 2);
               return C[size_t(c)];
           });
    L.outline(C[0]);
    L.draw(p);
}
void back(Paint& p, double t, const PixelFxSettings&) {
    const double y = -HOVER - 1.6 * std::sin(t / D * TAU);
    pickup::blobShadow(p, 0, 10, .8 - (-y - HOVER + 1.6) * .06);
}
void front(Paint& p, double t, const PixelFxSettings& o) {
    const auto&  C  = o.palette;
    const double ph = t / D, y = -HOVER - 1.6 * std::sin(ph * TAU), a = .42 + .11 * std::sin(ph * TAU + .9),
                 q = (ph - G0) / (G1 - G0), gu = q > 0 && q < 1 ? -5 + (TIP + 7) * ease::inOutSine(q) : -100;
    const auto   b = toWorld(0, 0, 0, y, a), e = toWorld(TIP, 0, 0, y, a);
    p.glow(0, y, 24, C[4], .3);
    p.glowLine(b.x, b.y, e.x, e.y, 7, C[5], .16);
    key(p, C, 0, y, a, gu);
    for (uint32_t i = 0; i < 4; ++i) {
        const double life = .4, age = wrap(t - (i + .6) / 4 * D, D);
        if (age > life) continue;
        const double an = rand({i, o.seed, 8}) * TAU, d = 9 + rand({i, o.seed, 9}) * 6, k = std::sin(age / life * PI);
        pickup::glint(p, std::cos(an) * d, y + std::sin(an) * d * .8 - age * 6, jsRound(1.8 * k), i % 2 ? C[6] : WHITE,
                      .35 * k);
    }
    if (gu > -6) {
        const auto g = toWorld(clamp(gu, 0, TIP), 0, 0, y, a);
        p.glow(g.x, g.y, 10, C[6], .45 * std::sin(q * PI));
    }
    const auto   tip = toWorld(TIP, 0, 0, y, a);
    const double at  = (ph - G1 + .02) * D;
    if (at > 0 && at < .3) pickup::glint(p, tip.x, tip.y, 4.5 * (1 - at / .3), C[6], .6 * (1 - at / .3));
    for (uint32_t i = 0; i < 9; ++i) {
        const double life = .32 + rand({i, o.seed, 4}) * .3, age = at - rand({i, o.seed, 5}) * .05;
        if (age < 0 || age > life) continue;
        const double sp = 25 + rand({i, o.seed, 1}) * 45, an = a + (rand({i, o.seed, 2}) - .5) * 2.2,
                     vx = std::cos(an) * sp, vy = std::sin(an) * sp - 25;
        const auto   pos = fly(tip.x, tip.y, vx, vy, age, 380, 2.5), vel = flyV(vx, vy, age, 380, 2.5);
        if (pos.y > -.5) continue;
        const double mag = jsHypot2(vel.x, vel.y), speed = mag == 0 ? 1 : mag, len = std::min(4.0, 1 + speed * .03),
                     k = 1 - age / life;
        p.lineFn(
            pos.x - vel.x / speed * len, pos.y - vel.y / speed * len, pos.x, pos.y,
            [&](double u) { return u > .6 ? WHITE : C[5]; }, k * 1.6);
        p.glow(pos.x, pos.y, 3, C[5], .4 * k);
    }
}
}  // namespace
const Descriptor& keyGlintDescriptor() {
    static const Descriptor d{{PixelFxKind::KeyGlint, "pickups/key-glint", 72, 60, 36, 54, D, true},
                              8,
                              [](PixelFxSettings& s) {
                                  s.seed    = 7;
                                  s.palette = {hex(0x0e1018), hex(0x222838), hex(0x3c4660), hex(0x5e6d8e),
                                               hex(0x8c9cbc), hex(0xbccae2), hex(0xe6eeff), WHITE};
                              },
                              front,
                              {},
                              nullptr,
                              back};
    return d;
}
}  // namespace eve::stylize::pixelfx
