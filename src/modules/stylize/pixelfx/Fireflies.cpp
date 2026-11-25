// eve-hot-path
// Native adaptation of pixelfx weather/fireflies.js (MIT), following Sprite Fusion fireflies.
// Copyright (c) 2026 Patch and Hugo Duprez; see NOTICE.txt.
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Weather.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double      D = 12;
constexpr PixelFxInfo SOURCE{PixelFxKind::Fireflies, "weather/fireflies", 320, 180, 160, 170, D, true};
double                cyc(double t, double n, double ph = 0) { return (TAU * n * t) / D + ph; }
struct Fly {
    uint32_t seed, index;
    int      depth;
    double   glow;
    double   r(uint32_t n) const { return rand({seed, index, n}); }
    Point    pos(double t) const {
        const double k  = depth == 0 ? .5 : depth == 1 ? .8 : 1;
        const double cx = -150 + r(1) * 300, cy = -16 - r(2) * (depth == 2 ? 70 : 130);
        const double ax = (14 + r(3) * 26) * k, ay = (8 + r(4) * 14) * k;
        const double n1 = 1 + std::floor(r(5) * 2), n2 = 1 + std::floor(r(6) * 3);
        return {cx + ax * std::sin(cyc(t, n1, r(7) * TAU)) + 10 * k * loopNoise(t / D, 1.5, index * 3),
                cy + ay * std::sin(cyc(t, n2, r(8) * TAU)) + 6 * k * loopNoise(t / D, 1.5, index * 3 + 1)};
    }
};
std::array<Fly, 32> flies(uint32_t seed, double t) {
    std::array<Fly, 32> result{};
    for (uint32_t i = 0; i < result.size(); ++i) {
        auto& f          = result[i];
        f                = {seed, i, i < 14 ? 0 : i < 26 ? 1 : 2, 0};
        const double per = D / (3 + std::floor(f.r(9) * 4)), u = wrap(t / per + f.r(10), 1);
        const auto pulse = [](double v) { return v < .06 ? v / .06 : v < .14 ? 1 : v < .4 ? 1 - (v - .14) / .26 : 0; };
        f.glow           = std::max(pulse(u), f.r(11) < .35 ? pulse(u - .22) * .8 : 0);
    }
    return result;
}
void firefly(Paint& q, double t, const Fly& f, std::span<const uint32_t> C) {
    const auto   p = f.pos(t);
    const double x = p.x, y = p.y, g = f.glow, X = jsRound(x), Y = jsRound(y);
    if (f.depth == 0) {
        if (g < .08) return;
        q.dot(X, Y, g > .6 ? C[4] : C[3], std::min(1., g * 1.6));
        q.glow(x, y, 6, C[2], .45 * g);
        return;
    }
    if (g > .3)
        for (int j = 1; j <= 4; ++j) {
            const auto a = f.pos(t - j * .05);
            q.dot(a.x, a.y, C[2], (g - .3) * (1 - j / 5.) * 1.3);
        }
    if (f.depth == 1) {
        q.px(X, Y, g > .5 ? C[5] : g > .15 ? C[3] : C[1]);
        if (g > .4) q.px(X, Y + 1, C[2]);
        q.glow(x, y, 9, C[2], .5 * g);
        q.glow(x, y, 3, C[4], .5 * g);
        return;
    }
    const int    dir  = f.pos(t + .05).x >= x ? 1 : -1;
    const double beat = std::fmod(std::floor(t * 30), 2);
    q.px(X + dir, Y, C[0]);
    q.px(X + dir * 2, Y, C[0]);
    q.dot(X, Y - 1 - beat, mix(C[1], WHITE, .4), .7);
    q.dot(X + dir, Y - 1 - beat, mix(C[1], WHITE, .4), .7);
    q.px(X, Y, g > .3 ? C[5] : C[1]);
    q.px(X - dir, Y, g > .5 ? C[4] : g > .15 ? C[3] : C[1]);
    if (g > .6) q.px(X - dir, Y + 1, C[3]);
    if (g > .7)
        for (const auto a : std::array<Point, 4>{{{0, -2}, {0, 2}, {-2, 0}, {2, 0}}})
            q.dot(X - dir + a.x, Y + a.y, C[3], (g - .7) * 3);
    q.glow(x, y, 28, C[2], .45 * g);
    q.glow(x, y, 10, C[4], .5 * g);
    q.glow(x, y, 4, C[5], .5 * g);
}
void grass(Paint& q, double t, uint32_t seed, std::span<const uint32_t> G, std::span<const uint32_t> C,
           const std::array<Fly, 32>& fs, bool front) {
    struct Lit {
        Point  pos;
        double glow;
    };
    std::array<Lit, 32> lit{};
    size_t              count = 0;
    for (const auto& f : fs)
        if (f.glow > .25 && f.depth > 0) lit[count++] = {f.pos(t), f.glow};
    const uint32_t n = front ? 150 : 230;
    for (uint32_t i = 0; i < n; ++i) {
        const auto   R    = [&](uint32_t k) { return rand({seed * 3 + (front ? 1u : 0u), i, k}); };
        const double x    = -162 + (i + R(1)) * (324. / n),
                     h    = (front ? 4 : 5) + R(2) * (front ? 8 : 11) + (R(3) < .1 ? 6 : 0),
                     base = (front ? 9 : 2) - R(4) * 3;
        const double sway = (1.2 + R(5)) * std::sin(cyc(t, 2, -x * .03) + R(6)) + .6 * std::sin(cyc(t, 5, x * .07));
        const int    tone = 1 + static_cast<int>(std::floor(R(7) * (front ? 3 : 2)));
        double       tip  = 0;
        for (size_t j = 0; j < count; ++j) {
            const auto&  a = lit[j];
            const double d = jsHypot2(a.pos.x - (x + sway), a.pos.y - (base - h));
            if (d < 28) tip = std::max(tip, a.glow * (1 - d / 28));
        }
        for (int r = 0; r < h; ++r) {
            const double u = r / h;
            const auto   c = u > .8 && tip > .2 ? (tip > .55 && u > .9 ? C[3] : C[2])
                             : u > .7           ? G[std::min(5, tone + 1)]
                                                : G[tone];
            q.px(x + sway * u * u, base - r, c);
        }
        if (R(8) < .07) {
            const double sx = x + sway, sy = base - h;
            q.px(sx, sy - 1, tip > .2 ? C[3] : G[5]);
            q.px(sx - 1, sy, G[4]);
            q.px(sx + 1, sy, G[4]);
        }
    }
}
void layer(Paint& p, double t, const PixelFxSettings& o, bool front) {
    auto       q  = weather::clip(p, SOURCE);
    const auto fs = flies(o.seed, t);
    if (!front)
        weather::rows(p, SOURCE, -170, 9, [&](double y, auto row) {
            const double k = (y + 170) / 180;
            for (auto& v : row) {
                v.c     = mix(o.weather.grass[0], o.weather.grass[1], k);
                v.a     = static_cast<float>(.48 + .14 * k);
                v.blend = true;
            }
        });
    grass(q, t, o.seed, o.weather.grass, o.palette, fs, front);
    for (const auto& f : fs)
        if ((f.depth == 2) == front) firefly(q, t, f, o.palette);
}
}  // namespace
const Descriptor& firefliesDescriptor() {
    static const std::array<Option, 1> options{{{"grass", 1, 64, false, 6, &PixelFxWeatherSettings::grass}}};
    static const Descriptor            d{SOURCE,
                                         6,
                                         [](PixelFxSettings& s) {
                                  s.seed          = 2;
                                  s.palette       = {hex(0x2a3a10), hex(0x5a7a1a), hex(0x9ad02a),
                                                     hex(0xd6ff5a), hex(0xf4ffb8), WHITE};
                                  s.weather.grass = {hex(0x070f0b), hex(0x0c1a12), hex(0x13281a),
                                                     hex(0x1d3a24), hex(0x2b5232), hex(0x3f6e40)};
                                         },
                                         [](Paint& p, double t, const PixelFxSettings& o) { layer(p, t, o, true); },
                                         options,
                                         nullptr,
                                         [](Paint& p, double t, const PixelFxSettings& o) { layer(p, t, o, false); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
