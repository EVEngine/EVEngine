// eve-hot-path
// Native adaptation of pixelfx nature/toxic-puddle.js, MIT.
// Copyright (c) 2026 Patch; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/PixelFxInternal.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double D = 4;
double           surf(double x, double W, double t) {
    const double d = std::min(1., std::abs(x) / W), ph = (TAU * t) / D;
    const double lens  = std::sqrt(1 - d * d) * 6.6 + (d > .8 ? 1.6 * smooth(.8, .9, d) : 0),
                 swell = std::pow(std::max(0., std::sin(x * .09 - ph)), 6) * 1.2 +
                         std::pow(std::max(0., std::sin(x * .06 + ph * 2 + 1)), 8) * .9;
    return -jsRound(lens + swell * (1 - d));
}
template <class Painter>
void pool(Painter& p, std::span<const uint32_t> C, std::span<const uint32_t> Sh, double W, double t, uint32_t seed) {
    const double ph = t / D;
    for (int x = int(std::floor(-W - 1)); x <= std::ceil(W + 1); ++x) {
        const double d = std::abs(x + .5) / W;
        if (d > 1.03) continue;
        const double top = surf(x + .5, W, t),
                     oil = .6 * tileNoise(x * .08, ph * 4, 0, 4, seed) +
                           .6 * tileNoise(x * .11 - ph * 4, 1, 4, 0, seed + 1u),
                     hue = tileNoise(x * .05, ph * 2 + 7, 0, 2, seed + 3u);
        for (double y = top; y <= 2; ++y) {
            if ((y == 2 && d > .75) || (y == 1 && d > .9)) continue;
            uint32_t c;
            if (d > .93 && y <= 0)
                c = y == top ? C[7] : C[6];
            else if (y == top) {
                const bool glint = std::abs(wrap(x * .07 - ph * 3, 1) - .5) < .012 ||
                                   rand({jsUint(x), jsUint(std::floor(t * 10))}) < .015;
                c = glint ? WHITE : oil > .55 ? (hue > 0 ? Sh[3] : Sh[2]) : oil > .15 ? (hue > 0 ? Sh[1] : C[5]) : C[5];
                if (glint) p.light(x, y, C[7], .5);
            } else if (y == top + 1)
                c = oil > .4 ? Sh[0] : C[3];
            else if (y >= 1)
                c = C[0];
            else {
                const double pulse = .5 + .5 * std::sin((TAU * t) / D + x * .03),
                             v     = std::abs(tileNoise(x * .09, y * .75 + ph * 4, 0, 4, seed + 7u) +
                                              .2 * std::sin((TAU * t * 2) / D + x * .15)),
                             w     = .6 + .6 * pulse;
                c                  = v < .035 * w ? C[7] : v < .08 * w ? C[6] : v < .14 * w ? C[4] : C[1];
                if (v < .08 * w) p.light(x, y, C[6], .25 + .3 * pulse);
            }
            p.px(x, y, c);
        }
        if (d > .9 && d <= 1.03) p.px(x, top - 1, C[1]);
    }
    for (int s : {-1, 1})
        for (int k = 1; k <= 6; ++k) p.dot(s * (W + k), 0, k < 3 ? C[6] : C[5], 1 - k / 7.);
}
template <class Painter>
void ribbon(Painter& p, std::span<const uint32_t> C, double x0, double y0, double age, double life, Rng& R, double k) {
    const double u = age / life, len = R.range(16, 26), rise = R.range(9, 14), amp = R.range(2, 4.5),
                 w = R.range(.18, .3), ph = R.range(0, TAU);
    const double head = rise * age, tail = std::max(0., head - len), fray = smooth(.35, .85, u),
                 fade = smooth(0, .12, u) * (1 - smooth(.5, 1, u)) * k;
    if (fade < .02) return;
    const uint32_t GREY = hex(0xb4b8ae);
    const int      n    = fray > .05 ? 3 : 1;
    for (double h = tail; h <= head; h += .5) {
        const double   s  = (h - tail) / std::max(1., head - tail),
                       cx = x0 + amp * std::sin(h * w + ph + age * 1.6) * (.3 + s), y = y0 - h,
                       a   = fade * std::sin(PI * s) * .45;
        const double   lit = std::max(0., 1 - h / 22) * (1 - .6 * fray);
        const uint32_t VAP = mix(GREY, C[6], .75 * lit);
        for (int q = 0; q < n; ++q) {
            const double off =
                n == 1 ? 0
                       : fray * s * (q - 1) * 3 + fray * 2.5 * std::sin(h * (.35 + q * .17) + ph * (q + 1) + age * 2);
            p.blend(cx + off, y, VAP, a * (n == 1 ? 1 : .65 - .25 * fray) * (.8 + .4 * lit));
        }
    }
}
template <class Painter>
void bubbles(Painter& p, std::span<const uint32_t> C, std::span<const uint32_t> Sh, double W, double t, uint32_t seed) {
    cycle(t, D, 8, seed, 2.4, 3.2, [&](double age, double L, Rng& R, int) {
        const double swell = R.range(1, 1.6), x = R.range(-.7, .7) * W, rmax = R.range(4.5, 8), sy = surf(x, W, t) + .5;
        if (age < swell) {
            const double k = age / swell, st = smooth(.65, 1, k), gr = ease::outCubic(std::min(1., k * 1.3)),
                         rx = rmax * gr * (1 - .18 * st), ry = rmax * gr * (1 + .5 * st);
            const double X = jsRound(x), Y = jsRound(sy), inner = .35 + .65 * k;
            for (int j = -int(std::ceil(ry)) - 1; j <= 0; ++j)
                for (int i = -int(std::ceil(rx)) - 1; i <= std::ceil(rx) + 1; ++i) {
                    const double dd = jsHypot2(i / std::max(.6, rx), j / std::max(.6, ry));
                    if (dd > 1.1) continue;
                    uint32_t c;
                    if (dd > .9)
                        c = C[0];
                    else if (dd < .55 * inner && j > -ry * .75)
                        c = dd < .3 * inner ? C[6] : C[5];
                    else if (dd < .8 * inner)
                        c = C[4];
                    else
                        c = j < -ry * .5 ? C[2] : C[3];
                    p.px(X + i, Y + j, c);
                }
            if (rx > 1.8)
                for (double a = 3.5; a < 4.7; a += .16)
                    p.px(X + std::cos(a) * rx * .68, Y + std::sin(a) * ry * .68,
                         a < 3.9   ? Sh[3]
                         : a < 4.3 ? C[7]
                                   : Sh[1]);
            if (rx > 1.2) {
                p.px(X - jsRound(rx * .35), Y - jsRound(ry * .62), WHITE);
                p.px(X - jsRound(rx * .35) + 1, Y - jsRound(ry * .62), C[7]);
            }
            p.glow(x, sy - ry * .5, 7 + rx * 1.4, C[6], (.2 + .35 * st) * gr);
            return;
        }
        const double e = age - swell;
        if (e < .3) {
            const double k = e / .3, rr = 2 + 14 * ease::outCubic(k);
            for (double a = 0; a < TAU; a += .5 / rr) {
                const double rx2 = x + std::cos(a) * rr, ry2 = sy + std::sin(a) * rr * .22;
                if (std::abs(rx2) < W * .95) p.dot(rx2, ry2, k < .4 ? WHITE : k < .7 ? C[7] : C[6], (1 - k) * 1.4);
            }
            p.glowRing(x, sy, rr, 3, C[6], .35 * (1 - k));
            if (e < .1) p.glow(x, sy - 2, 18, C[6], .7 * (1 - e / .1));
        }
        for (uint32_t j = 0; j < 7; ++j) {
            const double vx  = (rand({j, seed, jsUint(jsRound(x * 10))}) - .5) * 80,
                         vy  = -45 - rand({j, seed + 3u, jsUint(jsRound(x * 10))}) * 70;
            const auto   pos = fly(x, sy - 2, vx, vy, e, 420);
            if (pos.y < sy && e < .6) {
                p.px(pos.x, pos.y, C[1]);
                p.px(pos.x, pos.y - 1, j & 1u ? Sh[3] : C[6]);
            }
        }
        ribbon(p, C, x, sy - 2, e, L - swell, R, .9);
    });
}
struct ToxicPuddle final : AnalyticKernel {
    template <class Painter>
    void paintBack(Painter& p, double t, const PixelFxSettings& o) const {
        const auto&  C = o.palette;
        const double W = o.nature.width, pulse = .85 + .15 * std::sin((TAU * t * 2) / D);
        p.glow(0, -3, W * 1.25, C[5], .22 * pulse);
        p.glow(0, -10, W * 1.1, C[6], .1 * pulse);
        for (int s : {-1, 1}) p.glow(s * W, -1, 12, C[6], .35 * pulse);
        pool(p, C, o.nature.sheen, W, t, o.seed);
    }
    template <class Painter>
    void paintFront(Painter& p, double t, const PixelFxSettings& o) const {
        const double W = o.nature.width;
        bubbles(p, o.palette, o.nature.sheen, W, t, o.seed * 7u + 1u);
        cycle(t, D, 9, o.seed * 13u + 4u, 2.4, 3.4, [&](double age, double L, Rng& R, int) {
            const double x = R.range(-.85, .85) * W;
            ribbon(p, o.palette, x, surf(x, W, t) - 1, age, L, R, .75);
        });
    }
    void drawBack(Paint& p, double t, const PixelFxSettings& o) const override { paintBack(p, t, o); }
    void draw(Paint& p, double t, const PixelFxSettings& o) const override { paintFront(p, t, o); }
};
}  // namespace
const Descriptor& toxicPuddleDescriptor() {
    static const std::array<Option, 2> options{{{"sheen", 1, 64, false, 4, &PixelFxNatureSettings::sheen},
                                                {"width", 1, 256, false, 0, &PixelFxNatureSettings::width}}};
    static const Descriptor            d{{PixelFxKind::ToxicPuddle, "nature/toxic-puddle", 176, 100, 88, 86, D, true},
                                         8,
                                         [](PixelFxSettings& s) {
                                  s.seed         = 2;
                                  s.nature.width = 42;
                                  s.palette      = {hex(0x06070a), hex(0x0c100c), hex(0x141a10), hex(0x1e2814),
                                                    hex(0x2e3c18), hex(0x5a7a1c), hex(0xa6d42a), hex(0xecff86)};
                                  s.nature.sheen = {hex(0x2a1648), hex(0x5a2e8e), hex(0x1e7a86), hex(0x6adccc)};
                                         },
                                         nullptr,
                                         options,
                                         nullptr,
                                         nullptr,
                                         [](const PixelFxSettings&) -> std::unique_ptr<const AnalyticKernel> {
                                  return std::make_unique<ToxicPuddle>();
                                         }};
    return d;
}
}  // namespace eve::stylize::pixelfx
