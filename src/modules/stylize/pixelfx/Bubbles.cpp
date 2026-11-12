// eve-hot-path
// Native adaptation of pixelfx water/bubbles.js (MIT), including Sprite Fusion bubble shading.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Descriptor.h"
#include "stylize/pixelfx/PixelFxInternal.h"
#include "stylize/pixelfx/Water.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double                D = 4, TOP = -92, HALF = 48;
constexpr std::array<double, 3> MOUTHS{{-5, 1, 6}};
double                          surfaceY(double x, double ph) {
    return TOP + std::sin(x * .13 + ph * TAU * 2) * 1.1 + std::sin(x * .051 - ph * TAU) * 1.3;
}
double shaft(double x, double y, double ph) {
    const double u = x + (y - TOP) * .32, s = std::sin(u * .16 + ph * TAU) * .6 + std::sin(u * .07 - ph * TAU + 1) * .4;
    return std::max(0., s - .35) / .65;
}
void bubble(Paint& p, std::span<const uint32_t> C, double x, double y, double r, double q, double lit) {
    if (r < .9) {
        p.px(x, y, lit > .3 ? WHITE : r < .7 ? C[5] : C[6]);
        return;
    }
    if (r < 1.5) {
        const double X = std::floor(x), Y = std::floor(y);
        p.px(X, Y, WHITE);
        p.px(X + 1, Y, C[6]);
        p.px(X, Y + 1, C[5]);
        p.px(X + 1, Y + 1, C[4]);
        p.glow(x + .5, y + .5, 3, C[5], .12 + .2 * lit);
        return;
    }
    const double rx = r * (1 + q), ry = r * (1 - q);
    p.shader(x - rx - 1, y - ry - 1, x + rx + 1, y + ry + 1, [&](double px, double py, ShadeOutput& out) {
        const double dx = (px - x) / rx, dy = (py - y) / ry, d = jsHypot2(dx, dy) * r;
        if (d > r + .5) return;
        const double l = (-dx - dy) / std::numbers::sqrt2 / std::max(.01, d / r);
        if (d > r - .6)
            out.c = l > .45 ? C[6] : l < -.55 ? C[3] : C[4];
        else
            p.blend(px, py, C[5], .16 + .2 * lit);
    });
    p.px(jsRound(x - rx * .45), jsRound(y - ry * .45), WHITE);
    if (r >= 2.5) p.px(jsRound(x - rx * .45) + 1, jsRound(y - ry * .45), C[6]);
    if (r >= 2) p.px(jsRound(x + rx * .5), jsRound(y + ry * .5), C[6]);
    p.glow(x, y, r * 2.4 + 2, C[5], .12 + .3 * lit);
}
struct Bubble {
    double x, y, r, q, vy;
};
template <class F>
void each(double t, uint32_t seed, F fn) {
    cycle(t, D, 46, seed, 2.2, 3.4, [&](double age, double, Rng& rng, int i) {
        const double m = MOUTHS[size_t(i) % 3], choice = rng.next(),
                     r0 = choice < .7 ? rng.range(.5, 1.4) : rng.range(1.6, 2.6), vy = 30 + 9 * r0 + rng.range(-4, 4),
                     wf = rng.range(3, 6), wa = .8 + r0 * 1.2, ph0 = rng.range(0, TAU), drift = rng.range(-4, 4),
                     y = -2 - vy * age, k = clamp(-y / -TOP), x = m + drift * k + std::sin(age * wf + ph0) * wa * k;
        fn(Bubble{x, y, r0 * (1 + .45 * k), .12 * std::sin(age * wf * 2 + ph0) * std::min(1., r0 / 2), vy});
    });
}
void back(Paint& p, double t, const PixelFxSettings& o) {
    const auto&  C  = o.palette;
    const double ph = t / D;
    for (int Y = int(TOP) - 2; Y < 0; ++Y)
        for (int X = -int(HALF); X < HALF; ++X) {
            const double x = X + .5, y = Y + .5, sy = surfaceY(x, ph);
            if (y < sy) continue;
            const double e = 1 - smooth(HALF - 14, HALF, std::abs(x)), depth = (y - TOP) / -TOP;
            p.blend(x, y, C[1], (.22 + .22 * depth) * e);
            const double s = shaft(x, y, ph) * (1 - depth * .7) * e;
            if (s > .05) p.light(x, y, C[4], .2 * s);
        }
    for (int X = -int(HALF); X < HALF; ++X) {
        const double x = X + .5, e = 1 - smooth(HALF - 14, HALF, std::abs(x)), s = shaft(x, -2, ph),
                     c = std::max(0., 1 - std::abs(std::sin(x * .42 + std::sin(x * .11 + ph * TAU * 2) * 2.2)) * 2.4);
        if (s * c * e > .05) {
            p.light(x, -1, C[5], .5 * s * c * e);
            p.light(x, -2, C[4], .25 * s * c * e);
            p.light(x, 0, C[4], .3 * s * c * e);
        }
    }
    const auto&                                    ST = water::rock();
    constexpr std::array<std::array<double, 3>, 6> pebbles{
        {{-9, -1.5, 2.4}, {-4, -1, 1.8}, {3.5, -1.2, 2.2}, {9, -1.4, 2.6}, {0, -.6, 1.4}, {-12.5, -.8, 1.6}}};
    for (const auto& v : pebbles) {
        const auto [x, y, rr] = v;
        p.shader(x - rr - 1, y - rr - 1, x + rr + 1, y + rr + 1, [&](double px, double py, ShadeOutput& out) {
            const double dx = px - x, dy = py - y, d = jsHypot2(dx * 1.1, dy);
            if (d > rr + .7 || py > 0) return;
            out.c = d > rr ? ST[0] : dx + dy < -rr * .6 ? ST[5] : dx + dy < rr * .3 ? ST[3] : ST[2];
        });
    }
    for (const double m : MOUTHS) p.px(m, -1, ST[0]);
}
void front(Paint& p, double t, const PixelFxSettings& o) {
    const auto&  C  = o.palette;
    const double ph = t / D;
    cycle(t, D, 26, o.seed + 7, D, D, [&](double age, double, Rng& rng, int) {
        const double xx = rng.range(-HALF + 6, HALF - 6), angle = rng.range(0, TAU),
                     x = xx + std::sin((age / D) * TAU + angle) * 3,
                     y = TOP + 6 + std::fmod(rng.range(0, 86) + age * 4, 86), s = shaft(x, y, ph),
                     tw = std::sin((age / D) * TAU * 3 + rng.range(0, TAU));
        if (tw < -.3 && s < .2) return;
        p.px(x, y, s > .4 ? WHITE : tw > .6 ? C[5] : C[4]);
        if (s > .4) p.glow(x, y, 3, C[5], .25 * s);
    });
    each(t, o.seed, [&](const Bubble& b) {
        const double sy = surfaceY(b.x, ph);
        if (b.y > sy + b.r)
            bubble(p, C, b.x, b.y, b.r, b.q, shaft(b.x, b.y, ph));
        else {
            const double since = (sy + b.r - b.y) / b.vy;
            if (since < .12 && b.r >= 1.4) {
                for (double a = PI; a <= TAU + .01; a += .4 / b.r)
                    p.px(b.x + std::cos(a) * b.r, sy + std::sin(a) * b.r * .9, C[6]);
                p.px(b.x - b.r * .4, sy - b.r * .6, WHITE);
            } else if (since < (b.r >= 1.4 ? .32 : .2)) {
                const double k = clamp((since - (b.r >= 1.4 ? .12 : 0)) / .2), w = std::max(1., b.r + 1 + k * 4);
                for (double a = 0; a < PI; a += .5 / w)
                    if (std::sin(a) > .15)
                        p.dot(b.x + std::cos(a) * w, sy + std::sin(a) * w * .3, k < .4 ? WHITE : C[5], 1.2 - k);
                if (b.r >= 1.4 && k < .6) {
                    p.px(b.x - 2, sy - 2 - k * 4, C[6]);
                    p.px(b.x + 2, sy - 3 - k * 5, C[6]);
                }
            }
        }
    });
    for (int X = -int(HALF); X < HALF; ++X) {
        const double x = X + .5, y = surfaceY(x, ph), e = 1 - smooth(HALF - 12, HALF, std::abs(x));
        const bool   glint = std::sin(x * .37 - ph * TAU * 3) * std::sin(x * .13 + ph * TAU) > .55;
        p.dot(X, y, glint ? WHITE : C[5], e * 1.3);
        p.dot(X, y + 1, C[3], e * 1.3);
        if ((X & 3) == 0) p.glow(x, y + 1, 7, C[5], (glint ? .22 : .1) * e);
    }
}
}  // namespace
const Descriptor& bubblesDescriptor() {
    static const Descriptor d{{PixelFxKind::Bubbles, "water/bubbles", 104, 112, 52, 104, D, true},
                              8,
                              [](PixelFxSettings& s) { water::defaults(s, 4); },
                              front,
                              {},
                              nullptr,
                              back};
    return d;
}
}  // namespace eve::stylize::pixelfx
