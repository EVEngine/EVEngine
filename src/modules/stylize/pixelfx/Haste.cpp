// eve-hot-path
// Native adaptation of pixelfx status/haste.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/PixelFxInternal.h"
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/StatusShape.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double D = 1.2;
struct Haste final : AnalyticKernel {
    status::Shape                   shape;
    std::vector<std::array<int, 3>> levels;
    explicit Haste(const PixelFxSettings& o) : shape(o.actor) {
        for (const auto& q : shape.body) {
            const double v = luma(status::colorAt(o.actor, q[0], q[1]));
            levels.push_back({q[0], q[1], v < .24 ? 1 : v < .66 ? 2 : 3});
        }
    }
    static void lines(Paint& p, const PixelFxSettings& o, double t, uint32_t seed, bool front) {
        const auto& C = o.palette;
        const auto& a = o.actor;
        cycle(t, D, front ? 5 : 11, seed + (front ? 9u : 7u), .12, .26, [&](double age, double life, Rng& r, int) {
            const double u = age / life, y = a.y + r.range(2, a.height - 3),
                         length  = r.range(10, 26) * (front ? .7 : 1);
            const double initial = r.range(-14, 4), speed = r.range(160, 240), x = initial - age * speed;
            for (int d = 0; d < length; ++d) {
                if (d > length * .45 && d % 2) continue;
                p.dot(x + d, y,
                      d < 2             ? C[6]
                      : d < length * .4 ? C[5]
                                        : C[3],
                      (1 - d / length) * (1 - u) * (front ? .7 : 1.1));
            }
            p.light(x, y, C[4], .4 * (1 - u));
        });
    }
    void drawBack(Paint& p, double t, const PixelFxSettings& o) const override {
        const auto&    C = o.palette;
        const auto&    a = o.actor;
        const uint32_t f = uint32_t(std::floor(t * 30));
        for (int g = 3; g >= 1; --g) {
            const double dx    = -g * 14 - jsRound(rand({f >> 1, uint32_t(g), 3u}) * 1.6);
            const int    every = std::array<int, 4>{0, 3, 4, 0}[size_t(g)];
            p.outline({a.width, a.height, a.mask}, a.x + dx, a.y,
                      [&](int mx, int my) { return g == 3 && (mx + my) % 2 ? 0u : C[size_t(5 - g)]; });
            if (every)
                for (const auto& q : levels) {
                    if (wrap(q[1] + double(f), every) != 0) continue;
                    p.px(a.x + q[0] + dx, a.y + q[1], C[size_t(std::clamp(5 - g + q[2] - 2, 0, 6))]);
                }
            p.glow(a.x + double(a.width) / 2 + dx, a.y + a.height * .45, 16, C[3], .12 * (1 - g * .25));
        }
        for (const auto& q : shape.side) {
            const int x = q[0], y = q[1], dir = q[2];
            if (dir > 0 || (uint32_t(y) + (f >> 1)) % 3) continue;
            const int length = 3 + int(std::floor(rand({uint32_t(y), f >> 1, 5u}) * 6));
            for (int d = 1; d <= length; ++d)
                p.dot(a.x + x - d, a.y + y, d < 3 ? C[5] : C[4], 1 - double(d) / (length + 1));
        }
        lines(p, o, t, o.seed, false);
        cycle(t, D, 12, o.seed + 5u, .45, .7, [&](double age, double life, Rng& r, int i) {
            const double u = age / life, foot = i % 2 ? -7 : 3, vx = -r.range(50, 100),
                         x = foot + status::travel(vx, 0, 3.5, age), vy = r.range(6, 18),
                         y      = -1.5 - status::travel(vy, 0, 3.5, age);
            const double radius = r.range(2.6, 4.6) * (.6 + 1 * u), k = std::pow(1 - u, 1.2) * std::min(1., u * 10);
            p.disc(x, y, radius, o.status.dust, k * .5);
            p.disc(x + radius * .05, y + radius * .08, radius * .74, o.status.dust, k * .62);
            p.disc(x - radius * .22, y - radius * .3, radius * .5, C[5], k * .25);
        });
    }
    void draw(Paint& p, double t, const PixelFxSettings& o) const override {
        const auto&    C = o.palette;
        const auto&    a = o.actor;
        const uint32_t f = uint32_t(std::floor(t * 15));
        for (const auto& q : shape.side)
            if (q[2] > 0 && rand({uint32_t(q[0]), uint32_t(q[1]), f}) < .85)
                p.px(a.x + q[0], a.y + q[1], rand({uint32_t(q[1]), f, 2u}) < .3 ? C[6] : C[5]);
        lines(p, o, t, o.seed, true);
        p.glow(-6, a.y + a.height * .5, 22, C[3], .16);
        p.glow(0, a.y + a.height * .45, 30, C[3], .16);
        p.glow(a.x + a.width, a.y + a.height * .5, 10, C[5], .12 + .06 * std::sin((t / D) * TAU * 3));
    }
};
}  // namespace
const Descriptor& hasteDescriptor() {
    static const std::array<Option, 2> options{
        {{"actor", 0, 0, false, 0, &PixelFxSettings::actor},
         {"dust", 0, 4294967295., false, 0, &PixelFxStatusSettings::dust, true}}};
    static const Descriptor d{
        {PixelFxKind::Haste, "status/haste", 100, 56, 64, 48, D, true},
        7,
        [](PixelFxSettings& s) {
            s.seed         = 4;
            s.actor.source = PixelFxActorSource::Dummy;
            s.status.dust  = hex(0x8a8090);
            s.palette      = {hex(0x4c25ab), hex(0x773ae4), hex(0x5b6cff), hex(0x5eb0ff),
                              hex(0xa3dcff), hex(0xccf6ff), WHITE};
        },
        nullptr,
        options,
        nullptr,
        nullptr,
        [](const PixelFxSettings& o) -> std::unique_ptr<const AnalyticKernel> { return std::make_unique<Haste>(o); },
        true};
    return d;
}
}  // namespace eve::stylize::pixelfx
