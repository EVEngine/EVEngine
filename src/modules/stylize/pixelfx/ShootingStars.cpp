// eve-hot-path
// Native adaptation of pixelfx weather/shooting-stars.js (MIT), including Sprite Fusion sparkle.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Descriptor.h"
#include "stylize/pixelfx/Weather.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double      D = 10, TRAIN = 1.6;
constexpr PixelFxInfo SOURCE{PixelFxKind::ShootingStars, "weather/shooting-stars", 320, 180, 160, 180, D, true};
struct Meteor {
    double t0, x, y, a, v, life, s;
    bool   flare;
};
constexpr std::array<Meteor, 7> METEORS{{{.5, 60, -170, 2.45, 260, .55, .6, false},
                                         {1.9, -40, -176, 2.2, 220, .7, .8, false},
                                         {3.4, 150, -150, 2.62, 300, .45, .5, false},
                                         {4.6, 130, -178, 2.35, 240, 1.05, 1.4, true},
                                         {6.7, -90, -168, 2.05, 200, .6, .7, false},
                                         {7.9, 20, -160, 2.5, 280, .5, .6, false},
                                         {9.2, -10, -176, 2.28, 230, .8, 1, true}}};
void                            sky(Paint& p, Paint& q, double t, uint32_t S, std::span<const uint32_t> K) {
    weather::rows(p, SOURCE, -180, -1, [&](double y, std::span<weather::RowPixel> row) {
        for (uint32_t i = 0; i < 320; ++i) {
            const double x = double(i) - 160 + .5, d = std::abs((y + 110) - (x * -.32)) / 36;
            if (d >= 1) continue;
            const double k = (1 - d) * (1 - d);
            auto&        r = row[i];
            r.l            = K[2];
            r.k            = float(.1 * k);
            if (rand({i, jsUint(std::floor(y)), S, 5}) < .05 * k) {
                r.c = K[2];
                r.a = float(.7);
            }
        }
    });
    for (uint32_t i = 0; i < 130; ++i) {
        const double x = -160 + rand({S, i, 1}) * 320, y = -180 + rand({S, i, 2}) * 178, r = rand({S, i, 3}),
                     c  = 1 + std::floor(rand({S, i, 4}) * 4),
                     tw = .5 + .5 * std::sin((TAU * c * t) / D + rand({S, i, 5}) * TAU);
        if (r < .6)
            q.dot(x, y, K[3], .35 + .5 * tw);
        else if (r < .94) {
            q.px(x, y, r < .8 ? K[4] : WHITE);
            q.glow(x + .5, y + .5, 3, K[3], .22 * tw);
        } else
            q.sparkle(x, y, 1 + jsRound(tw * 2), K[4], K[3], .3 * tw);
    }
}
void meteor(Paint& q, double age, const Meteor& m, uint32_t i, uint32_t S, std::span<const uint32_t> C) {
    const double ca = std::cos(m.a), sa = std::sin(m.a), end = m.life + (m.flare ? .5 : 0) + TRAIN;
    if (age > end) return;
    const auto   pos  = [&](double tt) -> Point { return {m.x + ca * m.v * tt, m.y + sa * m.v * tt}; };
    const double burn = std::min(age, m.life);
    const auto   h    = pos(burn);
    const double hx = h.x, hy = h.y, trainK = age < m.life ? 1 : 1 - (age - m.life) / (TRAIN + (m.flare ? .5 : 0));
    if (trainK > 0) {
        const double len = m.v * burn, nx = -sa, ny = ca;
        for (double d = 0; d < len; d += 1) {
            const double passed = age - d / m.v;
            if (passed < .08) continue;
            const double g  = trainK * std::min(1., (passed - .08) * 4) * (.35 + .65 * std::sin((PI * d) / len)) * m.s,
                         tw = std::sin(d * .09 + passed * 1.7) * std::min(4., passed * 3),
                         x = m.x + ca * d + nx * tw + passed * 4, y = m.y + sa * d + ny * tw;
            q.dot(x, y, passed < .6 ? C[2] : C[1], g * .75);
            if (std::fmod(d, 6) == 0) q.glow(x, y, 6 + passed * 4, C[1], .12 * g);
        }
    }
    if (age > m.life + (m.flare ? .5 : 0)) return;
    const double u = age / m.life, k = age < m.life ? std::min(1., u * 6) * (u > .75 ? 1 - (u - .75) / .25 : 1) : 0;
    if (k > 0) {
        const double                  L = (24 + 40 * m.s) * std::min(1., u * 3);
        const std::array<uint32_t, 5> ramp{{C[5], C[4], C[3], C[2], C[1]}};
        for (double d = 0; d <= L; d += .5) {
            const double v = d / L, x = hx - ca * d, y = hy - sa * d;
            q.dot(x, y, v < .08 ? WHITE : at(ramp, v * .95), k * (v < .6 ? 1 : (1 - v) / .4));
            if (m.s > .9 && v < .4) q.dot(x - sa * .9, y + ca * .9, v < .15 ? C[4] : C[3], k * (1 - v / .4));
        }
        q.px(hx, hy, WHITE);
        if (m.s > .9) {
            q.px(hx + 1, hy, WHITE);
            q.px(hx, hy + 1, C[4]);
        }
        q.glow(hx, hy, 8 + 10 * m.s, C[4], .45 * k);
        q.glow(hx - ca * L * .3, hy - sa * L * .3, 10 + 8 * m.s, C[3], .2 * k);
    }
    if (!m.flare || age < m.life * .82) return;
    const double f  = (age - m.life * .82) / (m.life * .18 + .5);
    const auto   fp = pos(m.life * .86);
    const double fx = fp.x, fy = fp.y;
    if (f < .35) {
        q.sparkle(fx, fy, 9 * std::sin((PI * f) / .35) + 2, C[4], C[3], .6);
        q.glow(fx, fy, 40, C[3], .5 * (1 - f / .35));
    }
    for (uint32_t j = 0; j < 7; ++j) {
        const double ang = rand({S, i, j, 5}) * TAU, r = 5 + rand({S, i, j, 6}) * 16, d0 = rand({S, i, j, 7}) * .3,
                     g = (f - d0) / .45;
        if (g <= 0 || g >= 1) continue;
        const double sx = fx + std::cos(ang) * r * (.6 + g * .5), sy = fy + std::sin(ang) * r * (.6 + g * .5) - g * 3,
                     arm = jsRound(2.5 * std::sin(PI * g));
        if (arm >= 1)
            q.sparkle(sx, sy, arm, j % 3 ? C[4] : WHITE, C[3], .3 * (1 - g));
        else
            q.px(sx, sy, C[4]);
    }
    for (uint32_t j = 0; j < 4; ++j) {
        const double da = (rand({S, i, j, 1}) - .5) * .5, sp = m.v * (.5 + rand({S, i, j, 2}) * .4),
                     tt = f * (m.life * .18 + .5), g = 1 - f, x = fx + std::cos(m.a + da) * sp * tt,
                     y = fy + std::sin(m.a + da) * sp * tt;
        if (g <= 0) continue;
        q.line(x - std::cos(m.a + da) * 5 * g, y - std::sin(m.a + da) * 5 * g, x, y, C[3], g);
        q.px(x, y, g > .5 ? WHITE : C[4]);
        q.glow(x, y, 5, C[3], .35 * g);
    }
}
void draw(Paint& p, double t, const PixelFxSettings& o) {
    auto q = weather::clip(p, SOURCE);
    sky(p, q, t, o.seed, o.weather.sky);
    for (uint32_t i = 0; i < METEORS.size(); ++i)
        meteor(q, wrap(t - METEORS[i].t0, D), METEORS[i], i, o.seed, o.palette);
}
}  // namespace
const Descriptor& shootingStarsDescriptor() {
    static const std::array<Option, 1> options{{{"sky", 1, 64, false, 5, &PixelFxWeatherSettings::sky}}};
    static const Descriptor            d{
        SOURCE, 6,
        [](PixelFxSettings& s) {
            s.seed        = 4;
            s.palette     = {hex(0x3a1e5a), hex(0x7a3a8a), hex(0xe0607a), hex(0xffa86a), hex(0xffe6a8), WHITE};
            s.weather.sky = {hex(0x141428), hex(0x2a2a4a), hex(0x4c4c7c), hex(0x8e8ec4), hex(0xdcdcff)};
        },
        draw, options};
    return d;
}
}  // namespace eve::stylize::pixelfx
