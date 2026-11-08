// eve-hot-path
// Native adaptation of pixelfx lightning/ball-lightning.js (MIT).
// Copyright (c) 2026 Patch and Hugo Duprez; see NOTICE.txt.
#include "stylize/pixelfx/Descriptor.h"
#include "stylize/pixelfx/Lightning.h"
#include "stylize/pixelfx/PixelFxInternal.h"
namespace eve::stylize::pixelfx {
namespace {
Point at(double t) {
    const double a = (t / 4) * TAU;
    return {38 * std::sin(a) + 9 * std::sin(2 * a + 1), -42 + 13 * std::sin(2 * a + .4) + 6 * std::sin(3 * a + .5)};
}
void draw(Paint& p, double t, const PixelFxSettings& o) {
    const auto&    C = o.palette;
    const auto&    S = o.lightning.sparks;
    const uint32_t f = jsUint(wrap(std::floor(t * 30 + 1e-6), 120));
    Rng            r(hash({o.seed, 1, f}));
    const auto     v = at(t);
    const double   x = v.x, y = v.y;
    p.glow(x, -2, 30, C[3], .12 + .3 * clamp((y + 60) / 40));
    for (int i = 14; i >= 1; --i) {
        const auto   w = at(t - i * .034);
        const double u = i / 14., rr = 3 * (1 - u) + .3;
        if (u > .6 && (uint32_t(i) + f) % 2) continue;
        p.circle(w.x, w.y, rr + .8, u < .35 ? C[3] : C[2]);
        p.circle(w.x, w.y, rr, u < .35 ? C[6] : u < .7 ? C[5] : C[4]);
        p.glow(w.x, w.y, 7 * (1 - u) + 3, C[4], .25 * (1 - u));
    }
    cycle(t, 4, 40, o.seed + 5, .5, 1, [&](double age, double life, Rng& q, int) {
        const auto   b = at(t - age);
        const double k = 1 - age / life, a = q.range(0, TAU), sp = q.range(4, 14),
                     mx = b.x + std::cos(a) * sp * age + q.range(-3, 3),
                     my = b.y + std::sin(a) * sp * age + 6 * age + q.range(-3, 3);
        if (k > .25 || f % 2) p.px(mx, my, k > .6 ? C[6] : k > .3 ? C[4] : C[3]);
        p.glow(mx, my, 3, C[4], .3 * k);
    });
    const double R0 = 5.2 + .6 * std::sin(t * 9) + .4 * r.range(-1, 1);
    p.circle(x, y, R0 + 1.3, C[2]);
    p.circle(x, y, R0, C[4]);
    p.circle(x - .4, y - .4, R0 * .72, C[5]);
    for (int i = 0; i < 3; ++i) {
        const double a = t * (5 + i * 2.3) + i * 2.1, d = R0 * .6;
        p.circle(x + std::cos(a) * d, y + std::sin(a) * d * .8, 1.2, i ? C[6] : WHITE);
    }
    p.circle(x - .6, y - .6, R0 * .32, WHITE);
    for (int i = 0; i < 8; ++i) {
        if (r.next() < .35) continue;
        const double a = (i / 8.) * TAU + t * 2 + r.range(-.2, .2), l = r.range(1.5, 4);
        p.line(x + std::cos(a) * (R0 + 1), y + std::sin(a) * (R0 + 1), x + std::cos(a) * (R0 + 1 + l),
               y + std::sin(a) * (R0 + 1 + l), i % 2 ? C[5] : C[4]);
    }
    p.glow(x, y, 30, C[4], .3);
    p.glow(x, y, 9, C[6], .1);
    for (int i = 0; i < 2 + (r.next() < .4 ? 1 : 0); ++i) {
        const double a = r.range(0, TAU), l = r.range(5, 11), x0 = x + std::cos(a) * (R0 + 1),
                     y0 = y + std::sin(a) * (R0 + 1), b = a + r.range(-.7, .7);
        auto         path = lightning::zigzag(r, x0, y0, x0 + std::cos(b) * l, y0 + std::sin(b) * l, .4, .3);
        const double k    = r.range(.4, .7);
        lightning::crackle(p, path, k, C, .25);
    }
    const int    za = int(f) - 62;
    Rng          ground(hash({o.seed, 3}));
    const double gx = at(62. / 30).x + ground.range(-10, 10);
    if (za >= 0 && za < 4) {
        const double      k = std::array<double, 4>{{1, .6, .9, .4}}[size_t(za)];
        Rng               q(hash({o.seed, 4, f}));
        const auto        paths = lightning::zigzag(q, x, y + R0, gx, 0, .28, .4);
        lightning::Stroke opts;
        opts.hero = za == 0;
        lightning::stroke(p, paths, k, C, .7, opts);
        p.circle(gx, -1, 2.5 * k + 1, C[5]);
        p.px(gx, -1, WHITE);
        p.glow(gx, -4, 34, C[4], .6 * k);
    }
    const double sa = wrap(t - 62. / 30, 4);
    for (uint32_t j = 0; j < 14; ++j) {
        Rng          q(hash({o.seed, 7, j}));
        const double life = q.range(.25, .55);
        if (sa >= life) continue;
        const double a = -PI / 2 + q.range(-1.3, 1.3), sp = q.range(50, 170);
        lightning::spark(p, lightning::flight(gx, -1, std::cos(a) * sp, std::sin(a) * sp, sa, 0), 1 - sa / life, S);
    }
}
}  // namespace
const Descriptor& ballLightningDescriptor() {
    static const std::array<Option, 1> options{{{"sparks", 1, 64, false, 4, &PixelFxLightningSettings::sparks}}};
    static const Descriptor d{{PixelFxKind::BallLightning, "lightning/ball-lightning", 136, 104, 68, 92, 4, true},
                              8,
                              [](PixelFxSettings& s) {
                                  s.seed = 9;
                                  s.palette.assign(lightning::VOLT.begin(), lightning::VOLT.end());
                                  s.lightning.sparks.assign(lightning::PLASMA.begin(), lightning::PLASMA.end());
                              },
                              draw,
                              options};
    return d;
}
}  // namespace eve::stylize::pixelfx
