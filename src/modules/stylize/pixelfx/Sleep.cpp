// eve-hot-path
// Native adaptation of pixelfx status/sleep.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/PixelFxInternal.h"
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/StatusEyes.h"
#include "stylize/pixelfx/StatusVisual.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double                           D = 2.4;
constexpr std::array<std::string_view, 5>  Z0{"#####", "...#.", "..#..", ".#...", "#####"};
constexpr std::array<std::string_view, 7>  Z1{"#######", ".....#.", "....#..", "...#...",
                                              "..#....", ".#.....", "#######"};
constexpr std::array<std::string_view, 9>  Z2{".#######.", "#########", "......##.", ".....##..", "....##...",
                                              "...##....", "..##.....", "#########", ".#######."};
constexpr std::array<std::string_view, 11> Z3{".#########.", "###########", ".......###.", "......###..",
                                              ".....###...", "....###....", "...###.....", "..###......",
                                              ".###.......", "###########", ".#########."};
char                                       zMask(int size, int x, int y) {
    if (size == 0) return status::bitmap(Z0, x, y);
    if (size == 1) return status::bitmap(Z1, x, y);
    if (size == 2) return status::bitmap(Z2, x, y);
    return status::bitmap(Z3, x, y);
}
struct Z {
    double x, y;
    int    size;
    double alpha;
};
Z zAt(const PixelFxActor& a, int i, double u) {
    const double side = i % 2 ? 1 : .6, x0 = a.x + a.width * .78, y0 = a.y + 7,
                 x = x0 + 13 * side * u + 2.5 * std::sin(TAU * 1.1 * u + i * 2.1),
                 y = y0 - 32 * (1 - std::pow(1 - u, 1.35));
    return {x, y, u < .18 ? 0 : u < .42 ? 1 : u < .68 ? 2 : 3, std::min(1., u / .06)};
}
struct Sleep final : AnalyticKernel {
    const std::vector<status::Eye> eyeList;
    explicit Sleep(const PixelFxSettings& o) : eyeList(status::eyes(o.actor, status::Shape(o.actor))) {}
    void drawBack(Paint&, double, const PixelFxSettings&) const override {}
    void draw(Paint& p, double t, const PixelFxSettings& o) const override {
        const auto&  C      = o.palette;
        const auto&  a      = o.actor;
        const double breath = .5 + .5 * std::sin((t / D) * TAU * 2);
        for (const auto& e : eyeList) {
            for (const auto& q : e.pixels) p.px(a.x + q[0], a.y + q[1], e.skin);
            // Original lid-color lookup is unused and has no RNG or observable side effect.
            const double y = a.y + e.y1 - 1;
            for (int x = e.x0; x <= e.x1; ++x) p.px(a.x + x, y, 0xff1f1726u);
            p.px(a.x + e.x0 - 1, y - 1, 0xff1f1726u);
            p.px(a.x + e.x1 + 1, y - 1, 0xff1f1726u);
        }
        p.glow(0, a.y + 10, 24, C[3], .1 + .1 * breath);
        cycle(t, D, 5, o.seed + 6u, D, [&](double age, double life, Rng& r, int) {
            const double u = age / life, spread = r.range(.3, 1.1), phase = r.next() * TAU,
                         x = a.x + a.width * spread + std::sin(u * TAU + phase) * 3, y = a.y + r.range(-6, 8) - u * 10,
                         radius = r.range(3, 5) * (.8 + .4 * u), k = std::sin(PI * u);
            p.glow(x, y, radius * 2.4, C[3], .1 * k);
        });
        cycle(t, D, 14, o.seed + 4u, 1.2, 2, [&](double age, double life, Rng& r, int) {
            const double u = age / life, spread = r.range(.4, 1.1), phase = r.next() * TAU,
                         x = a.x + a.width * spread + std::sin(age * 2 + phase) * 2, y0 = r.range(0, 10),
                         speed = r.range(10, 18), y = a.y + y0 - age * speed,
                         tw = std::sin(PI * u) * (.6 + .4 * std::sin(age * 9 + r.next() * TAU));
            if (tw < .15) return;
            const int arm = tw > .7 && r.next() < .4 ? 1 : 0;
            p.sparkle(x, y, arm, C[4], C[3], .3 * tw);
            p.dot(x, y, tw > .6 ? C[5] : C[4], tw * 1.3);
        });
        std::array<std::pair<int, double>, 3> zs{};
        for (int i = 0; i < 3; ++i) zs[size_t(i)] = {i, wrap(t - (i * D) / 3, D) / D};
        std::stable_sort(zs.begin(), zs.end(), [](auto a0, auto b0) { return a0.second > b0.second; });
        for (const auto& [i, u] : zs) {
            const auto z = zAt(a, i, u);
            if (u > .9) {
                const double k = (u - .9) / .1;
                for (int j = 0; j < 7; ++j) {
                    const double angle = (double(j) / 7) * TAU + i, d = 3 + 6 * std::sqrt(k);
                    p.dot(z.x + std::cos(angle) * d, z.y + std::sin(angle) * d * .8, j % 2 ? C[4] : C[5], 1.2 - k);
                }
                p.glow(z.x, z.y, 10, C[3], .25 * (1 - k));
                continue;
            }
            for (int j = 1; j <= 6; ++j) {
                const auto q = zAt(a, i, std::max(0., u - j * .025));
                if (j % 2 == 0 || u - j * .025 < .05) continue;
                p.dot(q.x - 1, q.y + 4 + q.size, C[3], (1 - double(j) / 7) * .9);
            }
            const bool                    old = u > .78;
            const std::array<uint32_t, 4> ramp =
                old ? std::array<uint32_t, 4>{C[0], C[1], C[2], C[3]} : std::array<uint32_t, 4>{C[1], C[2], C[3], C[4]};
            status::icon(p, z.x, z.y, 7, [&](int dx, int dy) { return zMask(z.size, dx, dy); }, ramp, z.alpha);
            if (!old) p.px(z.x - 2 - z.size, z.y - 2 - z.size * .8, C[5]);
            p.glow(z.x, z.y, 7 + z.size * 3, C[3], (old ? .12 : .26) * z.alpha);
        }
    }
};
}  // namespace
const Descriptor& sleepDescriptor() {
    static const std::array<Option, 1> options{{{"actor", 0, 0, false, 0, &PixelFxSettings::actor}}};
    static const Descriptor            d{
        {PixelFxKind::Sleep, "status/sleep", 60, 82, 26, 74, D, true},
        6,
        [](PixelFxSettings& s) {
            s.seed         = 3;
            s.actor.source = PixelFxActorSource::Dummy;
            s.palette      = {hex(0x1a1636), hex(0x3c3a7a), hex(0x6f78c8), hex(0xa9b8f0), hex(0xdfe6ff), WHITE};
        },
        nullptr,
        options,
        nullptr,
        nullptr,
        [](const PixelFxSettings& o) -> std::unique_ptr<const AnalyticKernel> { return std::make_unique<Sleep>(o); },
        true};
    return d;
}
}  // namespace eve::stylize::pixelfx
