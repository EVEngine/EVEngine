// eve-hot-path
// Native adaptation of pixelfx status/stunned.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/PixelFxInternal.h"
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
#include "stylize/pixelfx/StatusVisual.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double                          D = 2;
constexpr std::array<std::string_view, 7> STAR7{"...#...", "..###..", "#######", ".#####.",
                                                "..###..", ".##.##.", ".#...#."};
char                                      bird(int dx, int dy, bool up) {
    const double x = dx + .5, y = dy + .5;
    if (dx == 3 && dy == -2) return 'e';
    if ((dx == 5 || dx == 4) && dy == -1) return dx == 5 ? 'b' : '#';
    constexpr std::array<std::array<int, 2>, 6> raised{{{-1, -1}, {-1, -2}, {-2, -2}, {-2, -3}, {-3, -3}, {-3, -4}}};
    constexpr std::array<std::array<int, 2>, 5> lowered{{{-1, 1}, {-1, 2}, {-2, 2}, {-2, 3}, {0, 1}}};
    const auto wing = up ? std::span<const std::array<int, 2>>(raised) : std::span<const std::array<int, 2>>(lowered);
    if (std::find(wing.begin(), wing.end(), std::array<int, 2>{dx, dy}) != wing.end()) return 'w';
    if (std::pow(x / 3.3, 2) + std::pow((y - .6) / 1.9, 2) <= 1 ||
        std::pow(x - 2.8, 2) + std::pow(y + 1.2, 2) <= std::pow(1.7, 2))
        return '#';
    if ((dx == -4 && (dy == -1 || dy == 0)) || (dx == -5 && dy == -1)) return '#';
    return 0;
}
void drawRing(Paint& p, double t, const PixelFxSettings& o, bool far) {
    const auto&  C   = o.palette;
    const auto&  B   = o.status.birds;
    const auto&  a   = o.actor;
    const auto   dim = [&](uint32_t c) { return far ? mix(c, B[0], .3) : c; };
    const double cy = a.y + 2, rx = a.width * .62, ry = 4.5;
    for (int i = 0; i < 5; ++i) {
        const double th = TAU * ((2 * t) / D + double(i) / 5), sn = std::sin(th), x = std::cos(th) * rx,
                     y = cy + sn * ry, depth = .5 + .5 * sn;
        const bool   near = sn >= 0, isBird = i % 2 == 1;
        if (near == far) continue;
        if (!isBird)
            for (int k = 1; k <= 12; ++k) {
                const double at = th - k * .085, px = std::cos(at) * rx, py = cy - 1 + std::sin(at) * ry;
                if ((std::sin(at) >= 0) == far ||
                    rand({uint32_t(i), uint32_t(k), jsUint(std::floor(((t / D) * 2 + k * .37) * 9))}) < .25)
                    continue;
                p.dot(px, py, dim(k < 4 ? C[6] : k < 8 ? C[5] : C[4]), 1.1 - double(k) / 13);
                if (k < 6) p.light(px, py, C[4], .3 * (1 - double(k) / 6));
            }
        if (isBird) {
            const bool   up  = int(std::floor(t * 8 + i * .5)) % 2 == 0;
            const double bob = std::sin((t / D) * TAU * 4 + i) * .7;
            status::icon(
                p, x, y - 3 + bob, 7, [&](int dx, int dy) { return bird(near ? -dx : dx, dy, up); },
                {B[0], dim(B[1]), dim(B[2]), dim(B[3])}, 1,
                [&](char ch) -> std::optional<uint32_t> {
                    if (ch == 'e') return B[0];
                    if (ch == 'b') return dim(o.status.beak);
                    if (ch == 'w') return dim(B[1]);
                    return std::nullopt;
                });
        } else {
            const double                  spin = (t / D) * TAU * 3 + i;
            const std::array<uint32_t, 4> ramp{C[0], dim(C[3]), dim(C[4]), dim(C[5])};
            if (depth > .55)
                status::icon(p, x, y - 1, 7,
                             status::shaped([](double u, double v) { return status::isInsideStar(u, v, .44); },
                                            4 + .8 * depth, spin),
                             ramp);
            else
                status::icon(p, x, y - 1, 5, [](int dx, int dy) { return status::bitmap(STAR7, dx, dy); }, ramp);
            p.px(x, y - 1, far ? C[5] : C[6]);
            p.glow(x, y - 1, 11, C[4], .24 + .3 * depth);
            p.glow(x, y - 1, 4, C[6], .3 * depth);
            const double angle = std::atan2(std::sin(th - PI / 2), std::cos(th - PI / 2)) / .32,
                         fr    = std::exp(-std::pow(angle, 2));
            if (!far && fr > .08) {
                const std::array<uint32_t, 4> f{C[3], C[4], C[5], C[6]};
                flare(p, x, y - 1, 9 * fr, fr, f);
            }
        }
    }
}
void back(Paint& p, double t, const PixelFxSettings& o) { drawRing(p, t, o, true); }
void draw(Paint& p, double t, const PixelFxSettings& o) {
    const auto& C = o.palette;
    const auto& a = o.actor;
    drawRing(p, t, o, false);
    cycle(t, D, 14, o.seed + 3u, .25, .5, [&](double age, double life, Rng& r, int) {
        const double th = r.range(0, TAU), u = age / life, spread = r.range(.45, .95),
                     x = std::cos(th) * a.width * spread, y = a.y + 1 + std::sin(th) * 7 - r.range(0, 9),
                     arm = jsRound(r.range(1, 2.6) * std::sin(PI * u));
        if (arm < 1) p.dot(x, y, C[6], std::sin(PI * u) * 1.4);
        p.sparkle(x, y, arm, C[5], C[4], .35 * std::sin(PI * u));
    });
    p.glow(0, a.y + 1, 26, C[4], .13);
}
}  // namespace
const Descriptor& stunnedDescriptor() {
    static const std::array<Option, 3> options{
        {{"actor", 0, 0, false, 0, &PixelFxSettings::actor},
         {"birds", 1, 64, false, 4, &PixelFxStatusSettings::birds},
         {"beak", 0, 4294967295., false, 0, &PixelFxStatusSettings::beak, true}}};
    static const Descriptor d{{PixelFxKind::Stunned, "status/stunned", 60, 64, 30, 56, D, true},
                              7,
                              [](PixelFxSettings& s) {
                                  s.seed         = 2;
                                  s.actor.source = PixelFxActorSource::Dummy;
                                  s.palette      = {hex(0x3a1e0c), hex(0x8a4a12), hex(0xd88a1c), hex(0xf5c030),
                                                    hex(0xffe36e), hex(0xfff6c8), WHITE};
                                  s.status.birds = {hex(0x1a1422), hex(0x6a7fae), hex(0xb8c8e8), hex(0xf4f8ff)};
                                  s.status.beak  = hex(0xffae35);
                              },
                              draw,
                              options,
                              nullptr,
                              back,
                              nullptr,
                              true};
    return d;
}
}  // namespace eve::stylize::pixelfx
