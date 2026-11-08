// eve-hot-path
// Native adaptation of pixelfx weather/snow.js (MIT), including Sprite Fusion snow.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Descriptor.h"
#include "stylize/pixelfx/Weather.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double               D = 12;
constexpr PixelFxInfo          SOURCE{PixelFxKind::Snow, "weather/snow", 320, 180, 160, 170, D, true};
constexpr std::array<Point, 4> AXIS{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}}, DIAG{{{1, 1}, {-1, -1}, {1, -1}, {-1, 1}}};
double                         bank(double x) {
    return -3 + 1.6 * std::sin(x * .045 + 1.3) + 1.1 * std::sin(x * .12 + .4) + .6 * std::sin(x * .31 + 2);
}
double gust(double t) { return 10 * std::sin((TAU * t) / D + .6) + 4 * std::sin((TAU * 2 * t) / D + 2.1); }
void   crystal(Paint& q, double x, double y, int r, bool turn, std::span<const uint32_t> C) {
    const double X = jsRound(x), Y = jsRound(y);
    const auto&  lng  = turn ? DIAG : AXIS;
    const auto&  shrt = turn ? AXIS : DIAG;
    const int    R    = turn ? r - 1 : r;
    for (const auto& a : lng) {
        const double dx = a.x, dy = a.y;
        for (int d = 1; d <= R; ++d) q.px(X + dx * d, Y + dy * d, d == R ? WHITE : C[6]);
        const int count = turn ? (R >= 3 ? 1 : 0) : R >= 5 ? 2 : R >= 4 ? 1 : 0;
        for (int j = 0; j < count; ++j) {
            const int    n  = R - 1 - j;
            const double ox = turn ? dx : dy, oy = turn ? -dy : -dx;
            q.px(X + dx * n + (turn ? 0 : ox), Y + dy * n + oy, C[4]);
            q.px(X + dx * n + (turn ? -dx : -ox) + (turn ? dx : 0), Y + dy * n + (turn ? dy * 0 : -oy), C[4]);
        }
    }
    for (const auto& a : shrt)
        for (int d = 1; d <= (turn ? 1 : std::max(1, R / 2)); ++d) q.px(X + a.x * d, Y + a.y * d, d == 1 ? C[6] : C[5]);
    q.px(X, Y, WHITE);
    q.glow(X + .5, Y + .5, r * 2.5 + 5, C[5], .13);
    q.light(X, Y, C[6], .4);
}
struct Layer {
    int    n, m0, m1;
    double air, s0, s1, wind, gust;
};
constexpr std::array<Layer, 3> LAYERS{
    {{230, 1, 2, 1, 1, 3, 3, .4}, {140, 2, 3, .8, 3, 7, 6, .7}, {24, 2, 3, .74, 8, 15, 9, 1}}};
