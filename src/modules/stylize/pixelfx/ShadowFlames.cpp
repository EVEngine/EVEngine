// eve-hot-path
// Native adaptation of pixelfx shadow/shadow-flames.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/PixelFxInternal.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double D  = 2.4;
constexpr int    GX = 22, GY = 60;
struct Tongue {
    double   dx, height, width;
    uint32_t seed;
};
constexpr std::array<Tongue, 5> TONGUES{
    {{-11, 20, 4.2, 1}, {11, 22, 4.4, 2}, {-6, 31, 5.4, 3}, {6, 29, 5.2, 4}, {0, 40, 6.6, 5}}};
struct FlameLayer {
    double width, height;
    int    tone;
};
constexpr std::array<FlameLayer, 4> LAYERS{{{1, 1, 0}, {.8, .88, 1}, {.54, .68, 2}, {.32, .48, 3}}};
double                              loopNoise(double t, double v, uint32_t seed, double speed = 5) {
    return noise2(std::cos((TAU * t) / D) * speed + v, std::sin((TAU * t) / D) * speed, seed);
}
using FlameGrid = std::array<int8_t, GX * 2 * GY>;
FlameGrid flameGrid(double t) {
    FlameGrid grid;
    grid.fill(-1);
    const auto set = [&](double x, double y, int v) {
        const int i = int(jsRound(x)) + GX, j = int(jsRound(y)) + GY;
        if (i >= 0 && j >= 0 && i < GX * 2 && j < GY) grid[size_t(j * GX * 2 + i)] = int8_t(v);
    };
    // Stable descending absolute offset in the pinned source: both ties retain input order.
    for (size_t i : {size_t(0), size_t(1), size_t(2), size_t(3), size_t(4)}) {
        const auto     tongue = TONGUES[i];
        const double   dx = tongue.dx, H0 = tongue.height, w = tongue.width;
        const uint32_t n = tongue.seed;
        const double   h = H0 * (.78 + .22 * loopNoise(t, n * 3, n, 4)), sway = loopNoise(t, n * 7, n + 9, 3) * 3;
        for (const auto layer : LAYERS) {
            const double Hl = h * layer.height;
            for (int r = 0; r <= Hl; ++r) {
                const double u    = r / std::max(1., Hl),
                             half = w * layer.width * std::pow(1 - u, .85) * std::min(1., (r + 1.5) / 3.5) +
                                    loopNoise(t, r * .45 + n, n + 20, 6) * 1.1 +
                                    loopNoise(t, r * 1.3 + n * 5, n + 50, 9) * .6,
                             cx = dx + sway * u * u * layer.height + loopNoise(t, r * .2 + n * 2, n + 30, 5) * u * 1.4;
                if (half < .3) continue;
                for (int x = int(jsRound(cx - half)); x <= jsRound(cx + half); ++x) {
                    if (layer.tone == 0 && u > .45 && loopNoise(t, x * .9 + r * .7 + n, n + 60, 8) > .35) continue;
                    set(x, -r, layer.tone);
                }
            }
        }
    }
    return grid;
}
struct ShadowFlames final : AnalyticKernel {
    void drawBack(Paint& p, double t, const PixelFxSettings& o) const override {
        const auto& C = o.palette;
        p.ellipse(
            0, .5, 22, 3.2, [&](double d) { return d < .6 ? C[0] : C[1]; },
            [](double d) { return d < .85 ? 1 : (1 - d) * 6; });
        p.glow(0, -2, 46, C[4], .3);
        cycle(t, D, 14, o.seed + 1, 1.2, 1.9, [&](double age, double L, Rng& r, int) {
            const double u = age / L, x0 = r.range(-6, 6), phase = r.range(0, 6),
                         x = x0 + std::sin(u * 4 + phase) * 5 * u, y = -30 - u * r.range(36, 52), rad = 3 + u * 6,
                         a = std::min(1., u * 6) * (1 - u) * .7;
            p.disc(x, y, rad, mix(mix(C[3], C[2], .5), C[1], u), a);
            p.disc(x - rad * .2, y - rad * .3, rad * .5, C[3], a * .4);
            p.disc(x, y + rad * .45, rad * .55, C[5], a * .45 * (1 - u));
        });
    }
    void draw(Paint& p, double t, const PixelFxSettings& o) const override {
        const auto&                   C = o.palette;
        const std::array<uint32_t, 4> tone{C[6], C[4], C[2], C[0]};
        const auto                    grid = flameGrid(t);
        for (int j = 0; j < GY; ++j)
            for (int i = 0; i < GX * 2; ++i) {
                const auto v = grid[size_t(j * GX * 2 + i)];
                if (v < 0) continue;
                const int x = i - GX, y = j - GY;
                uint32_t  c = tone[size_t(v)];
                if (v == 0 && y < -24 && (j == 0 || grid[size_t((j - 1) * GX * 2 + i)] < 0)) c = y < -32 ? WHITE : C[7];
                p.px(x + .5, y + .5, c);
            }
        cycle(t, D, 34, o.seed, .25, .5, [&](double age, double L, Rng& r, int) {
            const double u      = age / L;
            const auto   tongue = TONGUES[size_t(r.integer(0, 4))];
            const double a = r.range(-4, 4), b = r.range(-5, 5), x = tongue.dx + a + u * b, h = r.range(.45, .85),
                         rise = r.range(10, 20), y = -tongue.height * h - u * rise,
                         rad = r.range(1, 2.2) * (1 - u) + .3;
            p.circle(x, y, rad + .6, u < .5 ? C[6] : C[4]);
            if (rad > 1) p.circle(x, y + .4, rad * .5, u < .3 ? C[2] : C[4]);
        });
        cycle(t, D, 30, o.seed + 2, 1., 1.8, [&](double age, double L, Rng& r, int) {
            const double u = age / L, x0 = r.range(-12, 12), phase = r.range(0, 6),
                         x = x0 + std::sin(u * 6 + phase) * 5 * u, y = -10 - u * r.range(40, 70);
            if (u > .7 && std::sin(age * 40 + x) < 0) return;
            p.px(x, y, u < .3 ? WHITE : u < .6 ? C[7] : C[6]);
            p.glow(x, y, 3, C[6], .35 * (1 - u));
        });
        const double fl = .85 + .15 * loopNoise(t, 0, 77, 6);
        p.glow(0, -16, 52, C[4], .5 * fl);
        p.glow(0, -12, 26, C[6], .34 * fl);
        for (int j = 0; j < GY; ++j)
            for (int i = 0; i < GX * 2; ++i)
                if (grid[size_t(j * GX * 2 + i)] == 3) {
                    const int x = p.cellX(i - GX + .5, j - GY + .5), y = p.cellY(i - GX + .5, j - GY + .5);
                    if (p.contains(x, y) && p.isInsideClip(x, y)) p.lights[p.index(x, y)] = 0;
                }
    }
};
}  // namespace
const Descriptor& shadowFlamesDescriptor() {
    static const Descriptor d{{PixelFxKind::ShadowFlames, "shadow/shadow-flames", 104, 112, 52, 102, D, true},
                              8,
                              [](PixelFxSettings& s) {
                                  s.seed    = 12;
                                  s.palette = {hex(0x06040b), hex(0x150c26), hex(0x2a1150), hex(0x461a84),
                                               hex(0x6f2cc0), hex(0x9d5cec), hex(0xcfa6ff), hex(0xf6eaff)};
                              },
                              nullptr,
                              {},
                              nullptr,
                              nullptr,
                              [](const PixelFxSettings&) -> std::unique_ptr<const AnalyticKernel> {
                                  return std::make_unique<ShadowFlames>();
                              }};
    return d;
}
}  // namespace eve::stylize::pixelfx
