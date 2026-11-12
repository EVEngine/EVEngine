// eve-hot-path
// Native adaptation of pixelfx scifi/jetpack.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/ScifiGhosts.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double D = 2, GAP = 8;
struct Jetpack final : AnalyticKernel {
    scifi::Ghosts ghosts{D, INFINITY, std::array<double, 4>{-46, -28, 46, 84}};
    explicit Jetpack(const PixelFxSettings& o) {
        const auto&  F = o.palette;
        const double L = o.scifi.length;
        Rng          rng(o.seed);
        ghosts.sparks = {WHITE, F[6], F[5], F[4]};
        for (int side : {-1, 1}) {
            for (int i = 0; i < 26; ++i) {
                const double born = rng.range(0, D), x = side * GAP + rng.range(-1.5, 1.5), y = L * rng.range(.55, .85),
                             vx = rng.range(-10, 10), vy = rng.range(70, 130), life = rng.range(.22, .4),
                             size  = rng.range(2.2, 3.4);
                const auto   flags = ParticleFlag::Smokes | (rng.isChance(.5) ? ParticleFlag::Big : ParticleFlag::None);
                ghosts.add(born, ParticleKind::Fire, x, y, vx, vy, life, 0, size, flags);
            }
            for (int i = 0; i < 10; ++i) {
                const double born = rng.range(0, D), x = side * GAP + rng.range(-3, 3), y = L + rng.range(2, 8),
                             vx = side * rng.range(0, 14), vy = rng.range(40, 80), life = rng.range(1.1, 1.6),
                             size = rng.range(3, 5);
                ghosts.add(born, ParticleKind::Smoke, x, y, vx, vy, life, 0, size, ParticleFlag::Big);
            }
            for (int i = 0; i < 10; ++i) {
                const double born = rng.range(0, D), x = side * GAP + rng.range(-1, 1), y = L * rng.range(.3, .7),
                             vx = side * rng.range(-40, 60), vy = rng.range(80, 200), life = rng.range(.2, .45);
                ghosts.add(born, ParticleKind::Spark, x, y, vx, vy, life);
            }
            for (int i = 0; i < 5; ++i) {
                const double born = rng.range(0, D), x = side * GAP + rng.range(-2, 2), y = L * rng.range(.6, 1),
                             vx = side * rng.range(10, 40), vy = rng.range(20, 60), life = rng.range(.6, 1.1),
                             size = rng.range(.8, 1.6);
                ghosts.add(born, ParticleKind::Ember, x, y, vx, vy, life, F[4], size);
            }
        }
    }
    void pack(Paint& p, std::span<const uint32_t> H, std::span<const uint32_t> F, double dy) const {
        for (int side : {-1, 1}) {
            const double cx = side * GAP;
            p.shade(cx - 5, -24 + dy, cx + 5, -3 + dy, [&](double x, double y, int sx, int sy) {
                const double u = (x - cx) / 4.5, top = -19 + dy, v = y < top ? (y - top) / 4.5 : 0;
                if (u * u + v * v > 1) return;
                const double light = -u * .8 - v * .6;
                p.put(sx, sy,
                      u * u + v * v > .78             ? H[0]
                      : std::abs(y - (-12 + dy)) < .6 ? H[1]
                      : light > .55                   ? H[5]
                      : light > .15                   ? H[4]
                      : light > -.35                  ? H[3]
                                                      : H[2]);
            });
            for (int y = -3; y <= 0; ++y) {
                const double w = 2 + (y + 3) * .7;
                for (double x = std::floor(cx - w); x <= std::ceil(cx + w); ++x) {
                    const bool inner = std::abs(x + .5 - cx) < w - 1.2 && y >= -1;
                    p.px(x, y + dy,
                         inner                            ? (y == 0 ? F[6] : F[4])
                         : std::abs(x + .5 - cx) > w - .6 ? H[0]
                         : x < cx                         ? H[3]
                                                          : H[1]);
                }
            }
        }
        for (int y = -20; y <= -6; ++y) {
            p.px(-1, y + dy, H[1]);
            p.px(0, y + dy, H[2]);
            p.px(1, y + dy, H[0]);
        }
        p.px(0, -16 + dy, F[5]);
    }
    void drawBack(Paint&, double, const PixelFxSettings&) const override {}
    void draw(Paint& p, double t, const PixelFxSettings& o) const override {
        const auto&  F     = o.palette;
        const auto&  H     = o.scifi.metal;
        const double phase = t / D, frame = std::fmod(std::floor(t * 30), 60),
                     dy        = jsRound(std::sin(TAU * 12 * phase) * .6);
        const auto   particles = ghosts.evaluate(t);
        particles.draw(p, Paint::Layer::Back);
        for (int side : {-1, 1}) {
            const double cx = side * GAP + (rand({o.seed, jsUint(frame), jsUint(side)}) < .15 ? side * .5 : 0),
                         L  = o.scifi.length * (.92 + .08 * std::sin(TAU * 7 * phase + side) +
                                                .06 * (rand({o.seed, jsUint(frame), jsUint(side), 1}) - .5));
            p.shade(cx - 7, dy, cx + 7, dy + L + 4, [&](double x, double y, int sx, int sy) {
                const double v = (y - dy) / L, w = lerp(3.4, .6, v) + 1.4 * std::sin(v * PI) * (v < 1 ? 1 : 0),
                             n = tileFbm((x - cx) / 3 + side * 5, (y - dy) / 4 - phase * 20, 0, 10,
                                         o.seed + (side > 0 ? 9u : 0u), 2),
                             q = std::abs(x - cx) / w + n * .45;
                if (v > 1.15 || q > 1.2) return;
                const double heat = (1 - v) * 1.15 - q * .9 + n * .25;
                if (q > .95) {
                    p.put(sx, sy, heat > .2 ? F[3] : F[2], heat < -.3 ? .5 : 1);
                    return;
                }
                p.put(sx, sy,
                      heat > .82   ? WHITE
                      : heat > .62 ? F[6]
                      : heat > .42 ? F[5]
                      : heat > .22 ? F[4]
                      : heat > .02 ? F[3]
                                   : F[2]);
            });
            for (int i = 0; i < 3; ++i) {
                const double y = jsRound(dy + 5 + i * 6.5 + std::sin(TAU * 4 * phase + i) * .5),
                             r = jsRound(2.2 - i * .45 + .4 * std::sin(TAU * 8 * phase + i * 2));
                if (y > dy + L * .75 || r < 1) break;
                for (double yy = -r; yy <= r; ++yy)
                    for (double xx = -r; xx <= r; ++xx) {
                        const double distance = std::abs(xx) + std::abs(yy);
                        if (distance <= r) p.px(cx + xx, y + yy, distance < r ? WHITE : i == 0 ? F[6] : F[5]);
                    }
                p.glow(cx, y, 6, F[6], .35);
            }
            p.glow(cx, dy + 2, 9, WHITE, .45);
            p.glow(cx, dy + L * .45, L * .8, F[4], .35);
        }
        pack(p, H, F, dy);
        particles.draw(p, Paint::Layer::Front);
        p.glow(0, o.scifi.length + 14, 34, F[4], .16);
        p.glow(0, o.scifi.length * .4, 30, F[5], .18);
    }
};
}  // namespace
const Descriptor& jetpackDescriptor() {
    static const std::array<Option, 2> options{{{"metal", 1, 64, false, 6, &PixelFxScifiSettings::metal},
                                                {"length", 0, 128, false, 0, &PixelFxScifiSettings::length}}};
    static const Descriptor            d{
        {PixelFxKind::Jetpack, "scifi/jetpack", 96, 116, 48, 30, D, true},
        8,
        [](PixelFxSettings& s) {
            s.seed         = 30;
            s.scifi.length = 26;
            s.palette      = {hex(0x2a0e12), hex(0x5c1818), hex(0xa02818), hex(0xe0501a),
                              hex(0xff8a2a), hex(0xffc23d), hex(0xfff3a6), WHITE};
            s.scifi.metal  = {hex(0x0e0d14), hex(0x1e1c27), hex(0x312e3e), hex(0x4b4759), hex(0x6f6a82), hex(0x9d98b0)};
        },
        nullptr,
        options,
        nullptr,
        nullptr,
        [](const PixelFxSettings& s) -> std::unique_ptr<const AnalyticKernel> { return std::make_unique<Jetpack>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