void layer(Paint& q, double t, uint32_t S, std::span<const uint32_t> C, int L) {
    const auto     Ly = LAYERS[size_t(L)];
    const uint32_t sd = S * 13 + uint32_t(L);
    weather::trips(t, D, Ly.n, sd, Ly.m0, Ly.m1, [&](double u, double k, int m, uint32_t i) {
        const auto   j   = jsUint(wrap(k, m));
        const auto   R   = [&](uint32_t n) { return rand({sd, i, j, n}); };
        const double dur = D / m, tf = dur * Ly.air, age = std::min(u * dur, tf), sw = Ly.s0 + R(3) * (Ly.s1 - Ly.s0),
                     f = .25 + R(4) * .35, ph = R(5) * TAU;
        const auto   xAt = [&](double a) {
            return -175 + R(2) * 350 + Ly.wind * a + (gust(t - u * dur + a) - gust(t - u * dur)) * Ly.gust +
                   sw * std::sin(TAU * f * a + ph);
        };
        const double x = xAt(age), xl = xAt(tf), y0 = -176, y1 = L ? bank(xl) - .5 : -110 + R(1) * 106,
                     y = y0 + (y1 - y0) * (age / tf), rest = u * dur - tf;
        if (L == 0) {
            const double a = std::min(1., (1 - u) * 6);
            q.dot(x, y, R(6) < .5 ? C[4] : C[5], a);
            return;
        }
        const double fade = rest > 0 ? 1 - rest / (dur - tf) : 1;
        if (L == 1) {
            const bool big = R(6) < .3;
            if (rest > 0) {
                q.dot(xl, y1, C[7], fade * .9);
                return;
            }
            q.px(x, y, big ? WHITE : C[7]);
            if (big)
                for (const auto& a : AXIS) q.px(x + a.x, y + a.y, C[5]);
            else {
                q.px(x + 1, y, C[6]);
                q.px(x, y + 1, C[6]);
                q.px(x + 1, y + 1, C[5]);
            }
            return;
        }
        const int    r    = R(6) < .4 ? 3 : R(6) < .8 ? 4 : 5;
        const double spin = 1.5 + R(7) * 2.5;
        if (rest > 0) {
            q.dot(xl - 1, y1, C[6], fade);
            q.dot(xl, y1, WHITE, fade);
            q.dot(xl + 1, y1, C[6], fade);
            q.glow(xl, y1, 5, C[5], .25 * fade);
            return;
        }
        crystal(q, x, y, r, int(std::floor(age * spin + R(8) * 2)) % 2 != 0, C);
    });
}
void drift(Paint& q, double t, uint32_t S, std::span<const uint32_t> C) {
    weather::pass(q, SOURCE, -7, 9, [&](double x, double y, weather::Output& out) {
        const double d = y - bank(x);
        if (d < 0) return;
        out.c = d < 1 ? WHITE : d < 2 ? C[7] : d < 3.5 ? C[6] : d < 6 ? C[5] : C[4];
        if (d < 1) {
            out.l = C[6];
            out.k = .12;
        }
    });
    for (uint32_t i = 0; i < 18; ++i) {
        const double x = -156 + rand({S, i, 81}) * 312, c = 1 + std::floor(rand({S, i, 82}) * 3),
                     tw = std::sin((TAU * c * t) / D + rand({S, i, 83}) * TAU);
        if (tw < .55) continue;
        const double y = std::ceil(bank(x)) + std::floor(rand({S, i, 84}) * 3), k = (tw - .55) / .45;
        q.px(x, y, WHITE);
        if (k > .5)
            for (const auto& a : std::array<Point, 3>{{{1, 0}, {-1, 0}, {0, -1}}}) q.px(x + a.x, y + a.y, C[6]);
        q.glow(x + .5, y + .5, 6, C[6], .35 * k);
    }
}
void back(Paint& p, double t, const PixelFxSettings& o) {
    auto           q    = weather::clip(p, SOURCE);
    const auto&    C    = o.palette;
    const uint32_t haze = mix(C[1], C[2], .4);
    weather::pass(p, SOURCE, -170, 9, [&](double, double y, weather::Output& out) {
        out.c     = haze;
        out.a     = .26 + .22 * smooth(-170, 0, y);
        out.blend = true;
    });
    layer(q, t, o.seed, C, 0);
    drift(q, t, o.seed, C);
    layer(q, t, o.seed, C, 1);
}
void front(Paint& p, double t, const PixelFxSettings& o) {
    auto q = weather::clip(p, SOURCE);
    layer(q, t, o.seed, o.palette, 2);
}
}  // namespace
const Descriptor& snowDescriptor() {
    static const Descriptor d{SOURCE,
                              8,
                              [](PixelFxSettings& s) {
                                  s.seed    = 6;
                                  s.palette = {hex(0x0c1020), hex(0x161d36), hex(0x24304f), hex(0x3a4a72),
                                               hex(0x5f74a3), hex(0x93abd6), hex(0xcfe0fa), WHITE};
                              },
                              front,
                              {},
                              nullptr,
                              back};
    return d;
}
}  // namespace eve::stylize::pixelfx
