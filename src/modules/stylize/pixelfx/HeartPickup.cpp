// eve-hot-path
// Native adaptation of pixelfx pickups/heart-pickup.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Descriptor.h"
#include "stylize/pixelfx/Pickup.h"
#include "stylize/pixelfx/PixelFxInternal.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double                          D = 1.2, HOVER = 18, W = 7.2, LUB = .04, DUB = .24;
constexpr std::array<std::string_view, 4> MINI{".a.a.", "abbba", ".aba.", "..a.."};
double                                    beat(double ph, double at, double s) {
    const double u = ph - at;
    if (u < 0 || u > .3) return 0;
    return s * std::exp(-u * 16) * std::cos(u * 34);
}
double pulse(double ph) { return beat(ph, LUB, 1) + beat(ph, DUB, .55) + beat(ph + 1, LUB, 1); }
void   back(Paint& p, double t, const PixelFxSettings& o) {
    const auto&  C  = o.palette;
    const double ph = t / D, y = -HOVER - 1.2 * std::sin(ph * TAU);
    pickup::blobShadow(p, 0, 8 + 1.5 * pulse(ph), .8);
    cycle(t, D, 4, o.seed, .95, [&](double age, double life, Rng& r, int i) {
        const int                 side = i % 2 ? 1 : -1;
        const double              k = age / life, x0 = r.range(2, 5), vx = r.range(9, 15), phase = r.range(0, TAU),
                                  vy = r.range(14, 19), x = side * (x0 + age * vx) + std::sin(age * 7 + phase) * 1.5,
                                  yy = y - 5 - age * vy, a = std::min(1.0, age / .12) * (1 - k * k);
        std::array<uint32_t, 256> colors{};
        colors['a'] = k < .5 ? C[5] : C[4];
        colors['b'] = k < .3 ? WHITE : C[6];
        SpriteOptions opts;
        opts.alpha = a * 1.3;
        p.sprite(MINI, colors, jsRound(x - 2.5), jsRound(yy - 2), opts);
        p.glow(x, yy, 5, C[4], .3 * a);
    });
}
void front(Paint& p, double t, const PixelFxSettings& o) {
    const auto&  C  = o.palette;
    const double ph = t / D, y = -HOVER - 1.2 * std::sin(ph * TAU), b = pulse(ph), sx = 1 + .16 * b, sy = 1 - .13 * b,
                 cy = jsRound(y);
    p.glow(0, cy, 22 + 6 * std::max(0.0, b), C[4], .26 + .3 * std::max(0.0, b));
    pickup::Layer<40, 40> L;
    L.begin(-20, cy - 20);
    pickup::pillow(
        L, -12, cy - 11, 12, cy + 11,
        [&](double x, double yy) { return pickup::heartSdf(x, yy, 0, cy, W * sx, W * sy); },
        [&](double light, double depth) {
            if (light > .92 && depth > 1.2) return WHITE;
            return light > .8 ? C[6] : light > .6 ? C[5] : light > .38 ? C[4] : light > .12 ? C[3] : C[2];
        },
        {3.6});
    L.outline(hex(0x12060c));
    L.draw(p);
    const double u = wrap(ph - LUB, 1);
    if (u < .28) {
        const double k = u / .28, r = 9 + 8 * (1 - std::pow(1 - k, 3));
        p.ring(0, cy, r, 1, k < .2 ? C[6] : C[5], .85 * (1 - k));
        p.glowRing(0, cy, r, 4, C[4], .25 * (1 - k));
    }
    const double g = wrap(ph - LUB - .08, .5);
    if (g < .12) pickup::glint(p, -3.5, cy - 4, 2.6 * std::sin(g / .12 * PI), WHITE, .45);
}
}  // namespace
const Descriptor& heartPickupDescriptor() {
    static const Descriptor d{{PixelFxKind::HeartPickup, "pickups/heart-pickup", 56, 60, 28, 52, D, true},
                              8,
                              [](PixelFxSettings& s) {
                                  s.seed    = 4;
                                  s.palette = {hex(0x1a0408), hex(0x4a0a14), hex(0x8a1222), hex(0xd02436),
                                               hex(0xff4a56), hex(0xff8a8e), hex(0xffd0cc), WHITE};
                              },
                              front,
                              {},
                              nullptr,
                              back};
    return d;
}
}  // namespace eve::stylize::pixelfx
