// eve-hot-path
// Native adaptation of pixelfx shadow/necro-skulls.js, MIT.
// Copyright (c) 2026 Patch; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/PixelFxInternal.h"
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Shadow.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double                           CY = -52, FB = -32, FH = 34, RX = 42, RY = 11, TILT = -.12, D = 3;
constexpr int                              N = 5;
constexpr std::array<std::string_view, 12> SKULL{"...kkkkk...", ".kkhhhllkk.", "khhhhlllmmk", "khhllllllmk",
                                                 "kleeeleeemk", "kleeeleeedk", "kmleelleedk", ".kmllnllmk.",
                                                 ".kmmlnlmdk.", "..kmmmmmk..", "..ktgtgtk..", "...kkkkk..."};
struct SkullPosition {
    double x, y, z;
};
SkullPosition skullAt(int i, double t) {
    const double a = (TAU * t) / D + (double(i) * TAU) / N, ex = std::cos(a) * RX, ey = std::sin(a) * RY,
                 bob = std::sin((TAU * 2 * t) / D + i * 1.7) * 2;
    return {ex * std::cos(TILT) - ey * std::sin(TILT), CY + ex * std::sin(TILT) + ey * std::cos(TILT) + bob,
            std::sin(a)};
}
void tail(Paint& p, const std::vector<uint32_t>& C, int i, double t) {
    for (int k = 8; k >= 1; --k) {
        const auto   s   = skullAt(i, t - k * .05);
        const double age = double(k) / 8, r = 2 + age * 3.6, a = (1 - age) * .5 * (.6 + .4 * (s.z + 1) / 2);
        p.disc(s.x, s.y - age * 7 + 2, r, age < .4 ? C[5] : age < .7 ? C[4] : C[3], a);
    }
}
void drawSkull(Paint& p, const std::vector<uint32_t>& C, const std::vector<uint32_t>& K, const SkullPosition& s,
               double t, int i) {
    const bool                front = s.z >= 0;
    const double              depth = .55 + .45 * (s.z + 1) / 2;
    std::array<uint32_t, 256> pal{};
    pal['k']        = K[0];
    pal['d']        = front ? K[1] : K[0];
    pal['m']        = front ? K[2] : K[1];
    pal['l']        = front ? K[3] : K[2];
    pal['h']        = front ? K[4] : K[3];
    pal['e']        = C[0];
    pal['n']        = C[0];
    pal['t']        = front ? K[3] : K[2];
    pal['g']        = K[0];
    const double x0 = jsRound(s.x) - 5, y0 = jsRound(s.y) - 6;
    p.sprite(SKULL, pal, x0, y0);
    const double k = (.5 + .5 * std::sin((TAU * 3 * t) / D + i * 2.1)) * .5 + .5;
    for (int ex : {3, 7}) {
        p.px(x0 + ex + .5, y0 + 5.5, front ? (k > .75 ? WHITE : C[7]) : C[6]);
        if (front) p.px(x0 + ex + .5, y0 + 4.5, C[5]);
        p.glow(x0 + ex + 1, y0 + 6, 6, C[5], .45 * depth * k);
    }
    p.glow(s.x, s.y, 11, C[4], .18 * depth);
}
void flame(Paint& p, const std::vector<uint32_t>& C, double t) {
    constexpr double W      = 10;
    const double     scroll = t / D;
    p.shader(-W - 4, FB - FH - 4, W + 4, FB + 2, [&](double x, double y, ShadeOutput& out) {
        const double u = (FB - y) / FH;
        if (u < -.04 || u >= 1) return;
        const double n1   = tileNoise(x * .3, y * .2 + scroll * 8, 0, 8, 3),
                     n2   = tileNoise(x * .6, y * .45 + scroll * 16, 0, 16, 5),
                     prof = u < .22 ? .35 + .65 * std::sqrt(std::max(0., u) / .22) : std::pow(1 - (u - .22) / .78, .85),
                     w = W * prof * (1 + .3 * n1) + n2 * 2.2 * u, sway = n1 * 3 * u * u,
                     d = std::abs(x - sway) / std::max(.6, w);
        if (d > 1) return;
        const double heat = std::pow(1 - d, .7) * std::pow(std::max(0., 1 - u), .55) + n2 * .12;
        if (((int(std::floor(x)) + int(std::floor(y))) & 1) == 0) out.k = .16 * heat;
        out.c = heat > .82   ? WHITE
                : heat > .66 ? C[7]
                : heat > .46 ? C[6]
                : heat > .27 ? C[5]
                : heat > .12 ? C[4]
                             : C[3];
    });
    p.glow(0, FB - FH * .35, 40, C[5], .45);
    p.glow(0, FB - 5, 12, C[7], .25);
}
void orbit(Paint& p, double t, const PixelFxSettings& o, bool front) {
    struct Entry {
        int           i;
        SkullPosition s;
    };
    std::array<Entry, N> order{};
    size_t               count = 0;
    for (int i = 0; i < N; ++i) {
        const auto s = skullAt(i, t);
        if ((s.z >= 0) == front) order[count++] = {i, s};
    }
    // Five entries: preserve JS stable depth ordering without per-draw allocation.
    for (size_t j = 1; j < count; ++j) {
        const auto entry = order[j];
        size_t     k     = j;
        while (k > 0 && order[k - 1].s.z > entry.s.z) {
            order[k] = order[k - 1];
            --k;
        }
        order[k] = entry;
    }
    for (size_t j = 0; j < count; ++j) tail(p, o.palette, order[j].i, t);
    for (size_t j = 0; j < count; ++j) drawSkull(p, o.palette, o.shadow.bone, order[j].s, t, order[j].i);
}
struct NecroSkulls final : AnalyticKernel {
    void drawBack(Paint& p, double t, const PixelFxSettings& o) const override {
        p.glow(0, -1, 40, o.palette[4], .2);
        orbit(p, t, o, false);
    }
    void draw(Paint& p, double t, const PixelFxSettings& o) const override {
        const auto& C = o.palette;
        cycle(t, D, 18, o.seed, .8, 1.4, [&](double age, double L, Rng& r, int) {
            const double x0 = r.range(-6, 6), u = age / L, x = x0 + std::sin(u * 5 + r.range(0, 6)) * 3 * u,
                         y = FB - 6 - u * r.range(36, 56);
            p.dot(x, y, u < .5 ? C[7] : C[6], u < .7 ? 1 : (1 - u) * 3.3);
            p.glow(x, y, 3, C[6], .3 * (1 - u));
        });
        flame(p, C, t);
        orbit(p, t, o, true);
        shadow::halo(p, 0, CY, 0, 46, C[4], .12);
    }
};
}  // namespace
const Descriptor& necroSkullsDescriptor() {
    static const std::array<Option, 1> options{{{"bone", 1, 64, false, 5, &PixelFxShadowSettings::bone}}};
    static const Descriptor            d{{PixelFxKind::NecroSkulls, "shadow/necro-skulls", 136, 116, 68, 108, D, true},
                                         8,
                                         [](PixelFxSettings& s) {
                                  s.seed        = 6;
                                  s.palette     = {hex(0x040e10), hex(0x0a2228), hex(0x0f3d44), hex(0x16615f),
                                                   hex(0x23907f), hex(0x45c4a0), hex(0x9af0c8), hex(0xe8fff4)};
                                  s.shadow.bone = {hex(0x0c1a1c), hex(0x2c4a4a), hex(0x5c847c), hex(0xa6cdb8),
                                                   hex(0xe4fff0)};
                                         },
                                         nullptr,
                                         options,
                                         nullptr,
                                         nullptr,
                                         [](const PixelFxSettings&) -> std::unique_ptr<const AnalyticKernel> {
                                  return std::make_unique<NecroSkulls>();
                                         }};
    return d;
}
}  // namespace eve::stylize::pixelfx
