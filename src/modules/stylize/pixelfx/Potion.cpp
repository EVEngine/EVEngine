// eve-hot-path
// Native adaptation of pixelfx pickups/potion.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Descriptor.h"
#include "stylize/pixelfx/Pickup.h"
#include "stylize/pixelfx/PixelFxInternal.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double              D = 2.4, RB = 7, HOVER = 17, NECK = 2, LIP = -12, CORK_TOP = -16;
const std::array<uint32_t, 5> CORK{hex(0x2a160c), hex(0x4e2c16), hex(0x7a4a26), hex(0xa8703a), hex(0xd09a5a)};
double                        surface(double x, double ph) {
    return -.5 + .17 * std::sin(ph * TAU - .9) * x + .45 * std::sin(x * .85 + ph * TAU * 3);
}
void flask(Paint& p, std::span<const uint32_t> C, double t, double ph, double cy, double tilt, uint32_t seed) {
    const double          ct = std::cos(tilt), st = std::sin(tilt);
    const auto            glass = hex(0x2a2236);
    pickup::Layer<40, 44> L;
    L.begin(-20, std::floor(cy) - 22);
    L.fill(-20, std::floor(cy) - 22, 19, std::floor(cy) + 12, [&](double px, double py) -> uint32_t {
        const double wx = px, wy = py - cy, fx = wx * ct + wy * st, fy = -wx * st + wy * ct, d = jsHypot2(fx, fy);
        if (fy < LIP - .5 && fy >= CORK_TOP && std::abs(fx) <= NECK + .2)
            return CORK[fy < CORK_TOP + 1 ? 4 : fx < -.6 ? 3 : fx > 1 ? 1 : 2];
        if (fy >= LIP - .5 && fy < LIP + 1 && std::abs(fx) <= NECK + 1.4)
            return fx < 0 ? mix(WHITE, C[6], .4) : mix(glass, C[5], .45);
        const bool inNeck = fy >= LIP + 1 && fy < -RB + 2 && std::abs(fx) <= NECK + .6;
        if (d > RB && !inNeck) return 0;
        const bool   wall = inNeck ? std::abs(fx) > NECK - .5 : d > RB - 1;
        const double s = surface(wx, ph), depth = wy - s;
        if (!wall && !inNeck && depth > 0) {
            const double core = 1 - jsHypot2(wx * .85, wy - 1.8) / RB;
            if (depth < .9) return wx < -1.5 && wx > -4.5 ? WHITE : C[6];
            int k = core > .62 ? 5 : core > .38 ? 4 : core > .18 ? 3 : 2;
            if (depth < 1.9 && k < 5) ++k;
            if (wx > 1.5 && wy > 2) --k;
            return C[size_t(std::clamp(k, 2, 6))];
        }
        if (wall) {
            const double lit = (-fx * .75 - fy * .66) / std::max(1.0, d);
            if (inNeck) return fx < 0 ? mix(glass, C[6], .55) : mix(glass, C[5], .3);
            if (lit > .82) return mix(WHITE, C[6], .3);
            if (lit > .3) return mix(glass, C[6], .5);
            return depth > 0 ? C[3] : mix(glass, C[5], .28);
        }
        return glass;
    });
    L.outline(hex(0x120d1a));
    L.draw(p);
    for (double a = 3.45; a < 4.35; a += .04) {
        const double lx = std::cos(a + tilt) * (RB - 2.2), ly = std::sin(a + tilt) * (RB - 2.2);
        p.px(lx, cy + ly, a > 3.7 && a < 4.1 ? WHITE : mix(WHITE, C[6], .5));
    }
    p.px(jsRound(2 * std::cos(tilt) + 3 * std::sin(tilt)), jsRound(cy - 3), WHITE);
    cycle(t, D, 10, seed, .7, 1.3, [&](double age, double life, Rng& r, int) {
        const double x0 = r.range(-4, 4), r0 = r.range(.6, 1.7), vy = r.range(7, 13), w = r.range(5, 9),
                     phase = r.range(0, TAU), x = x0 + std::sin(age * w + phase) * .7, y = cy + 5.5 - vy * age,
                     ph2 = (t + age) / D, top = cy + surface(x, ph2);
        if (y > top + .8) {
            const double rr = r0 * (.7 + .5 * (age / life));
            if (jsHypot2(x, y - cy) > RB - 1.5) return;
            if (rr < .95)
                p.px(x, y, C[6]);
            else {
                p.px(x, y, C[3]);
                p.px(x + 1, y, C[6]);
                p.px(x, y - 1, WHITE);
                p.px(x + 1, y - 1, C[6]);
                if (rr > 1.4) {
                    p.px(x - 1, y, C[5]);
                    p.px(x, y + 1, C[4]);
                }
            }
            p.glow(x + .5, y, 3, C[6], .15);
        } else {
            const double k = clamp((top + .8 - y) / 3);
            if (k >= 1) return;
            for (double a = PI; a < TAU; a += .6)
                p.dot(x + std::cos(a) * (1 + k * 2), top - .5 + std::sin(a) * (1 + k * 2) * .5, k < .5 ? WHITE : C[6],
                      1.2 - k);
            p.dot(x - 1, top - 1.5 - k * 3, C[6], 1 - k);
            p.dot(x + 1.5, top - 2 - k * 4, WHITE, 1 - k);
        }
    });
}
void back(Paint& p, double t, const PixelFxSettings& o) {
    const auto&  C  = o.palette;
    const double ph = t / D, y = -HOVER - 1.4 * std::sin(ph * TAU);
    pickup::blobShadow(p, 0, 7.5, .8 - (-y - HOVER + 1.4) * .06);
    pickup::pool(p, 0, .5, 18, 3, C[5], .3);
    cycle(t, D, 7, o.seed + 1, 1.2, 1.8, [&](double age, double life, Rng& r, int) {
        const int    side = r.sign();
        const double f = r.range(3, 4.5), phase = r.range(0, TAU), vy = r.range(11, 16), x0 = r.range(-1, 1);
        for (int i = 0; i < 9; ++i) {
            const double a = age - i * .04;
            if (a < 0) break;
            const double x = x0 + std::sin(a * f + phase) * (.8 + a * 3.2) * side, yy = y + CORK_TOP - 1 - a * vy,
                         k = (1 - a / life) * std::min(1.0, a / .15) * (1 - double(i) / 9);
            p.dot(x, yy, i == 0 ? WHITE : i < 3 ? C[6] : C[5], k * 1.4);
            if (i == 0) p.glow(x, yy, 4, C[5], .3 * k);
        }
    });
}
void front(Paint& p, double t, const PixelFxSettings& o) {
    const auto&  C  = o.palette;
    const double ph = t / D, y = -HOVER - 1.4 * std::sin(ph * TAU), tilt = .13 * std::sin(ph * TAU + .4);
    p.glow(0, y + 1.5, 26, C[4], .22);
    flask(p, C, t, ph, y, tilt, o.seed);
    p.glow(0, y + 2, 10, C[5], .38 + .08 * std::sin(ph * TAU * 2));
    const double g = std::fmod(ph + .2, .5) / .5;
    if (g < .16) pickup::glint(p, -4, y - 4, 2.6 * std::sin(g / .16 * PI), WHITE, .4);
}
}  // namespace
const Descriptor& potionDescriptor() {
    static const Descriptor d{{PixelFxKind::Potion, "pickups/potion", 56, 72, 28, 64, D, true},
                              8,
                              [](PixelFxSettings& s) {
                                  s.seed    = 5;
                                  s.palette = {hex(0x1a0516), hex(0x3d0a33), hex(0x701560), hex(0xb0228f),
                                               hex(0xf53cd1), hex(0xff86e6), hex(0xffd2f6), WHITE};
                              },
                              front,
                              {},
                              nullptr,
                              back};
    return d;
}
}  // namespace eve::stylize::pixelfx
