// eve-hot-path
// Native adaptation of pixelfx weather/clouds.js (MIT), including Sprite Fusion fog shading.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Descriptor.h"
#include "stylize/pixelfx/Weather.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double      D = 12, MX = 92, MY = -128, MR = 12;
constexpr PixelFxInfo SOURCE{PixelFxKind::Clouds, "weather/clouds", 320, 180, 160, 180, D, true};
struct Layer {
    int                n;
    double             w0, w1, h0, h1, y0, y1, v, life0, life1;
    std::array<int, 4> tone;
    double             alpha;
};
constexpr std::array<Layer, 3> LAYERS{{{7, 40, 70, 6, 11, -150, -110, 3, 8, 11, {2, 3, 4, 5}, .7},
                                       {6, 60, 100, 9, 15, -120, -70, 6, 8, 11, {1, 2, 4, 6}, .88},
                                       {5, 90, 140, 12, 20, -80, -30, 10, 9, 12, {1, 2, 3, 6}, 1}}};
void                           moon(Paint& q, std::span<const uint32_t> C) {
    q.shader(MX - MR - 1, MY - MR - 1, MX + MR + 1, MY + MR + 1, [&](double x, double y, ShadeOutput& out) {
        const double dx = x - MX, dy = y - MY, d = jsHypot2(dx, dy) / MR;
        if (d > 1) return;
        const bool sea = std::pow(dx + 4, 2) + std::pow(dy + 2, 2) < 14 ||
                         std::pow(dx - 3, 2) + std::pow(dy - 4, 2) < 9 || std::pow(dx - 5, 2) + std::pow(dy + 5, 2) < 4;
        out.c          = d > .86 ? (dx + dy < 0 ? C[6] : WHITE) : sea ? C[5] : d < .5 && dx + dy < 0 ? WHITE : C[7];
    });
}
void cloud(Paint& q, double t, uint32_t S, std::span<const uint32_t> C, const Layer& L, uint32_t j, uint32_t i) {
    const auto   R    = [&](uint32_t k) { return rand({S, j, i, k}); };
    const double life = L.life0 + R(1) * (L.life1 - L.life0), age = wrap(t - R(2) * D, D);
    if (age >= life) return;
    const double u = age / life, grow = std::min(1., u * 4) * std::min(1., (1 - u) * 4),
                 w = (L.w0 + R(3) * (L.w1 - L.w0)) * (.6 + .4 * grow), x0 = -200 + R(4) * 330 + L.v * age,
                 base = L.y0 + R(5) * (L.y1 - L.y0), H = (L.h0 + R(6) * (L.h1 - L.h0)) * grow;
    if (H < 1.5) return;
    struct Puff {
        double x, y, r;
    };
    std::array<Puff, 10> puffs{};
    size_t               count = 0;
    const int            n     = 4 + int(std::floor(R(7) * 4));
    for (int k = 0; k < n; ++k) {
        const double f = (k + .5) / n, r = H * (.5 + .35 * std::sin(PI * f)) * (.75 + .5 * R(uint32_t(10 + k))) *
                                           (1 + .06 * std::sin((TAU * t) / D + k + i));
        puffs[count++] = {x0 + w * (f - .5) + (R(uint32_t(20 + k)) - .5) * 6,
                          base - r * .3 - R(uint32_t(30 + k)) * H * .15, r};
    }
    for (int k = 0; k < 1 + std::floor(R(8) * 3); ++k) {
        const double f = .3 + R(uint32_t(40 + k)) * .4,
                     r = H * (.75 + .35 * R(uint32_t(50 + k))) * (1 + .05 * std::sin((TAU * t) / D + k * 2 + i));
        puffs[count++] = {x0 + w * (f - .5), base - H * .55 - r * .2, r};
    }
    const double x1 = x0 - w / 2 - H, x2 = x0 + w / 2 + H;
    const auto   B = weather::box(SOURCE);
    for (int x = int(std::max(B.x0, std::floor(x1))); x <= std::min(B.x1 - 1, std::ceil(x2)); ++x) {
        const double cx  = x + .5;
        double       top = INFINITY, top2 = INFINITY;
        for (size_t k = 0; k < count; ++k) {
            const auto   a  = puffs[k];
            const double dx = cx - a.x;
            if (dx * dx >= a.r * a.r) continue;
            const double y = a.y - std::sqrt(a.r * a.r - dx * dx);
            if (y < top) {
                top2 = top;
                top  = y;
            } else if (y < top2)
                top2 = y;
        }
        if (top == INFINITY) continue;
        const double end = std::min(cx - x1, x2 - cx) / H,
                     bot = base + 1.5 + std::sin(cx * .21 + i) * .8 - (end < 1 ? (1 - end) * 4 : 0);
        if (bot <= top) continue;
        const double h = bot - top, inner = top2 - top > 2.5 && top2 < bot - 2 ? std::floor(top2) : -999;
        for (int y = int(std::floor(top)); y < bot; ++y) {
            const double d = y + .5 - top, v = d / h, th = weather::bayer(x, y) * .12, md = jsHypot2(cx - MX, y - MY);
            int          c = d < 1 ? L.tone[3] : v < .25 + th ? L.tone[2] : v < .62 + th ? L.tone[1] : L.tone[0];
            if (y == inner) c = std::max(c, L.tone[2]);
            if (bot - y - .5 < 1 && v > .5) c = std::max(0, L.tone[0] - 1);
            if (md < 46 && (d < 2 || y == inner || v < .3)) c = std::min(7, c + (md < 24 ? 2 : 1));
            const double a = L.alpha * std::min(1., grow * 1.5);
            if (a >= 1)
                q.px(x, y, C[size_t(c)]);
            else
                q.blend(x, y, C[size_t(c)], a);
            if (d < 1 && md < 46) q.light(x, y, C[6], .35 * (1 - md / 46));
        }
    }
}
void draw(Paint& p, double t, const PixelFxSettings& o) {
    auto q = weather::clip(p, SOURCE);
    moon(q, o.palette);
    for (uint32_t j = 0; j < LAYERS.size(); ++j)
        for (uint32_t i = 0; i < uint32_t(LAYERS[j].n); ++i) cloud(q, t, o.seed, o.palette, LAYERS[j], j, i);
    q.glow(MX, MY, 110, o.palette[4], .14);
    q.glow(MX, MY, 40, o.palette[5], .25);
    q.glow(MX, MY, 18, o.palette[6], .3);
}
}  // namespace
const Descriptor& cloudsDescriptor() {
    static const Descriptor d{SOURCE, 8,
                              [](PixelFxSettings& s) {
                                  s.seed    = 7;
                                  s.palette = {hex(0x0c0c18), hex(0x16172a), hex(0x232540), hex(0x363a5a),
                                               hex(0x545a80), hex(0x8088ae), hex(0xbcc4e2), hex(0xf2f4ff)};
                              },
                              draw};
    return d;
}
}  // namespace eve::stylize::pixelfx
